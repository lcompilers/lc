#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

void verify1d(const xt::xtensor<double, 1>& array,
    const xt::xtensor<double, 1>& result,
    const int size) {
    int i;
    double eps;

    eps = 1e-12;

    for( i = 0; i < size; i++ ) {
        if (abs(sin(sin(array(i))) - result(i)) > eps) {
            exit(2);
        }
    }
}

void verifynd(const xt::xtensor<double, 3>& array,
    const xt::xtensor<double, 3>& result,
    const int size1, const int size2, const int size3) {
    int i, j, k;
    double eps;

    eps = 1e-12;

    for( i = 0; i < size1; i++ ) {
        for( j = 0; j < size2; j++ ) {
            for( k = 0; k < size3; k++ ) {
                if (abs(pow(sin(array(i, j, k)), 2.0) - result(i, j, k)) > eps) {
                    exit(2);
                }
            }
        }
    }
}

void verify2d(const xt::xtensor<double, 2>& array,
    const xt::xtensor<double, 2>& result,
    const int size1, const int size2) {
    int i, j;
    double eps;
    eps = 1e-12;

    for( i = 0; i < size1; i++ ) {
        for( j = 0; j < size2; j++ ) {
            if (abs(pow(cos(array(i, j)), 2.0) - result(i, j)) > eps) {
                exit(2);
            }
        }
    }
}

void elemental_sin() {
    int i, j, k;
    xt::xtensor<double, 1> array1d = xt::empty<double>({256});
    xt::xtensor<double, 1> sin1d = xt::empty<double>({256});
    xt::xtensor<double, 3> arraynd = xt::empty<double>({256, 64, 16});
    xt::xtensor<double, 3> sinnd = xt::empty<double>({256, 64, 16});

    for( i = 0; i < 256; i++ ) {
        array1d(i) = i + 1;
    }

    sin1d = sin(sin(array1d));

    verify1d(array1d, sin1d, 256);

    for( i = 0; i < 256; i++ ) {
        for( j = 0; j < 64; j++ ) {
            for( k = 0; k < 16; k++ ) {
                arraynd(i, j, k) = i + j + k + 3;
            }
        }
    }

    sinnd = xt::pow(sin(arraynd), 2.0);

    verifynd(arraynd, sinnd, 256, 64, 16);
}

void elemental_cos() {
    int i, j;
    xt::xtensor<double, 2> array2d = xt::empty<double>({256, 64});
    xt::xtensor<double, 2> cos2d = xt::empty<double>({256, 64});

    for( i = 0; i < 256; i++ ) {
        for( j = 0; j < 64; j++ ) {
            array2d(i, j) = i + j;
        }
    }

    cos2d = xt::pow(cos(array2d), 2.0);

    verify2d(array2d, cos2d, 256, 64);
}

void elemental_trig_identity() {
    int i, j, k, l;
    double eps;
    xt::xtensor<double, 4> arraynd = xt::empty<double>({64, 32, 8, 4});
    xt::xtensor<double, 4> observed = xt::empty<double>({64, 32, 8, 4});
    eps = 1e-12;

    for( i = 0; i < 64; i++ ) {
        for( j = 0; j < 32; j++ ) {
            for( k = 0; k < 8; k++ ) {
                for( l = 0; l < 4; l++ ) {
                    arraynd(i, j, k, l) = i + j + k + l;
                }
            }
        }
    }

    observed = xt::pow(sin(arraynd), 2.0) + xt::pow(cos(arraynd), 2.0);

    for( i = 0; i < 64; i++ ) {
        for( j = 0; j < 32; j++ ) {
            for( k = 0; k < 8; k++ ) {
                for( l = 0; l < 4; l++ ) {
                    if( abs(observed(i, j, k, l) - 1.0) > eps ) {
                        exit(2);
                    }
                }
            }
        }
    }
}

int main() {

elemental_sin();
elemental_cos();
elemental_trig_identity();

}
