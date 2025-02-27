/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file Use external cblas library call.
 */
#include <tvm/runtime/data_type.h>
#include <tvm/runtime/logging.h>
#include <tvm/runtime/registry.h>

#include "../cblas/gemm_common.h"
#include "cublas_utils.h"

// <bojian/DietCode>
// #include "cutlass/library/handle.h"


#define CHECK_CUTLASS_ERROR(status)                                                              \
  {                                                                                              \
    cutlass::Status error = status;                                                              \
    if (error != cutlass::Status::kSuccess) {                                                    \
      std::cerr << "Got cutlass error: " << cutlassGetStatusString(error) << " at: " << __LINE__ \
                << std::endl;                                                                    \
      exit(EXIT_FAILURE);                                                                        \
    }                                                                                            \
  }


namespace tvm {
namespace contrib {

using namespace runtime;
inline cublasOperation_t CUBLASBooleanToTranspose(bool item) {
  return item ? CUBLAS_OP_T : CUBLAS_OP_N;
}

inline void CUBLASTryEnableTensorCore(cublasHandle_t hdl) {
  // TensorCores are only supported in cublas 9.0 or higher
  int version;
  CHECK_CUBLAS_ERROR(cublasGetVersion(hdl, &version));
  // <bojian/DietCode> Disabled tensor cores for fair comparison.
  // if (version >= 9000) CHECK_CUBLAS_ERROR(cublasSetMathMode(hdl, CUBLAS_TENSOR_OP_MATH));
}

struct CublasHgemmOp {
  typedef half TDatatype;
  cublasHandle_t handle;
  explicit CublasHgemmOp(cublasHandle_t hdl) : handle(hdl) {}

  void operator()(bool ta, bool tb, int M, int N, int K, half alpha, half* A, int lda, half* B,
                  int ldb, half beta, half* C, int ldc) {
    CHECK_CUBLAS_ERROR(cublasHgemm(handle, CUBLASBooleanToTranspose(ta),
                                   CUBLASBooleanToTranspose(tb), M, N, K, &alpha, A, lda, B, ldb,
                                   &beta, C, ldc));
  }
};

struct CublasSgemmOp {
  typedef float TDatatype;
  cublasHandle_t handle;
  explicit CublasSgemmOp(cublasHandle_t hdl) : handle(hdl) {}

