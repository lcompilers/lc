#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

int main() {

xt::xtensor<int, 2> array_const_1 = {{-14, 3, 0, -2}, {19, 1, 20, 21}};
xt::xtensor_fixed<int, xt::xshape<2>> i23_shape = {4, 2};
std::cout<< array_const_1 << "\n";
array_const_1.reshape(i23_shape);
std::cout << array_const_1 << "\n";
std::cout << array_const_1(0, 0) << " " << array_const_1(0, 1) << "\n";
std::cout << array_const_1(1, 0) << " " << array_const_1(1, 1) << "\n";
std::cout << array_const_1(2, 0) << " " << array_const_1(2, 1) << "\n";
std::cout << array_const_1(3, 0) << " " << array_const_1(3, 1) << "\n";
if( array_const_1(0, 0) != -14 ) {
    exit(2);
}
if( array_const_1(0, 1) != 3 ) {
    exit(2);
}
if( array_const_1(1, 0) != 0 ) {
    exit(2);
}
if( array_const_1(1, 1) != -2 ) {
    exit(2);
}
if( array_const_1(2, 0) != 19 ) {
    exit(2);
}
if( array_const_1(2, 1) != 1 ) {
    exit(2);
}
if( array_const_1(3, 0) != 20 ) {
    exit(2);
}
if( array_const_1(3, 1) != 21 ) {
    exit(2);
}

return 0;

}
