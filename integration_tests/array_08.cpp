#include <iostream>
#include <xtensor/xtensor.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

xt::xtensor<double, 2> matmul(
    xt::xtensor<double, 2>& a,
    xt::xtensor<double, 2>& b) {
    xt::xtensor<double, 2> ab = xt::empty<double>({a.shape(0), b.shape(1)});
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

    xt::xtensor<double, 2> a = xt::empty<double>({3, 2});
    xt::xtensor<double, 2> b = xt::empty<double>({2, 3});
    xt::xtensor<double, 2> c = xt::empty<double>({3, 3});
    xt::xtensor<double, 2> ce = {
        {0.0, 0.0, 0.0},
        {0.0, 6.0, 12.0},
        {0.0, 12.0, 24.0}};

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
    for( int i = 0; i < c.shape(0); i++ ) {
        for( int j = 0; j < c.shape(1); j++ ) {
            if( c(i, j) != ce(i, j) ) {
                exit(2);
            }
        }
    }

    return 0;
}
