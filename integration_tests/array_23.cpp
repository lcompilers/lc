#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

void f(xt::xtensor<float, 1>& a, int n,
    xt::xtensor<float, 1>& x, int i) {
    x(i) = i;
    a(i) = 2 * i;
    if (i + 1 < n) {
        f(a, n, x, i + 1);
    }
}

int main() {

xt::xtensor<float, 1> y = xt::empty<float>({20});
xt::xtensor<float, 1> a = xt::empty<float>({20});
int i;

f(a, 20, y, 0);

std::cout << y << std::endl;
std::cout << a << std::endl;

for( int i = 0; i < 20; i++ ) {
    if (2 * y(i) != a(i)) {
        exit(2);
    }
}

}
