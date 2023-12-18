#include <iostream>
#include "xtensor/xtensor.hpp"
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

int main() {

    xt::xtensor<double, 2> arr1; /* { // TODO: Uncomment this initializer
      {1.0, 2.0, 3.0},
      {2.0, 5.0, 7.0},
      {2.0, 5.0, 7.0}}; */

    xt::xtensor<double, 1> arr2 {5.0, 6.0, 7.0};

    // xt::xarray<double> res = xt::view(arr1, 1) + arr2; // TODO: Uncomment this statement
    std::cout << arr2;

    return 0;
}