  void operator()(bool ta, bool tb, int M, int N, int K, float alpha, float* A, int lda, float* B,
                  int ldb, float beta, float* C, int ldc) {
    CHECK_CUBLAS_ERROR(cublasSgemm(handle, CUBLASBooleanToTranspose(ta),
                                   CUBLASBooleanToTranspose(tb), M, N, K, &alpha, A, lda, B, ldb,
                                   &beta, C, ldc));
  }
};

// <bojian/DietCode>
/*
struct CutlassSgemmOp {
  ::cutlass::library::Handle* handle;

  explicit CutlassSgemmOp(::cutlass::library::Handle* const hdl) : handle(hdl) {}

  void operator()(DLTensor* a, DLTensor* b, DLTensor* c, DLTensor* bias = nullptr,
                  bool transpose_a = false, bool transpose_b = false);
};

using cutlass::library::GemmUniversalMode;
using cutlass::library::NumericTypeID;
using cutlass::library::ComplexTransform;
using cutlass::library::EpilogueKind;

void CutlassSgemmOp::operator()(DLTensor* a, DLTensor* b, DLTensor* c, DLTensor* bias,
                                bool transpose_a, bool transpose_b) {
  int m = c->shape[1];
  int n = c->shape[0];
  int k = transpose_b ? b->shape[1] : b->shape[0];

  int lda = a->shape[1];
  int ldb = b->shape[1];
  int ldc = c->shape[1];

  const float* ptra = static_cast<float*>(a->data);
  const float* ptrb = static_cast<float*>(b->data);
  float* ptrc = static_cast<float*>(c->data);
  const float* ptrbias = bias ? static_cast<float*>(bias->data) : ptrc;

  const auto layouta = transpose_a ? ::cutlass::library::LayoutTypeID::kRowMajor : ::cutlass::library::LayoutTypeID::kColumnMajor;
  const auto layoutb = transpose_b ? ::cutlass::library::LayoutTypeID::kRowMajor : ::cutlass::library::LayoutTypeID::kColumnMajor;

  float alpha = 1.0;
  float beta = bias ? 1.0 : 0.0;

  // alpha * A*B + beta * C
  CHECK_CUTLASS_ERROR(handle->gemm_universal(
    GemmUniversalMode::kGemm,
    m, n, k,
    NumericTypeID::kF32,                            // data type of internal accumulation
    NumericTypeID::kF32,                            // data type of alpha/beta scalars
    &alpha,                                         // pointer to alpha scalar
    ::cutlass::library::NumericTypeID::kF32,        // data type of B matrix
    layoutb,                                        // layout of B matrix
    ::cutlass::library::ComplexTransform::kNone,    // complex transform of A matrix
    ptrb,                                           // pointer to B matrix in device memory
    ldb,                                            // leading dimension of B matrix
    ::cutlass::library::NumericTypeID::kF32,        // data type of A matrix
    layouta,                                        // layout of A matrix
    ::cutlass::library::ComplexTransform::kNone,    // complex transform of A matrix
    ptra,                                           // pointer to A matrix in device memory
    lda,                                            // leading dimension of A matrix
    &beta,                                          // pointer to beta scalar
    ::cutlass::library::NumericTypeID::kF32,        // data type of C and D matrix
    ptrbias,                                        // pointer to C matrix in device memory
    0,                                              // leading dimension fo C matrix
    ptrc,                                           // pointer to D matrix in device memory
    ldc,                                            // leading dimension of D matrix
    1, 0, 0, 0, 0
  ));
}

TVM_REGISTER_GLOBAL("tvm.contrib.cutlass.matmul")
    .set_body([](TVMArgs args, TVMRetValue* ret) {

      // using TGemmOp = CutlassSgemmOp;

      DLTensor* A = args[0];
      DLTensor* B = args[1];
      DLTensor* C = args[2];
      bool transa = args[3];
      bool transb = args[4];
      int bit_depth = sizeof(float) * 8;
      ICHECK_EQ(A->ndim, 2);
      ICHECK_EQ(B->ndim, 2);
      ICHECK_EQ(C->ndim, 2);

      ICHECK_EQ(ElementStride(A), 1);
      ICHECK_EQ(ElementStride(B), 1);
      ICHECK_EQ(ElementStride(C), 1);

      // C can never be transposed.
      ICHECK(!IsInPlaceTransposed(C));

      // Reversed strides indicates an in-place transpose operation.
      transa = IsInPlaceTransposed(A) ? !transa : transa;
      transb = IsInPlaceTransposed(B) ? !transb : transb;

      ICHECK(TypeMatch(B->dtype, kDLFloat, bit_depth));
      ICHECK(TypeMatch(C->dtype, kDLFloat, bit_depth));

      CuTlassThreadEntry* entry_ptr = CuTlassThreadEntry::ThreadLocal();
      CutlassSgemmOp op(&entry_ptr->handle);
      op(A, B, C, nullptr, transa, transb);
    });
 */

struct CublasDgemmOp {
  typedef double TDatatype;
  cublasHandle_t handle;
  explicit CublasDgemmOp(cublasHandle_t hdl) : handle(hdl) {}
  void operator()(bool ta, bool tb, int M, int N, int K, double alpha, double* A, int lda,
                  double* B, int ldb, double beta, double* C, int ldc) {
    CHECK_CUBLAS_ERROR(cublasDgemm(handle, CUBLASBooleanToTranspose(ta),
                                   CUBLASBooleanToTranspose(tb), M, N, K, &alpha, A, lda, B, ldb,
                                   &beta, C, ldc));
  }
};

struct CublasHgemmBatchOp {
  typedef half TDatatype;
  cublasHandle_t handle;
  explicit CublasHgemmBatchOp(cublasHandle_t hdl) : handle(hdl) {}
  void operator()(int batch_size, bool ta, bool tb, int M, int N, int K, half alpha, half* A,
                  int a_stride, int lda, half* B, int b_stride, int ldb, half beta, half* C,
                  int c_stride, int ldc) {
    CHECK_CUBLAS_ERROR(cublasHgemmStridedBatched(
        handle, CUBLASBooleanToTranspose(ta), CUBLASBooleanToTranspose(tb), M, N, K, &alpha, A, lda,
        a_stride, B, ldb, b_stride, &beta, C, ldc, c_stride, batch_size));
  }
};

struct CublasSgemmBatchOp {
  typedef float TDatatype;
  cublasHandle_t handle;
  explicit CublasSgemmBatchOp(cublasHandle_t hdl) : handle(hdl) {}
  void operator()(int batch_size, bool ta, bool tb, int M, int N, int K, float alpha, float* A,
                  int a_stride, int lda, float* B, int b_stride, int ldb, float beta, float* C,
                  int c_stride, int ldc) {
    CHECK_CUBLAS_ERROR(cublasSgemmStridedBatched(
        handle, CUBLASBooleanToTranspose(ta), CUBLASBooleanToTranspose(tb), M, N, K, &alpha, A, lda,
        a_stride, B, ldb, b_stride, &beta, C, ldc, c_stride, batch_size));
  }
};

struct CublasDgemmBatchOp {
  typedef double TDatatype;
  cublasHandle_t handle;
  explicit CublasDgemmBatchOp(cublasHandle_t hdl) : handle(hdl) {}
  void operator()(int batch_size, bool ta, bool tb, int M, int N, int K, double alpha, double* A,
                  int a_stride, int lda, double* B, int b_stride, int ldb, double beta, double* C,
                  int c_stride, int ldc) {
    CHECK_CUBLAS_ERROR(cublasDgemmStridedBatched(
        handle, CUBLASBooleanToTranspose(ta), CUBLASBooleanToTranspose(tb), M, N, K, &alpha, A, lda,
        a_stride, B, ldb, b_stride, &beta, C, ldc, c_stride, batch_size));
  }
};

// Check cublas supported mix-precision computation type and return computeType
bool CheckMixPrecisionType(DLDataType in_dtype, DLDataType out_dtype, bool int_support = true) {
  if (int_support && TypeMatch(out_dtype, kDLInt, 32)) {
    return TypeMatch(in_dtype, kDLInt, 8);
  } else if (TypeMatch(out_dtype, kDLFloat, 32)) {
    return TypeMatch(in_dtype, kDLInt, 8) || TypeMatch(in_dtype, kDLFloat, 16);
  } else {
    return false;
  }
}

int roundoff(int v, int d) { return (v + d - 1) / d * d; }

#if CUDART_VERSION >= 10010
inline void CallLtIgemm(TVMArgs args, TVMRetValue* ret, cublasLtHandle_t hdl) {
  DLTensor* A = args[0];
  DLTensor* B = args[1];
  DLTensor* C = args[2];
  bool transa = args[3];
  bool transb = args[4];
  // Reversed strides indicates an in-place transpose operation.
  transa = IsInPlaceTransposed(A) ? !transa : transa;
  transb = IsInPlaceTransposed(B) ? !transb : transb;
  int M = ColumnCount(B, transb);
  int N = RowCount(A, transa);
  int K = ColumnCount(A, transa);
  int N_out = ColumnCount(C, false);
  int m = M;
  int n = m;
  int k = m;
  int lda = M * K / (roundoff(K, 32) / 32);
  int ldb = K * N / (roundoff(K, 32) / 32);
  int ldc = M * N_out / (roundoff(N_out, 32) / 32);
  ICHECK_EQ(A->ndim, 2);
  ICHECK_EQ(B->ndim, 2);
  ICHECK_EQ(C->ndim, 2);

  ICHECK_EQ(ElementStride(A), 1);
  ICHECK_EQ(ElementStride(B), 1);
  ICHECK_EQ(ElementStride(C), 1);

  ICHECK(TypeEqual(A->dtype, B->dtype));
  ICHECK(TypeMatch(A->dtype, kDLInt, 8));
  ICHECK(TypeMatch(C->dtype, kDLInt, 32));

  ICHECK(CheckMixPrecisionType(A->dtype, C->dtype)) << "Unsupported data type";
  int32_t alpha = args.size() > 5 ? args[5] : 1;
  int32_t beta = args.size() > 6 ? args[6] : 0;
  cublasLtMatrixLayout_t Adesc = nullptr, Bdesc = nullptr, Cdesc = nullptr;
  auto A_data = reinterpret_cast<void*>(static_cast<char*>(A->data) + A->byte_offset);
  auto B_data = reinterpret_cast<void*>(static_cast<char*>(B->data) + B->byte_offset);
  auto C_data = reinterpret_cast<void*>(static_cast<char*>(C->data) + C->byte_offset);

  cublasOperation_t opTranspose = CUBLAS_OP_T;
  cublasLtOrder_t order_COL32 = CUBLASLT_ORDER_COL32;
  cublasLtOrder_t order_COL4_4R2_8C = CUBLASLT_ORDER_COL4_4R2_8C;
  cublasLtMatmulDesc_t operationDesc = nullptr;
#if CUDART_VERSION >= 11000
  CHECK_CUBLAS_ERROR(cublasLtMatmulDescCreate(&operationDesc, CUBLAS_COMPUTE_32I, CUDA_R_32I));
#else
  CHECK_CUBLAS_ERROR(cublasLtMatmulDescCreate(&operationDesc, CUDA_R_32I));
#endif
  CHECK_CUBLAS_ERROR(cublasLtMatmulDescSetAttribute(operationDesc, CUBLASLT_MATMUL_DESC_TRANSB,
                                                    &opTranspose, sizeof(opTranspose)));
  cublasOperation_t opTransA = CUBLASBooleanToTranspose(transa);
  cublasOperation_t opTransB = CUBLASBooleanToTranspose(transb);
  CHECK_CUBLAS_ERROR(cublasLtMatmulDescSetAttribute(operationDesc, CUBLASLT_MATMUL_DESC_TRANSA,
                                                    &opTransA, sizeof(opTransA)));
  CHECK_CUBLAS_ERROR(cublasLtMatmulDescSetAttribute(operationDesc, CUBLASLT_MATMUL_DESC_TRANSB,
                                                    &opTransB, sizeof(opTransB)));
  // Create descriptors for the original matrices
  CHECK_CUBLAS_ERROR(cublasLtMatrixLayoutCreate(&Adesc, CUDA_R_8I, opTransA == CUBLAS_OP_N ? m : k,
                                                opTransA == CUBLAS_OP_N ? k : m, lda));
  CHECK_CUBLAS_ERROR(cublasLtMatrixLayoutCreate(&Bdesc, CUDA_R_8I, opTransB == CUBLAS_OP_N ? k : n,
                                                opTransB == CUBLAS_OP_N ? n : k, ldb));
  CHECK_CUBLAS_ERROR(cublasLtMatrixLayoutCreate(&Cdesc, CUDA_R_32I, m, n, ldc));

  CHECK_CUBLAS_ERROR(cublasLtMatrixLayoutSetAttribute(Adesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                                      &order_COL32, sizeof(order_COL32)));
  CHECK_CUBLAS_ERROR(cublasLtMatrixLayoutSetAttribute(
      Bdesc, CUBLASLT_MATRIX_LAYOUT_ORDER, &order_COL4_4R2_8C, sizeof(order_COL4_4R2_8C)));
  CHECK_CUBLAS_ERROR(cublasLtMatrixLayoutSetAttribute(Cdesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                                      &order_COL32, sizeof(order_COL32)));

  CHECK_CUBLAS_ERROR(cublasLtMatmul(hdl, operationDesc, &alpha, B_data, Adesc, A_data, Bdesc, &beta,
                                    C_data, Cdesc, C_data, Cdesc, nullptr, nullptr, 0, nullptr));
}
#endif

inline void CallGemmEx(TVMArgs args, TVMRetValue* ret, cublasHandle_t hdl) {
  DLTensor* A = args[0];
  DLTensor* B = args[1];
  DLTensor* C = args[2];
  bool transa = args[3];
  bool transb = args[4];
  ICHECK_EQ(A->ndim, 2);
  ICHECK_EQ(B->ndim, 2);
  ICHECK_EQ(C->ndim, 2);

  ICHECK_EQ(ElementStride(A), 1);
  ICHECK_EQ(ElementStride(B), 1);
  ICHECK_EQ(ElementStride(C), 1);

  ICHECK(TypeEqual(A->dtype, B->dtype));

  // C can never be transposed.
  ICHECK(!IsInPlaceTransposed(C));

  // Reversed strides indicates an in-place transpose operation.
  transa = IsInPlaceTransposed(A) ? !transa : transa;
  transb = IsInPlaceTransposed(B) ? !transb : transb;

  ICHECK(CheckMixPrecisionType(A->dtype, C->dtype)) << "Unsupported data type";
  ICHECK(!TypeMatch(A->dtype, kDLInt, 8) || ColumnStride(A) % 4 == 0)
      << "leading dimension must divide 4 for int8 gemm";
  ICHECK(!TypeMatch(B->dtype, kDLInt, 8) || ColumnStride(B) % 4 == 0)
      << "leading dimension must divide 4 for int8 gemm";
  double alpha = args.size() > 5 ? args[5] : 1.0;
  double beta = args.size() > 6 ? args[6] : 0.0;

  cudaDataType_t cuda_in_type = GetCudaDataType(A->dtype);
  cudaDataType_t cuda_out_type = GetCudaDataType(C->dtype);
  cublasGemmAlgo_t algo = CUBLAS_GEMM_DEFAULT;
  void *alpha_ptr = nullptr, *beta_ptr = nullptr;
  auto alpha_int = static_cast<int32_t>(alpha);
  auto beta_int = static_cast<int32_t>(beta);
  auto alpha_float = static_cast<float>(alpha);
  auto beta_float = static_cast<float>(beta);
  if (C->dtype.code == kDLInt) {
    alpha_ptr = &alpha_int;
    beta_ptr = &beta_int;
  } else if (C->dtype.code == kDLFloat) {
    alpha_ptr = &alpha_float;
    beta_ptr = &beta_float;
  }

  auto A_data = reinterpret_cast<void*>(static_cast<char*>(A->data) + A->byte_offset);
  auto B_data = reinterpret_cast<void*>(static_cast<char*>(B->data) + B->byte_offset);
  auto C_data = reinterpret_cast<void*>(static_cast<char*>(C->data) + C->byte_offset);

  CHECK_CUBLAS_ERROR(
      cublasGemmEx(hdl, CUBLASBooleanToTranspose(transb), CUBLASBooleanToTranspose(transa),
                   ColumnCount(B, transb), RowCount(A, transa), ColumnCount(A, transa), alpha_ptr,
                   B_data, cuda_in_type, ColumnStride(B), A_data, cuda_in_type, ColumnStride(A),
                   beta_ptr, C_data, cuda_out_type, ColumnStride(C), cuda_out_type, algo));
}

inline void CallBatchGemmEx(TVMArgs args, TVMRetValue* ret, cublasHandle_t hdl) {
  DLTensor* A = args[0];
  DLTensor* B = args[1];
  DLTensor* C = args[2];
  bool transa = args[3];
  bool transb = args[4];
  ICHECK_EQ(A->ndim, 3);
  ICHECK_EQ(B->ndim, 3);
  ICHECK_EQ(C->ndim, 3);

  int batch_size = BatchCount3D(C);
  ICHECK_EQ(ElementStride(A), 1);
  ICHECK_EQ(ElementStride(B), 1);
  ICHECK_EQ(ElementStride(C), 1);

  ICHECK(TypeEqual(A->dtype, B->dtype));

  // C can never be transposed.
  ICHECK(!IsInPlaceTransposed(C));

  // Reversed strides indicates an in-place transpose operation.
  transa = IsInPlaceTransposed(A) ? !transa : transa;
  transb = IsInPlaceTransposed(B) ? !transb : transb;

  ICHECK(CheckMixPrecisionType(A->dtype, C->dtype, false)) << "Unsupported data type";
  ICHECK(!TypeMatch(A->dtype, kDLInt, 8) || ColumnStride(A) % 4 == 0)
      << "leading dimension must divide 4 for int8 gemm";
  ICHECK(!TypeMatch(B->dtype, kDLInt, 8) || ColumnStride(B) % 4 == 0)
      << "leading dimension must divide 4 for int8 gemm";
  double alpha = args.size() > 5 ? args[5] : 1.0;
  double beta = args.size() > 6 ? args[6] : 0.0;

  int A_stride = A->shape[1] * A->shape[2];
  int B_stride = B->shape[1] * B->shape[2];
  int C_stride = C->shape[1] * C->shape[2];

  // Broadcast A or B by changing its stride.
  int batch_size_a = BatchCount3D(A);
  int batch_size_b = BatchCount3D(B);
  if (batch_size_a != batch_size_b) {
    if (batch_size_a == 1) {
      A_stride = 0;
    } else if (batch_size_b == 1) {
      B_stride = 0;
    }
  } else {
    ICHECK_EQ(batch_size_a, batch_size);
    ICHECK_EQ(batch_size_b, batch_size);
  }

  cudaDataType_t cuda_in_type = GetCudaDataType(A->dtype);
  cudaDataType_t cuda_out_type = GetCudaDataType(C->dtype);
  cublasGemmAlgo_t algo = CUBLAS_GEMM_DEFAULT;
  void *alpha_ptr = nullptr, *beta_ptr = nullptr;
  auto alpha_int = static_cast<int32_t>(alpha);
  auto beta_int = static_cast<int32_t>(beta);
  auto alpha_float = static_cast<float>(alpha);
  auto beta_float = static_cast<float>(beta);
  if (C->dtype.code == kDLInt) {
    alpha_ptr = &alpha_int;
    beta_ptr = &beta_int;
  } else if (C->dtype.code == kDLFloat) {
    alpha_ptr = &alpha_float;
    beta_ptr = &beta_float;
  }

  auto A_data = reinterpret_cast<void*>(static_cast<char*>(A->data) + A->byte_offset);
  auto B_data = reinterpret_cast<void*>(static_cast<char*>(B->data) + B->byte_offset);
  auto C_data = reinterpret_cast<void*>(static_cast<char*>(C->data) + C->byte_offset);
  CHECK_CUBLAS_ERROR(cublasGemmStridedBatchedEx(
      hdl, CUBLASBooleanToTranspose(transb), CUBLASBooleanToTranspose(transa),
      ColumnCount3D(B, transb), RowCount3D(A, transa), ColumnCount3D(A, transa), alpha_ptr, B_data,
      cuda_in_type, ColumnStride3D(B), B_stride, A_data, cuda_in_type, ColumnStride3D(A), A_stride,
      beta_ptr, C_data, cuda_out_type, ColumnStride3D(C), C_stride, batch_size, cuda_out_type,
      algo));
}

// matrix multiplication for row major
TVM_REGISTER_GLOBAL("tvm.contrib.cublas.matmul").set_body([](TVMArgs args, TVMRetValue* ret) {
  DLTensor* A = args[0];
  DLTensor* C = args[2];

  CuBlasThreadEntry* entry_ptr = CuBlasThreadEntry::ThreadLocal();

  CUBLASTryEnableTensorCore(entry_ptr->handle);

  if (TypeEqual(A->dtype, C->dtype)) {
    ICHECK(TypeMatch(A->dtype, kDLFloat, 16) || TypeMatch(A->dtype, kDLFloat, 32) ||
           TypeMatch(A->dtype, kDLFloat, 64));

    if (TypeMatch(A->dtype, kDLFloat, 16))
      CallGemm(args, ret, CublasHgemmOp(entry_ptr->handle));
    else if (TypeMatch(A->dtype, kDLFloat, 32))
      CallGemm(args, ret, CublasSgemmOp(entry_ptr->handle));
    else
      CallGemm(args, ret, CublasDgemmOp(entry_ptr->handle));
  } else {
    CallGemmEx(args, ret, entry_ptr->handle);
  }
});

#if CUDART_VERSION >= 10010
TVM_REGISTER_GLOBAL("tvm.contrib.cublaslt.matmul").set_body([](TVMArgs args, TVMRetValue* ret) {
  DLTensor* A = args[0];

  CuBlasThreadEntry* entry_ptr = CuBlasThreadEntry::ThreadLocal();

  CUBLASTryEnableTensorCore(entry_ptr->handle);

  ICHECK(TypeMatch(A->dtype, kDLInt, 8)) << "Expects dtype to be int8\n";
  cublasLtHandle_t ltHandle;
  CHECK_CUBLAS_ERROR(cublasLtCreate(&ltHandle));
  CallLtIgemm(args, ret, ltHandle);
  CHECK_CUBLAS_ERROR(cublasLtDestroy(ltHandle));
});
#endif  // CUDART_VERSION >= 10010

TVM_REGISTER_GLOBAL("tvm.contrib.cublas.batch_matmul").set_body([](TVMArgs args, TVMRetValue* ret) {
  DLTensor* A = args[0];
  DLTensor* C = args[2];

  CuBlasThreadEntry* entry_ptr = CuBlasThreadEntry::ThreadLocal();

  CUBLASTryEnableTensorCore(entry_ptr->handle);
  if (TypeEqual(A->dtype, C->dtype)) {
    ICHECK(TypeMatch(A->dtype, kDLFloat, 16) || TypeMatch(A->dtype, kDLFloat, 32) ||
           TypeMatch(A->dtype, kDLFloat, 64));

    if (TypeMatch(A->dtype, kDLFloat, 16))
      CallBatchGemm(args, ret, CublasHgemmBatchOp(entry_ptr->handle));
    else if (TypeMatch(A->dtype, kDLFloat, 32))
      CallBatchGemm(args, ret, CublasSgemmBatchOp(entry_ptr->handle));
    else
      CallBatchGemm(args, ret, CublasDgemmBatchOp(entry_ptr->handle));
  } else {
    CallBatchGemmEx(args, ret, entry_ptr->handle);
  }
});

}  // namespace contrib
}  // namespace tvm
