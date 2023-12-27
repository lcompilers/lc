#include <iostream>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

xt::xtensor<double, 1> func(const xt::xtensor<double, 1>& R, const xt::xtensor<double, 1>& V) {
    xt::xtensor<double, 1> Vmid = xt::empty<double>({R.shape(0) - 1});
    int i;

    for( i = 0; i < Vmid.shape(0); i++ ) {
        Vmid(i) = R(i)*V(i);
    }

    return Vmid;
}

int main() {

    xt::xtensor_fixed<double, xt::xshape<5>> R;
    xt::xtensor_fixed<double, xt::xshape<5>> V;
    xt::xtensor_fixed<double, xt::xshape<3>> Vmid;

    R.fill(45.0);
    V.fill(23.0);

    xt::view(Vmid, xt::range(0, 3)) = func(xt::view(R, xt::range(0, 4)), xt::view(V, xt::range(0, 4)));
    std::cout << Vmid << std::endl;
    if( xt::any(xt::not_equal(Vmid, 1035.00)) ) {
        exit(2);
    }

    return 0;
}
