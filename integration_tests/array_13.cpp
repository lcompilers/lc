#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

xt::xtensor<double, 1> R3(const xt::xtensor<double, 1>& x) {
    xt::xtensor<double, 1> R3_ = xt::empty<double>({x.size()});

    double a, b;
    const double c = 2.0;
    a = x(0);
    b = x(1);
    R3_.fill(std::pow(a, 2.0) + std::pow(b, 2.0) - std::pow(c, 2.0));

    return R3_;
}

int main() {

    xt::xtensor_fixed<double, xt::xshape<2>> x = {0.5, 0.5};
    std::cout << R3(x) << std::endl;
    if( xt::any(xt::not_equal(R3(x), -3.5)) ) {
        exit(2);
    }

    return 0;

}
