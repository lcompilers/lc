#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

// LFortran: ./integration_tests/arrays_35.f90

int main() {
    xt::xtensor<float, 1> x = xt::empty<float>({2});
    x = { 9.0, 2.1 };

    std::cout << x << std::endl;
    if (std::abs(x(0) - 9.0) > 1e-7) exit(2);
    if (std::abs(x(1) - 2.1) > 1e-7) exit(2);
}
