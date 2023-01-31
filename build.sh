#!/usr/bin/env bash

set -ex

python libasr/src/libasr/asdl_cpp.py libasr/src/libasr/ASR.asdl libasr/src/libasr/asr.h
python libasr/src/libasr/wasm_instructions_visitor.py

mkdir build
cd build

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    ..
cmake --build . -j16
