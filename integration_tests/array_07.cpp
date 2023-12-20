#include <iostream>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

xt::xtensor_fixed<double, xt::xshape<3, 3>> matmul(
    xt::xtensor_fixed<double, xt::xshape<3, 2>>& a,
    xt::xtensor_fixed<double, xt::xshape<2, 3>>& b) {
    xt::xtensor_fixed<double, xt::xshape<3, 3>> ab;
    for( int i = 0; i < a.shape(0); i++ ) {
        for( int j = 0; j < b.shape(1); j++ ) {
            ab(i, j) = 0;
            for( int k = 0; k < a.shape(1); k++ ) {
                ab(i, j) += a(i, k) * b(k, j);
            }
        }
    }
    return ab;
}

int main() {

    xt::xtensor_fixed<double, xt::xshape<3, 2>> a;
    xt::xtensor_fixed<double, xt::xshape<2, 3>> b;
    xt::xtensor_fixed<double, xt::xshape<3, 3>> c;

    for( int i = 0; i < a.shape(0); i++ ) {
        for( int j = 0; j < a.shape(1); j++ ) {
            a(i, j) = i * a.shape(0) * j;
        }
    }

    for( int i = 0; i < b.shape(0); i++ ) {
        for( int j = 0; j < b.shape(1); j++ ) {
            b(i, j) = i * b.shape(0) * j;
        }
    }

    std::cout << a << b << std::endl;
    c = matmul(a, b);
    std::cout << c << std::endl;

    return 0;
}
