#!/usr/bin/env bash

set -ex

python src/libasr/asdl_cpp.py src/libasr/ASR.asdl src/libasr/asr.h
python src/libasr/wasm_instructions_visitor.py
rm -rf src/libasr/__pycache__/

rm -rf build
mkdir build

mkdir -p build/src/libasr
mv src/libasr/asr.h build/src/libasr
mv src/libasr/wasm_visitor.h build/src/libasr

cd build



cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    ..
cmake --build . -j16
