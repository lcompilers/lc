#include <iostream>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

int main() {

    xt::xtensor_fixed<double, xt::xshape<3, 3>> arr1;
    xt::xtensor<double, 1> arr2 {5.0, 6.0, 7.0};
    xt::xtensor_fixed<double, xt::xshape<3>> result;

    for( int i = 0; i < 3; i++ ) {
        for( int j = 0; j < 3; j++ ) {
            arr1(i, j) = i*3 + j;
        }
    }

    std::cout << arr1 << arr2 << std::endl;
    result = xt::view(arr1, 1) + arr2;
    std::cout << result << std::endl;

    return 0;
}
