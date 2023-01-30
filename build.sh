#!/usr/bin/env bash

set -ex

cd libasr
python src/libasr/asdl_cpp.py src/libasr/ASR.asdl src/libasr/asr.h
python src/libasr/wasm_instructions_visitor.py
cd ..


cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    .
cmake --build . -j16
