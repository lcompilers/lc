#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

void copy_from_to(const xt::xtensor_fixed<int, xt::xshape<10>>& xa,
    xt::xtensor_fixed<int, xt::xshape<10>>&xb) {
    int i;
    for( i = 0; i < xa.size(); i++ ) {
        xb(i) = xa(i);
    }
}

bool verify(const xt::xtensor_fixed<int, xt::xshape<10>>& a,
   const xt::xtensor_fixed<int, xt::xshape<10>>& b) {
    int i;
    bool r = true;
    for( i = 0; i < a.size(); i++ ) {
        r = r && (a(i) == b(i));
    }

    return r;
}

int main() {

    xt::xtensor_fixed<int, xt::xshape<10>> x, y;
    int i;
    bool r;

    for( i = 0; i < x.size(); i++ ) {
        x(i) = i;
    }

    copy_from_to(x, y);
    std::cout<< x << std::endl;
    std::cout << y << std::endl;
    r = verify(x, y);
    std::cout << r << std::endl;
    if (!r) {
        exit(2);
    }

    return 0;

}
