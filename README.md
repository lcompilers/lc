# lc

LC is the C frontend to LCompilers.

# Clone lc

    git clone https://github.com/certik/lc.git
    git submodule update --init --recursive

# Build

    mamba env create -f environment_unix.yml
    conda activate lc
    ./build.sh
    ./build/src/lc --ast-dump examples/test.cpp
