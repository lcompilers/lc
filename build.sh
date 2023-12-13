#!/usr/bin/env bash

set -ex

python src/libasr/asdl_cpp.py src/libasr/ASR.asdl src/libasr/asr.h
python src/libasr/wasm_instructions_visitor.py

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    -DCMAKE_INSTALL_PREFIX=$CONDA_PREFIX \
    .
cmake --build . -j16 --target install
