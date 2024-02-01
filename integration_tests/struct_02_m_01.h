#ifndef INTEGRATION_TESTS_STRUCT_02_M_01_H
#define INTEGRATION_TESTS_STRUCT_02_M_01_H

#include <complex>

struct X {
    float r;
    int i;
};

struct Y {
    std::complex<double> c;
    struct X d;
};

void set(struct X& a) {
    a.i = 1;
    a.r = 1.5;
}

#endif
