#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

int main() {
    xt::xtensor_fixed<int, xt::xshape<4>> R;
    xt::xtensor_fixed<int, xt::xshape<4>> V;
    xt::xtensor_fixed<int, xt::xshape<4>> U;
    R.fill(23);
    V.fill(9);
    U.fill(1);

    xt::view(R, xt::range(1, 4, 1)) = xt::view(V, xt::range(1, 4)) * 1 * xt::view(U, xt::range(1, 4));

    std::cout<< R << std::endl;
    if (R(0) != 23) {
        exit(2);
    }
    if (R(1) != 9) {
        exit(2);
    }
    if (R(2) != 9) {
        exit(2);
    }
    if (R(3) != 9) {
        exit(2);
    }

    return 0;
}
