#include <iostream>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

double reduce_array(xt::xtensor_fixed<double, xt::xshape<3, 3>>& arr) {
    double sum = 0.0;
    for( int i = 0; i < arr.shape(0); i++ ) {
        for( int j = 0; j < arr.shape(1); j++ ) {
            sum += arr(i, j);
        }
    }
    return sum;
}

int main() {

    xt::xtensor_fixed<double, xt::xshape<3, 3>> arr1;
    double result;

    for( int i = 0; i < 3; i++ ) {
        for( int j = 0; j < 3; j++ ) {
            arr1(i, j) = i*3 + j;
        }
    }

    std::cout << arr1 << std::endl;
    result = reduce_array(arr1);
    std::cout << result << std::endl;

    return 0;
}
