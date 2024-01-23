#include <iostream>
#include <complex>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

using namespace std::complex_literals;

void check(const xt::xtensor<std::complex<double>, 3>& c, const int op_code) {

int i, j, k;
double left, right;

std::cout << c << std::endl;

for( i = 0; i < c.shape(0); i++ ) {
    for( j = 0; j < c.shape(1); j++ ) {
        for( k = 0; k < c.shape(2); k++ ) {
            left = i + j + k + 3;
            right = (i + 1)*(j + 1)*(k + 1);
            switch( op_code ) {
                case 0: {
                    if(c(i, j, k) != left + right*1.0i) {
                        exit(2);
                    }
                    break;
                }
                case 1: {
                    if(c(i, j, k) != -left  - right*1.0i) {
                        exit(2);
                    }
                    break;
                }
            }
        }
    }
}

}

int main() {

xt::xtensor_fixed<int, xt::xshape<2, 2, 1>> a;
xt::xtensor_fixed<int, xt::xshape<2, 2, 1>> b;
xt::xtensor<std::complex<double>, 3> c = xt::empty<std::complex<double>>({2, 2, 1});
int i, j, k;

for( i = 0; i < 2; i++ ) {
    for( j = 0; j < 2; j++ ) {
        for( k = 0; k < 1; k++ ) {
            a(i, j, k) = i + j + k + 3;
            b(i, j, k) = (i + 1)*(j + 1)*(k + 1);
        }
    }
}

std::cout << a << std::endl;
std::cout << b << std::endl;

c = a + 1.0i*b;
check(c, 0);

c = -a + 1.0i*(-b);
check(c, 1);

return 0;

}
