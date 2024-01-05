#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

double mysum(const int n, const xt::xtensor<double, 1>& A) {
    int i;
    double r = 0;
    for( i = 0; i < A.size(); i++ ) {
        r = r + A(i);
    }
    return r;
}

int main() {

xt::xtensor<double, 2> B = xt::empty<double>({10, 2});
double rescol1, rescol2;

xt::view(B, xt::all(), 0) = 1;
xt::view(B, xt::all(), 1) = 2;

rescol1 = mysum(B.shape(0), xt::view(B, xt::all(), 0));
std::cout << rescol1 << std::endl;
if( rescol1 != 10.0 ) {
    exit(2);
}

rescol2 = mysum(B.shape(1), xt::view(B, xt::all(), 1));
std::cout << rescol2 << std::endl;
if( mysum(B.shape(0), xt::view(B, xt::all(), 1)) != 20.0 ) {
    exit(2);
}

}
