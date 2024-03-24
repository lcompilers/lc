#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

// LFortran: ./integration_tests/arrays_31.f90

int main() {
    xt::xtensor<int, 1> x = xt::empty<int>({5});

    int pqr = x.size();

    std::cout << pqr << std::endl;
    if (pqr != 5) {
        exit(2);
    }
}
