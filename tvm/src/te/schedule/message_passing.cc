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
 * \file message_passing.cc
 * \brief The message passing domain.
 */
#include "message_passing.h"

#include <tvm/arith/analyzer.h>
#include <tvm/tir/expr.h>

// <bojian/DietCode>
#include "../../auto_scheduler/utils.h"
#include "../../arith/interval_set.h"
#include <tvm/tir/dyn_shape_var_functor.h>
#include <tvm/tir/expr_functor.h>

namespace tvm {


// <bojian/DietCode>
// namespace auto_scheduler {
// extern bool enable_verbose_logging;
// }


namespace te {

// <bojian/DietCode>
// using auto_scheduler::enable_verbose_logging;
using auto_scheduler::ArrayToString;

using namespace tir;

void Update(std::unordered_map<IterVar, Range>* p_state, const IterVar& iv, Range r,
            arith::Analyzer* analyzer) {
  auto it = p_state->find(iv);
  if (it == p_state->end()) {
    (*p_state)[iv] = r;
    analyzer->Bind(iv->var, r);
  } else {
    bool match =
        is_zero(it->second->min) && analyzer->CanProve(r->extent - it->second->extent == 0);
    ICHECK(match) << iv << " domain already inferred,"
                  << " cannot prove their extents are the same " << it->second->extent << " vs "
                  << r->extent;
  }
}

/*!
 * \param Upward propagating whether an IterVar derives at least one leaf IterVar that binds to
 * a thread.
 *
 * \param stage The stage to operate on.
 * \param p_state The propagation result of each IterVar.
 */
void PassUpThreadBinding(const Stage& stage, std::unordered_map<IterVar, bool>* p_state) {
  auto bound_to_thread = [&stage](const IterVar& iv) {
    bool bound = false;
    auto it = stage->iter_var_attrs.find(iv);
    if (it != stage->iter_var_attrs.end()) {
      bound = (*it).second->bind_thread.defined();
    }
    return bound;
  };

  auto& state = *p_state;
  // Fill p_state with leaf itervars
  for (const IterVar& iv : stage->leaf_iter_vars) {
    state[iv] = bound_to_thread(iv);
  }
  // Traverse the graph bottom-up to propagate thread binding information
  for (size_t i = stage->relations.size(); i != 0; --i) {
    IterVarRelation rel = stage->relations[i - 1];
    if (const SplitNode* s = rel.as<SplitNode>()) {
      state[s->parent] = state[s->inner] || state[s->outer];
    } else if (const FuseNode* s = rel.as<FuseNode>()) {
      state[s->inner] = state[s->fused];
      state[s->outer] = state[s->fused];
    } else if (const RebaseNode* s = rel.as<RebaseNode>()) {
      state[s->parent] = state[s->rebased];
    } else if (rel.as<SingletonNode>()) {
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

void PassDownDomain(const Stage& stage, std::unordered_map<IterVar, Range>* p_state,
                    arith::Analyzer* actx, bool allow_missing) {
  auto ceil_div = [actx](const PrimExpr& a, const PrimExpr& b) {
    if (actx->CanProve(indexmod(a, b) == 0)) {
      return actx->Simplify(indexdiv(a, b));
    }
    return actx->Simplify(indexdiv(a + (b - 1), b));
  };

  auto minimum_or_later = [actx](const PrimExpr& a, const PrimExpr& b) {
    if (actx->CanProve(a < b)) {
      return actx->Simplify(a);
    }
    return actx->Simplify(b);
  };

  std::unordered_map<IterVar, bool> dominating_thread;
  PassUpThreadBinding(stage, &dominating_thread);

  auto& state = *p_state;
  // forwar iteration on relations
  for (IterVarRelation rel : stage->relations) {
    if (const SplitNode* r = rel.as<SplitNode>()) {
      if (!state.count(r->parent)) {
        ICHECK(allow_missing);
        continue;
      }
      ICHECK(!state.count(r->inner));
      const Range& range_parent = state.at(r->parent);
      // Tighten iv's extent to min(parent_extent, factor_or_nparts), only if all of the
      // following conditions are met:
      // 1. No leaf IterVar derived from iv binds to any thread.  People may use split
      // to force an IterVar extent to match the number of allocated threads to fuse stages
      // that require different number of threads.  We don't want to change these extents.
      // 2. allow_missing is false, i.e. that PassDownDomain is called by the final InferBound,
      // rather than by an early compiler phase, such as rfactor().  We don't want to tighten an
      // IterVar in an early phase allowing missing IterVars, because it may bind to a thread later.
      // 3. range_parent's extent is not 0.  At lest one Topi test has a case where a tensor has one
      // zero-sized dimension.  Split creates iv with a positive extent to avoid zero-extent
      // IterVar.  We don't touch it.
      auto resolve_min_extent_for_split = [&](const IterVar& iv, const PrimExpr& factor_or_nparts) {

        // <bojian/DietCode>
        // if (enable_verbose_logging) {
        //   LOG(INFO) << "range_parent->extent=" << range_parent->extent << " -> "
        //             << actx->Simplify(range_parent->extent);
        // }


        return dominating_thread[iv] || allow_missing || is_zero(range_parent->extent)
                   ? factor_or_nparts
                   : minimum_or_later(range_parent->extent, factor_or_nparts);
      };
      if (r->factor.defined()) {
        Update(p_state, r->inner,
               Range::FromMinExtent(0, resolve_min_extent_for_split(r->inner, r->factor)), actx);
        Update(p_state, r->outer,
               Range::FromMinExtent(0, ceil_div(range_parent->extent, r->factor)), actx);
      } else {
        Update(p_state, r->outer,
               Range::FromMinExtent(0, resolve_min_extent_for_split(r->outer, r->nparts)), actx);
        Update(p_state, r->inner,
               Range::FromMinExtent(0, ceil_div(range_parent->extent, r->nparts)), actx);
      }
    } else if (const FuseNode* r = rel.as<FuseNode>()) {
      if (!state.count(r->outer) || !state.count(r->inner)) {
        ICHECK(allow_missing);
        continue;
      }
      const Range& range_outer = state.at(r->outer);
      const Range& range_inner = state.at(r->inner);
      state[r->fused] = Range::FromMinExtent(0, range_outer->extent * range_inner->extent);
    } else if (const RebaseNode* r = rel.as<RebaseNode>()) {
      if (!state.count(r->parent)) {
        ICHECK(allow_missing);
        continue;
      }
      Update(p_state, r->rebased, Range::FromMinExtent(0, state.at(r->parent)->extent), actx);
    } else if (const SingletonNode* s = rel.as<SingletonNode>()) {
      Update(p_state, s->iter, Range::FromMinExtent(0, 1), actx);
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
  // update the extents of binded threads.
  for (auto kv : stage->iter_var_attrs) {
    if (kv.second->bind_thread.defined()) {
      ICHECK(state.count(kv.first));
      Update(p_state, kv.second->bind_thread, state.at(kv.first), actx);
    }
  }
}

void PassUpIndex(const Stage& stage, const Map<IterVar, Range>& dom_map,
                 std::unordered_map<IterVar, PrimExpr>* p_state, bool allow_missing) {
  auto& state = *p_state;
  for (size_t i = stage->relations.size(); i != 0; --i) {
    IterVarRelation rel = stage->relations[i - 1];
    if (const SplitNode* s = rel.as<SplitNode>()) {
      if (!state.count(s->outer) || !state.count(s->inner)) {
        ICHECK(allow_missing);
        continue;
      }
      PrimExpr outer = state.at(s->outer);
      PrimExpr inner = state.at(s->inner);
      PrimExpr factor = dom_map.at(s->inner)->extent;
      PrimExpr parent_min = dom_map.at(s->parent)->min;
      state[s->parent] = inner + outer * factor;
      // add min if they exist
      if (!is_zero(parent_min)) {
        state[s->parent] = state[s->parent] + parent_min;
      }
    } else if (const FuseNode* s = rel.as<FuseNode>()) {
      if (!state.count(s->fused)) {
        ICHECK(allow_missing);
        continue;
      }
      PrimExpr value = state.at(s->fused);
      PrimExpr factor = dom_map.at(s->inner)->extent;
      PrimExpr outer_min = dom_map.at(s->outer)->min;
      PrimExpr inner_min = dom_map.at(s->inner)->min;
      state[s->outer] = indexdiv(value, factor);
      state[s->inner] = indexmod(value, factor);
      // add min if they exist
      if (!is_zero(outer_min)) {
        state[s->outer] = state[s->outer] + outer_min;
      }
      if (!is_zero(inner_min)) {
        state[s->inner] = state[s->inner] + inner_min;
      }
      // s->fused, s->outer and s->inner may be of different dtype,
      // so we cast the `state` back to its original dtype
      state[s->outer] = cast(s->outer->var.dtype(), state[s->outer]);
      state[s->inner] = cast(s->inner->var.dtype(), state[s->inner]);
    } else if (const RebaseNode* s = rel.as<RebaseNode>()) {
      if (!state.count(s->rebased)) {
        ICHECK(allow_missing);
        continue;
      }
      PrimExpr value = state.at(s->rebased);
      PrimExpr parent_min = dom_map.at(s->parent)->min;
      // add min if they exist
      if (!is_zero(parent_min)) {
        state[s->parent] = value + parent_min;
      } else {
        state[s->parent] = value;
      }
    } else if (rel.as<SingletonNode>()) {
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

void PassDownIndex(const Stage& stage, const Map<IterVar, Range>& dom_map,
                   std::unordered_map<IterVar, PrimExpr>* p_state, bool allow_missing) {
  auto& state = *p_state;
  for (IterVarRelation rel : stage->relations) {
    if (const SplitNode* s = rel.as<SplitNode>()) {
      if (!state.count(s->parent)) {
        ICHECK(allow_missing);
        continue;
      }
      Range r = dom_map.at(s->inner);
      ICHECK(is_zero(r->min));
      PrimExpr parent = state.at(s->parent);
      PrimExpr factor = r->extent;
      state[s->outer] = indexdiv(parent, factor);
      state[s->inner] = indexmod(parent, factor);
    } else if (const FuseNode* s = rel.as<FuseNode>()) {
      if (!state.count(s->inner) && !state.count(s->outer)) {
        ICHECK(allow_missing);
        continue;
      }
      PrimExpr factor = dom_map.at(s->inner)->extent;
      PrimExpr outer_min = dom_map.at(s->outer)->min;
      PrimExpr inner_min = dom_map.at(s->inner)->min;
      PrimExpr inner = state.at(s->inner);
      PrimExpr outer = state.at(s->outer);
      ICHECK(is_zero(outer_min));
      ICHECK(is_zero(inner_min));
      state[s->fused] = outer * factor + inner;
    } else if (const RebaseNode* s = rel.as<RebaseNode>()) {
      if (!state.count(s->rebased)) {
        ICHECK(allow_missing);
        continue;
      }
      PrimExpr value = state.at(s->parent);
      PrimExpr parent_min = dom_map.at(s->parent)->min;
      ICHECK(is_zero(parent_min));
      state[s->rebased] = value;
    } else if (const SingletonNode* s = rel.as<SingletonNode>()) {
      state[s->iter] = make_zero(s->iter->var.dtype());
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

// Domain message passing.
void PassUpDomain(const SplitNode* s, const std::unordered_map<IterVar, Range>& dom_map,
                  const IntSet& outer, const IntSet& inner, IntSet* parent) {
  if (dom_map.count(s->outer) && dom_map.count(s->inner) && dom_map.count(s->parent) &&
      outer.MatchRange(dom_map.at(s->outer)) && inner.MatchRange(dom_map.at(s->inner))) {
    *parent = IntSet::FromRange(dom_map.at(s->parent));
    return;
  }
  PrimExpr factor = dom_map.at(s->inner)->extent;
  PrimExpr parent_min = dom_map.at(s->parent)->min;
  ICHECK(outer.defined());
  ICHECK(inner.defined());
  ICHECK(factor.defined());
  *parent = arith::EvalSet(s->outer->var * factor + s->inner->var + parent_min,
                           {{s->outer, outer}, {s->inner, inner}});
}

void PassUpDomain(const FuseNode* s, const std::unordered_map<IterVar, Range>& dom_map,
                  const IntSet& fused, IntSet* outer, IntSet* inner) {
  ICHECK(dom_map.count(s->outer));
  ICHECK(dom_map.count(s->inner));
  ICHECK(dom_map.count(s->fused));
  arith::Analyzer ana;

  if (fused.MatchRange(dom_map.at(s->fused))) {
    *outer = IntSet::FromRange(dom_map.at(s->outer));
    *inner = IntSet::FromRange(dom_map.at(s->inner));
    return;
  }
  PrimExpr outer_min = dom_map.at(s->outer)->min;
  PrimExpr inner_min = dom_map.at(s->inner)->min;

  if (fused.IsSinglePoint()) {
    PrimExpr value = fused.PointValue();
    PrimExpr factor = dom_map.at(s->inner)->extent;
    PrimExpr v_outer = indexdiv(value, factor);
    PrimExpr v_inner = indexmod(value, factor);
    if (!is_zero(outer_min)) v_outer = v_outer + outer_min;
    if (!is_zero(inner_min)) v_inner = v_inner + inner_min;
    *outer = IntSet::SinglePoint(v_outer);
    *inner = IntSet::SinglePoint(v_inner);
  } else {
    PrimExpr fused_extent = (fused.max() - fused.min() + 1);
    PrimExpr inner_extent = dom_map.at(s->inner)->extent;
    *outer = IntSet::Interval(outer_min + indexdiv(fused.min(), inner_extent),
                              outer_min + indexdiv(fused.max(), inner_extent));
    if (is_zero(ana.Simplify(indexmod(inner_extent, fused_extent))) &&
        is_zero(ana.Simplify(indexmod(fused.min(), fused_extent)))) {
      // fused never spans multiple rows, make a tight bounding box
      // there may be other cases when bounding box could be tightened
      *inner = IntSet::Interval(inner_min + indexmod(fused.min(), inner_extent),
                                inner_min + indexmod(fused.max(), inner_extent));
    } else {  // fused may span multiple rows, use full row widths
      if (!is_zero(ana.Simplify(indexmod(fused_extent, inner_extent))) ||
          !is_zero(ana.Simplify(indexmod(fused.min(), inner_extent)))) {
        LOG(WARNING)
            << "fused and original axes are not aligned, this may cause redundant computations";
      }
      *inner = IntSet::FromRange(dom_map.at(s->inner));
    }
    return;
  }
}

void PassUpDomain(const RebaseNode* s, const std::unordered_map<IterVar, Range>& dom_map,
                  const IntSet& rebased, IntSet* parent) {
  ICHECK(dom_map.count(s->parent));
  if (rebased.MatchRange(dom_map.at(s->rebased))) {
    *parent = IntSet::FromRange(dom_map.at(s->parent));
    return;
  }
  PrimExpr parent_min = dom_map.at(s->parent)->min;
  *parent = arith::EvalSet(s->rebased->var + parent_min, {{s->rebased, rebased}});
}

void PassUpDomain(const Stage& stage, const std::unordered_map<IterVar, Range>& dom_map,
                  std::unordered_map<IterVar, IntSet>* p_state) {
  auto& state = *p_state;
  for (size_t i = stage->relations.size(); i != 0; --i) {
    IterVarRelation rel = stage->relations[i - 1];
    if (const SplitNode* r = rel.as<SplitNode>()) {
      IntSet parent;
      PassUpDomain(r, dom_map, state.at(r->outer), state.at(r->inner), &parent);
      state[r->parent] = parent;
    } else if (const FuseNode* r = rel.as<FuseNode>()) {
      IntSet outer, inner;
      PassUpDomain(r, dom_map, state.at(r->fused), &outer, &inner);
      state[r->outer] = outer;
      state[r->inner] = inner;
    } else if (const RebaseNode* r = rel.as<RebaseNode>()) {
      IntSet parent;
      PassUpDomain(r, dom_map, state.at(r->rebased), &parent);
      state[r->parent] = parent;
    } else if (rel.as<SingletonNode>()) {
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

// Pass up bit mask with or relation.
void PassUpBitMaskOr(const Stage& stage, std::unordered_map<IterVar, int>* p_state,
                     bool allow_missing) {
  auto& state = *p_state;
  for (size_t i = stage->relations.size(); i != 0; --i) {
    IterVarRelation rel = stage->relations[i - 1];
    if (const SplitNode* s = rel.as<SplitNode>()) {
      if (!state.count(s->inner) && !state.count(s->outer)) {
        ICHECK(allow_missing);
        continue;
      }
      int res = 0;
      if (state.count(s->parent)) res |= state[s->parent];
      if (state.count(s->inner)) res |= state[s->inner];
      if (state.count(s->outer)) res |= state[s->outer];
      state[s->parent] = res;
    } else if (const FuseNode* s = rel.as<FuseNode>()) {
      if (!state.count(s->fused)) {
        ICHECK(allow_missing);
        continue;
      }
      if (!state.count(s->outer)) {
        state[s->outer] = state[s->fused];
      } else {
        state[s->outer] |= state[s->fused];
      }
      if (!state.count(s->inner)) {
        state[s->inner] = state[s->fused];
      } else {
        state[s->inner] |= state[s->fused];
      }
    } else if (const RebaseNode* s = rel.as<RebaseNode>()) {
      if (!state.count(s->rebased)) {
        ICHECK(allow_missing);
        continue;
      }
      if (!state.count(s->parent)) {
        state[s->parent] = state[s->rebased];
      } else {
        state[s->parent] |= state[s->rebased];
      }
    } else if (rel.as<SingletonNode>()) {
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

void PassDownBitMaskOr(const Stage& stage, std::unordered_map<IterVar, int>* p_state,
                       bool allow_missing) {
  auto& state = *p_state;
  for (IterVarRelation rel : stage->relations) {
    if (const SplitNode* s = rel.as<SplitNode>()) {
      if (!state.count(s->parent)) {
        ICHECK(allow_missing);
        continue;
      }
      if (!state.count(s->outer)) {
        state[s->outer] = state.at(s->parent);
      } else {
        state[s->outer] |= state.at(s->parent);
      }
      if (!state.count(s->inner)) {
        state[s->inner] = state.at(s->parent);
      } else {
        state[s->inner] |= state.at(s->parent);
      }
    } else if (const FuseNode* s = rel.as<FuseNode>()) {
      if (!state.count(s->outer) && !state.count(s->inner)) {
        ICHECK(allow_missing);
        continue;
      }
      int res = 0;
      if (state.count(s->outer)) res |= state.at(s->outer);
      if (state.count(s->inner)) res |= state.at(s->inner);
      if (state.count(s->fused)) res |= state.at(s->fused);
      state[s->fused] = res;
    } else if (const RebaseNode* s = rel.as<RebaseNode>()) {
      if (!state.count(s->parent)) {
        ICHECK(allow_missing);
        continue;
      }
      if (!state.count(s->rebased)) {
        state[s->rebased] = state.at(s->parent);
      } else {
        state[s->rebased] |= state.at(s->parent);
      }
    } else if (const SingletonNode* s = rel.as<SingletonNode>()) {
      state[s->iter] = 0;
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

/*!
 * \brief message passing to find if boundary checking on IterVar is needed.
 * \param s The stage to be used.
 * \param p_state The message passing state
 *     IterVar->flag
 */
void PassUpBoundCheck(const Stage& s, const Map<IterVar, Range>& dom_map,
                      std::unordered_map<IterVar, bool>* p_state, arith::Analyzer* analyzer) {
  auto& state = *p_state;

  // <bojian/DietCode>
  // const Stage& stage = s;

  for (size_t i = s->relations.size(); i != 0; --i) {
    IterVarRelation rel = s->relations[i - 1];
    if (const SplitNode* s = rel.as<SplitNode>()) {
      bool outer = state.at(s->outer);
      bool inner = state.at(s->inner);

      if (dom_map.count(s->inner) && dom_map.count(s->outer)) {
        PrimExpr factor = dom_map.at(s->inner)->extent;
        PrimExpr step = dom_map.at(s->outer)->extent;
        if (outer || inner) {
          state[s->parent] = true;

          // <bojian/DietCode>
          // if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
          //   LOG(INFO) << "Bound checking is required for " << stage;
          // }

        } else {
          if (analyzer->CanProve(dom_map.at(s->parent)->extent == factor * step)) {
            state[s->parent] = false;

            // <bojian/DietCode>
            // LOG(INFO) << stage << ": " << dom_map.at(s->parent)->extent
            //           << "==" << factor * step;

          } else {
            state[s->parent] = true;

            // <bojian/DietCode>
            // if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
            //   LOG(INFO) << "Bound checking is required for " << stage;
            // }

          }
        }
      } else {
        state[s->parent] = true;

        // <bojian/DietCode>
        // if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
        //   LOG(INFO) << "Bound checking is required for " << stage;
        // }

      }
    } else if (const FuseNode* s = rel.as<FuseNode>()) {
      bool fused = state.at(s->fused);
      state[s->outer] = fused;
      state[s->inner] = fused;
    } else if (const RebaseNode* s = rel.as<RebaseNode>()) {
      state[s->parent] = state.at(s->rebased);
    } else if (rel.as<SingletonNode>()) {
      // nop
    } else {
      LOG(FATAL) << "unknown relation type";
    }
  }
}

bool IsRangeSame(const Range input_1, const Range input_2) {
  arith::Analyzer analyzer;
  if (input_1.same_as(input_2)) return true;

  return (analyzer.CanProve(input_1->min == input_2->min) &&
          analyzer.CanProve(input_1->extent == input_2->extent));
}


class ContainsBlockIdx : public ExprVisitor {
 public:
  bool hasBlockIdx = false;
 protected:
  void VisitExpr_(const VarNode* op) override {
    if (op->name_hint == "blockIdx.x") {
      hasBlockIdx = true;
    }
  }
};


// <bojian/DietCode>
bool enable_verbose_logging_in_msg_passing = false;


std::vector<PrimExpr> MakeBoundCheck(const Stage& stage, const Map<IterVar, Range>& dom_map,
                                     const std::unordered_map<IterVar, PrimExpr>& value_map,
                                     bool skip_ivar_domain,
                                     const std::unordered_set<IterVar>& skip_iter) {
  // <bojian/DietCode>
  if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
    LOG(INFO) << "Making bound check for " << stage;
  }

  arith::Analyzer analyzer;

  std::unordered_map<IterVar, bool> bound_state;
  for (IterVar iv : stage->leaf_iter_vars) {
    bound_state[iv] = false;
  }
  PassUpBoundCheck(stage, dom_map, &bound_state, &analyzer);

  std::vector<PrimExpr> preds;
  Map<Var, IntSet> iset_dmap;

  // setup domain map for set analysis
  for (const auto& kv : dom_map) {
    iset_dmap.Set(kv.first->var, IntSet::FromRange(kv.second));
  }

  for (auto entry : dom_map) {
    analyzer.Bind(entry.first->var, entry.second);
  }

  // <bojian/DietCode>
  // if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
  //   LOG(INFO) <<  "all_iter_vars=" << stage->all_iter_vars << ", "
  //             << "root_iter_vars=" << stage->op->root_iter_vars();
  // }
  Array<tir::DynShapeVar> shape_vars = stage->shape_vars;
  Array<Array<IntImm>> wkl_insts = stage->wkl_insts;
  // LOG(INFO) << "!!! stage.name=" << stage->op->name;
  // if (stage->op->name == "W.shared") {
  //   enable_verbose_logging_in_msg_passing = true;
  // } else {
  //   enable_verbose_logging_in_msg_passing = false;
  // }
  // if (!shape_vars.empty() && !wkl_insts.empty()) {
  //   LOG(INFO) << "shape_vars=" << shape_vars << " captured with "
  //                "wkl_insts=" << wkl_insts;
  // } else {
  //   LOG(WARNING) << "Don't see anything"; // ", "
  //                   // "stack trace=" << dmlc::StackTrace(1);
  // }
  std::unordered_set<IterVar> ivs_w_pred;

  for (const IterVar& iv : stage->all_iter_vars) {
    if (skip_iter.count(iv) || iv->iter_type == kOpaque) continue;
    if (bound_state.at(iv)) {
      Range dom = dom_map.at(iv);
      PrimExpr value = value_map.at(iv) - dom->min;
      PrimExpr vmax = analyzer.int_set(value, iset_dmap).max();

      // <bojian/DietCode>
      // LOG(INFO) << "vmax=" << vmax << " of value=" << value;
      bool can_ignore_bound_check;
      if (wkl_insts.empty()) {
        can_ignore_bound_check =
            analyzer.CanProve(vmax < dom->extent);
            // canProveForAllDynamicAxes(analyzer, vmax < dom->extent);
      } else {
        can_ignore_bound_check =
            canProveLTForAllWklInsts(value, dom->extent, dom_map, shape_vars, wkl_insts);
      }


      if (vmax.dtype() != value.dtype() || !can_ignore_bound_check) {
        if (dmlc::GetEnv("DIETCODE_SCHED_OPT", 0)) {

          // LOG(INFO) << "Inserting predicate for iv=" << iv;

          bool bound_check_already_covered = false;
          for (const IterVar& iv_w_pred : ivs_w_pred) {
            if (iv->var->name_hint == iv_w_pred->var->name_hint + ".outer" ||
                iv->var->name_hint == iv_w_pred->var->name_hint + ".outer.outer") {
              bound_check_already_covered = true;
            }
          }
          if (bound_check_already_covered) {
            continue;
          }
        }

        if (enable_verbose_logging_in_msg_passing) {
          LOG(INFO) << "Inserting predicate=" << (value < dom->extent) << " with "
                       "dom->min=" << dom->min;
          LOG(INFO) << "iv=" << iv;
        }

        preds.emplace_back(value < dom->extent);

        // <bojian/DietCode>
        ivs_w_pred.insert(iv);
        if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
          LOG(INFO) << "Inserting predicate (" << preds.back()
                    << ") for " << stage;
        }

      }
    }
  }
  for (const IterVar& iv : stage->op->root_iter_vars()) {
    if (skip_iter.count(iv) || iv->iter_type == kOpaque) continue;
    Range dom = dom_map.at(iv);
    ICHECK(iv->dom.defined());
    if (!skip_ivar_domain && !IsRangeSame(iv->dom, dom)) {
      PrimExpr value = value_map.at(iv) - iv->dom->min;
      IntSet s = analyzer.int_set(value, iset_dmap);
      PrimExpr vmin = s.min();
      PrimExpr vmax = s.max();
      // The range of `value` resides in [vmin, vmax]

      // <bojian/DietCode> Ignored the lower bound check.
      // bool can_ignore_lower_bound_check;
      // // can_ignore_lower_bound_check = analyzer.CanProve(vmin >= 0);
      // if (wkl_insts.empty()) {
      //   can_ignore_lower_bound_check = analyzer.CanProve(vmin >= 0);
      // } else {
      //   can_ignore_lower_bound_check =
      //       canProveGEForAllWklInsts(value, Integer(0), dom_map, shape_vars, wkl_insts);
      // }
      // if (vmin.dtype() != value.dtype() || !can_ignore_lower_bound_check) {
      //   preds.emplace_back(value >= 0);
      // }

      bool can_ignore_upper_bound_check;
      if (wkl_insts.empty()) {
        can_ignore_upper_bound_check =
            analyzer.CanProve(vmax < iv->dom->extent);
            // canProveForAllDynamicAxes(analyzer, vmax < iv->dom->extent);
      } else {
        can_ignore_upper_bound_check =
            canProveLTForAllWklInsts(value, iv->dom->extent, dom_map, shape_vars, wkl_insts);
      }

      if (vmax.dtype() != value.dtype() || !can_ignore_upper_bound_check) {

        // <bojian/DietCode>
        if (dmlc::GetEnv("DIETCODE_SCHED_OPT", 0)) {
          // ContainsBlockIdx blockIdx_checker;
          // blockIdx_checker(value);
          if (stage->origin_op->name.find(".local") != std::string::npos &&
              // blockIdx_checker.hasBlockIdx
              std::string(iv->var->name_hint).find(".c") != std::string::npos
              ) {
            if (!dmlc::GetEnv("DIETCODE_SCHED_OPT_NO_LOCAL_PADDING", 0)) {
              // LOG(WARNING) << "\'.local\' spotted in " << stage->origin_op->name << ". "
              //                 "Assuming it is a local compute operation whose boundary check "
              //                 "(" << value << "<" << iv->dom->extent << ") can be neglected.";
              continue;
            } // else {
              // LOG(WARNING) << "Local padding has been disabled";
            // }
          }
          // if (dmlc::GetEnv("DIETCODE_SCHED_OPT", 0) == 2) {
          //   if (stage->origin_op->name.find(".shared") != std::string::npos) {
          //     if (!dmlc::GetEnv("DIETCODE_SCHED_OPT_NO_LOCAL_PADDING", 0)) {
          //       LOG(WARNING) << "\'.local\' spotted in " << stage->origin_op->name << ". "
          //                       "Assuming it is a local compute operation whose boundary check "
          //                       "(" << value << "<" << iv->dom->extent << ") can be neglected.";
          //       continue;
          //     }
          //   }
          // }  // if (dmlc::GetEnv("DIETCODE_SCHED_OPT", 0) == 2)
        }    // if (dmlc::GetEnv("DIETCODE_SCHED_OPT", 0))

        if (enable_verbose_logging_in_msg_passing) {
          LOG(INFO) << "Inserting predicate=" << (value < iv->dom->extent) << " with "
                       "dom->min=" << iv->dom->min;
          LOG(INFO) << "iv=" << iv;
        }

        preds.emplace_back(value < iv->dom->extent);

        // <bojian/DietCode>
        if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
          LOG(INFO) << "Inserting predicate (" << preds.back()
                    << ") for " << stage;
        }

      }
    }
  }

  // <bojian/DietCode>
  if (dmlc::GetEnv("DIETCODE_DEBUG_TRACE", 0)) {
    LOG(INFO) << "Inserting predicates=" << ArrayToString(preds);
  }

  return preds;
}
}  // namespace te
}  // namespace tvm
