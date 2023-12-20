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

    res = xt::empty<double>({3});
    res = xt::view(arr1, 1) + arr2;
    std::cout<< arr1 << arr2 <<std::endl;
    std::cout << res << std::endl;

    return 0;
}
