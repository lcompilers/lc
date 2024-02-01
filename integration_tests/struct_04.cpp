#include <iostream>
#include "struct_04_m_01.h"

void pass() {
    std::cout << struct_instance_sample.A << struct_instance_sample.B << std::endl;
    if (abs(struct_instance_sample.A - 10.0) > 1.0e-7) {
        exit(2);
    }
    if (abs(struct_instance_sample.B - 20.0) > 1.0e-7) {
        exit(2);
    }
}

int main() {
    struct_instance_sample.A = 10.0;
    struct_instance_sample.B = 20.0;
    pass();
}
