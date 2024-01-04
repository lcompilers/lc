#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>

xt::xtensor<double, 1> solution() {
    xt::xtensor<double, 1> x;
    x = {1.0, 1.0};
    return x;
}


void compare_solutions() {
    int i;
    xt::xtensor_fixed<double, xt::xshape<2>> x;

    std::cout << solution().size() << std::endl;

    for( i = 0; i < solution().size(); i++ ) {
        x(i) = i;
    }

    for( i = 0; i < 2; i++ ) {
        std::cout << x(i) << std::endl;
        if( x(i) != i ) {
            exit(2);
        }
    }
}

int main() {

    compare_solutions();

    return 0;

}
