# LC

LC is the C and C++ frontend to LCompilers.

# Build and Run

    mamba env create -f environment_unix.yml
    conda activate lc
    ./build.sh
    lc examples/expr2.c
    lc --ast-dump examples/expr2.c
    lc --asr-dump examples/expr2.c
    lc --show-llvm examples/expr2.c

# Tests

To run tests, do:

    CPATH=$CONDA_PREFIX/include ./run_tests.py
    cd integration_tests
    CPATH=$CONDA_PREFIX/include ./run_tests.py
