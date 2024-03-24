#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

// LFortran: ./integration_tests/arrays_34.f90


void sub(xt::xtensor<int, 1>& x, int z) {
    switch (z) {
        case 1:
            x.resize({1}); // Resize x to have size 1
            x(0) = 1;
            break;
        default:
            std::cout << "z = " << z << std::endl;
            std::cout << "z not supported." << std::endl;
    }
}

int main() {
    xt::xtensor<int, 1> x;

    sub(x, 1);
    std::cout << x << std::endl;
    if (x(0) != 1) {
        exit(2);
    }

    return 0;
}
