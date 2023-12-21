#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

int main() {

    xt::xtensor<int, 1> g;
    xt::xtensor<int, 2> y;
    xt::xtensor_fixed<int, xt::xshape<20>> b;
    int i;

    y = xt::empty<int>({20, 20});
    g = xt::empty<int>({20});

    b.fill(2);
    y.fill(4);
    g.fill(8);

    std::cout << b << y << g << std::endl;

    for( i = 0 ; i < 20; i++ ) {
        xt::view(y, i) = xt::view(g, xt::all()) * xt::view(y, i) + xt::view(b, xt::all());
    }

    std::cout<< y << std::endl;
    if( xt::any(xt::not_equal(y, 34)) ) {
        exit(2);
    }

    return 0;

}
