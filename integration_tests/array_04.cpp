#include <iostream>
#include <xtensor/xfixed.hpp>
#include "xtensor/xtensor.hpp"
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

int main() {

    xt::xtensor<double, 2> arr1 {
      {1.0, 2.0, 3.0},
      {2.0, 5.0, 7.0},
      {2.0, 5.0, 7.0}};

    xt::xtensor<double, 1> arr2 {5.0, 6.0, 7.0};
    xt::xtensor<double, 1> res;
    xt::xtensor_fixed<double, xt::xshape<3>> res_ = {7.0, 11.0, 14.0};

    res = xt::empty<double>({3});
    res = xt::view(arr1, 1) + arr2;
    std::cout << arr1 << "\n";
    std::cout << arr2 << "\n";
    std::cout << res << "\n";

    if( xt::any(xt::abs(res - res_) > 1e-8) ) {
        exit(2);
    }

    return 0;
}
