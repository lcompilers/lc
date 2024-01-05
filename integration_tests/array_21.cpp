#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

int main() {

    xt::xtensor_fixed<int, xt::xshape<11>> nums_int = {5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5};
    xt::xtensor_fixed<double, xt::xshape<5>> nums_real = {1.5, -3.2, 4.5, 0.9, 7.2};

    if (xt::amin(nums_int)() != -5) {
        exit(2);
    }
    if (xt::amax(nums_int)() != 5) {
        exit(2);
    }
    if (xt::amin(nums_real)() != -3.2) {
        exit(2);
    }
    if (xt::amax(nums_real)() != 7.2) {
        exit(2);
    }

    std::cout << xt::amin(nums_int)() << " " << xt::amax(nums_int)() << std::endl;
    std::cout << xt::amin(nums_real)() << " " << xt::amax(nums_real)() << std::endl;

    return 0;

}
