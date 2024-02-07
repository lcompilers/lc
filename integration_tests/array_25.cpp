#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

void fill_array(xt::xtensor<int, 1>& aa);

int main() {
    xt::xtensor<int, 1> a = xt::empty<int>({10});
    xt::xtensor_fixed<int, xt::xshape<10>> ae = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    fill_array(a);
    std::cout << a << std::endl;
    if( xt::any(xt::not_equal(a, ae)) ) {
        exit(2);
    }
}

void fill_array(xt::xtensor<int, 1>& a) {
   int i;
   for( i = 0; i < 10; i++ ) {
      a[i] = i;
   }
}
