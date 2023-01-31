#!/usr/bin/env bash

set -ex

python libasr/src/libasr/asdl_cpp.py libasr/src/libasr/ASR.asdl libasr/src/libasr/asr.h
python libasr/src/libasr/wasm_instructions_visitor.py
rm -rf libasr/src/libasr/__pycache__/

rm -rf build
mkdir build

mkdir -p build/libasr/src/libasr
mv libasr/src/libasr/asr.h build/libasr/src/libasr
mv libasr/src/libasr/wasm_visitor.h build/libasr/src/libasr

cd build



cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    ..
cmake --build . -j16
