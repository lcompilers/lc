#ifndef INTEGRATION_TESTS_STRUCT_03_M_01_H
#define INTEGRATION_TESTS_STRUCT_03_M_01_H

struct t {
    int i;
};

struct t new_value() {
    struct t p;
    p.i = 123;
    return p;
}

#endif
