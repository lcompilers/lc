#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

xt::xtensor<double, 1> solution();
void compare_solutions(const xt::xtensor<double, 1>& y);

xt::xtensor<double, 1> solution() {
    xt::xtensor<double, 1> x = xt::empty<double>({2});
    x = {1.0, 2.0};
    return x;
}

void compare_solutions(const xt::xtensor<double, 1>& x) {
    xt::xtensor<double, 1> diff = xt::empty<double>({x.size()});

    diff = solution() - x;

    std::cout << diff << std::endl;

    if (std::abs(diff(0) - 0.0) > 1e-6) {
        exit(2);
    }
    if (std::abs(diff(0) - 0.0) > 1e-6) {
        exit(2);
    }

    diff = x - solution();

    std::cout << diff << std::endl;

    if (diff(0) != 0.0) {
        exit(2);
    }
    if (diff(1) != 0.0) {
        exit(2);
    }
}

int main() {

    xt::xtensor<double, 1> z = xt::empty<double>({2});

    std::cout << solution() << std::endl;
    z = solution();
    std::cout << z << std::endl;
    compare_solutions(z);

}
