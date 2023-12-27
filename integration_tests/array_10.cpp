#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

xt::xtensor<double, 2> softmax(xt::xtensor<double, 2>& x) {
    xt::xtensor<double, 2> y = xt::empty<double>({x.shape(0), x.shape(1)});
    int i;
    double s;

    for( int i = 0; i < x.shape(1); i++ ) {
        xt::view(y, xt::all(), i) = xt::exp(xt::view(x, xt::all(), i) - xt::amax(xt::view(x, xt::all(), i)));
        xt::view(y, xt::all(), i) = xt::view(y, xt::all(), i) / xt::sum(xt::view(y, xt::all(), i));
    }

    return y;
}

int main() {

    xt::xtensor<double, 2> x = xt::empty<double>({10, 10});
    // xt::xtensor<double, 2> sx = xt::empty<double>({10, 10}); // Uncomment, bug in IntrinsicScalarFunctions::Abs
    xt::xtensor_fixed<double, xt::xshape<10, 10>> sx;

    x.fill(2.0);

    sx = softmax(x);
    std::cout<< sx << std::endl;
    if( xt::any(xt::abs(sx - 0.1) > 1e-6) ) {
        exit(2);
    }

}
