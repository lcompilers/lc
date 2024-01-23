#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

void check(const xt::xtensor<bool, 3>& c, const xt::xtensor<int, 3>& a,
    const xt::xtensor<int, 3>& b, const int op_code) {

int i, j, k;

std::cout << c << std::endl;

for( i = 0; i < a.shape(0); i++ ) {
    for( j = 0; j < a.shape(1); j++ ) {
        for( k = 0; k < a.shape(2); k++ ) {
            switch( op_code ) {
                case 0: {
                    if(c(i, j, k) != (a(i, j, k) == b(i, j, k))) {
                        exit(2);
                    }
                    break;
                }
                case 1: {
                    if(c(i, j, k) != (a(i, j, k) != b(i, j, k))) {
                        exit(2);
                    }
                    break;
                }
                case 2: {
                    if(c(i, j, k) != (a(i, j, k) < b(i, j, k))) {
                        exit(2);
                    }
                    break;
                }
                case 3: {
                    if(c(i, j, k) != (b(i, j, k) <= a(i, j, k))) {
                        exit(2);
                    }
                    break;
                }
                case 4: {
                    if(c(i, j, k) != (b(i, j, k) > a(i, j, k))) {
                        exit(2);
                    }
                    break;
                }
                case 5: {
                    if(c(i, j, k) != (b(i, j, k) >= a(i, j, k))) {
                        exit(2);
                    }
                    break;
                }
                default: {
                    exit(2);
                }
            }
        }
    }
}

}

int main() {

    xt::xtensor<int, 3> a = xt::empty<int>({2, 2, 1});
    xt::xtensor<int, 3> b = xt::empty<int>({2, 2, 1});
    xt::xtensor<bool, 3> c = xt::empty<bool>({2, 2, 1});
    int i, j, k;

    for( i = 0; i < 2; i++ ) {
        for( j = 0; j < 2; j++ ) {
            for( k = 0; k < 1; k++ ) {
                a(i, j, k) = (i + 1)/(j + 1);
                b(i, j, k) = (j + 1)/(i + 1);
            }
        }
    }

    std::cout << a << std::endl;
    std::cout << b << std::endl;

    c = xt::equal(a, b);
    check(c, a, b, 0);

    c = xt::not_equal(a, b);
    check(c, a, b, 1);

    c = (a < b);
    check(c, a, b, 2);

    c = (b <= a);
    check(c, a, b, 3);

    c = (b > a);
    check(c, a, b, 4);

    c = (b >= a);
    check(c, a, b, 5);

}
