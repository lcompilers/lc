# lc

LC is the C frontend to LCompilers.

# Build

    mamba env create -f environment_unix.yml
    conda activate lc
    ./build.sh
    lc examples/expr2.c
    lc --ast-dump examples/expr2.c
    lc --asr-dump examples/expr2.c
    lc --show-llvm examples/expr2.c
