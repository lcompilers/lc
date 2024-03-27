#!/usr/bin/env bash

set -ex

python src/libasr/asdl_cpp.py src/lc/LC.asdl src/lc/ast.h
python src/libasr/asdl_cpp.py src/libasr/ASR.asdl src/libasr/asr.h
python src/libasr/wasm_instructions_visitor.py
python src/libasr/intrinsic_func_registry_util_gen.py

(cd src/lc/parser && re2c -W -b tokenizer.re -o tokenizer.cpp)
(cd src/lc/parser && bison -Wall -d -r all parser.yy)

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    -DCMAKE_INSTALL_PREFIX=$CONDA_PREFIX \
    .
cmake --build . -j16 --target install
