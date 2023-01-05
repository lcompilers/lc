#!/usr/bin/env bash

set -ex

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_LLVM=yes \
    .
cmake --build . -j16
