#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"

xt::xtensor<float, 2> copy_array(const xt::xtensor<float, 2>& input);
xt::xtensor<float, 2> twice(const xt::xtensor<float, 2>& x);

xt::xtensor<float, 2> prg(const xt::xtensor<float, 2>& arr) {
    xt::xtensor<float, 2> otpt = xt::xtensor<float, 2>({arr.shape(0), arr.shape(1)});
    otpt = copy_array(twice(copy_array(arr)));
    return otpt;
}

xt::xtensor<float, 2> twice(const xt::xtensor<float, 2>& x) {
    xt::xtensor<float, 2> y = xt::empty<float>({x.shape(0), x.shape(1)});
    y = 2.0*x;
    return y;
}

xt::xtensor<float, 2> copy_array(const xt::xtensor<float, 2>& input) {
    xt::xtensor<float, 2> output = xt::empty<float>({input.shape(0), input.shape(1)});
    output = input;
    return output;
}

int main() {

xt::xtensor<float, 2> array = xt::empty<float>({10, 10});
xt::xtensor<float, 2> output = xt::empty<float>({10, 10});

array.fill(3.0);

output = prg(array);
std::cout << output << std::endl;
if( xt::any(xt::not_equal(output, 6.0)) ) {
    exit(2);
}

}
