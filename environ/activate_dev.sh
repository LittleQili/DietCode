export USE_TVM_BASE=0
export OLD_LD_LIBRARY_PATH=${OLD_LD_LIBRARY_PATH:-${LD_LIBRARY_PATH}}
export LD_LIBRARY_PATH=${OLD_LD_LIBRARY_PATH}:/mnt/tvm/build

export TVM_HOME=/mnt/tvm
export PYTHONPATH=/mnt/tvm/python/build/lib.linux-x86_64-3.8/:/
export PYTHONPATH=$TVM_HOME/python:${PYTHONPATH}
