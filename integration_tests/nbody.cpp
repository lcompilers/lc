/*
The Computer Language Benchmarks Game
Source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/program/nbody-ifc-6.html

    contributed by Simon Geard, translated from  Mark C. Williams nbody.java
    modified by Brian Taylor
    modified by yuankun shi
    modified by Padraig O Conbhui
*/

#include <iostream>
#include <cmath>
#include <xtensor/xtensor.hpp>
#include <xtensor/xfixed.hpp>
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"

const int nb = 5;
const double PI = 3.141592653589793;
const double SOLAR_MASS = 4 * PI * PI;
const int N = (nb - 1) * nb/2;

void offset_momentum(const int k, xt::xtensor_fixed<double, xt::xshape<3, nb>>& v,
   const xt::xtensor_fixed<double, xt::xshape<nb>>& mass) {

    xt::xtensor_fixed<double, xt::xshape<3>> p;
    int i;

    for( int i = 0; i < 3; i++ ) {
        p(i) = xt::sum(xt::view(v, i, xt::all()) * xt::view(mass, xt::all()))();
    }
    xt::view(v, xt::all(), k) = -p / SOLAR_MASS;
}


void advance(const double tstep,
    xt::xtensor_fixed<double, xt::xshape<3, nb>>& x,
    xt::xtensor_fixed<double, xt::xshape<3, nb>>& v,
    const xt::xtensor_fixed<double, xt::xshape<nb>>& mass) {

    xt::xtensor_fixed<double, xt::xshape<3, N>> r;
    xt::xtensor_fixed<double, xt::xshape<N>> mag;

    double distance, d2;
    int i, j, m;

    m = 0;
    for( i = 0; i < nb; i++ ) {
        for( j = i + 1; j < nb; j++ ) {
            xt::view(r, xt::all(), m) = xt::view(x, xt::all(), i) - xt::view(x, xt::all(), j);
            m = m + 1;
        }
    }

    for( m = 0; m < N; m++ ) {
        d2 = xt::sum(xt::pow(xt::view(r, xt::all(), m), 2))();
        distance = 1.0/sqrt(d2);
        distance = distance * (1.5 - 0.5 * d2 * distance * distance);
        // distance = distance * (1.5 - 0.5 * d2 * distance * distance);
        mag(m) = tstep * pow(distance, 3.0);
    }

    m = 0;
    for( i = 0; i < nb; i++ ) {
        for( j = i + 1; j < nb; j++ ) {
            xt::view(v, xt::all(), i) = (xt::view(v, xt::all(), i) -
                xt::view(r, xt::all(), m) * mass(j) * mag(m));
            xt::view(v, xt::all(), j) = xt::view(v, xt::all(), j) +
                xt::view(r, xt::all(), m) * mass(i) * mag(m);
            m = m + 1;
        }
    }

    x = x + tstep * v;
}


double energy(const xt::xtensor_fixed<double, xt::xshape<3, nb>>& x,
    const xt::xtensor_fixed<double, xt::xshape<3, nb>>& v,
    const xt::xtensor_fixed<double, xt::xshape<nb>>& mass) {
    double energy;

    double distance, tmp;
    xt::xtensor_fixed<double, xt::xshape<3>> d;
    int i, j;

    energy = 0.0;
    for( i = 0; i < nb; i++ ) {
        energy = energy + 0.5 * mass(i) * xt::sum(xt::pow(xt::view(v, xt::all(), i), 2.0))();
        for( j = i + 1; j < nb; j++ ) {
            d = xt::view(x, xt::all(), i) - xt::view(x, xt::all(), j);
            tmp = xt::sum(xt::pow(d, 2.0))();
            distance = sqrt(tmp);
            energy = energy - (mass(i) * mass(j)) / distance;
        }
    }

    return energy;
}

struct body {
    double x, y, z, u, vx, vy, vz, vu, mass;

    body(double x_, double y_, double z_, double u_,
         double vx_, double vy_, double vz_, double vu_,
         double mass_) : x{x_}, y{y_}, z{z_}, u{u_}, vx{vx_},
    vy{vy_}, vz{vz_}, vu{vu_}, mass{mass_} {

    }
};

int main() {

    const double tstep = 0.01;
    const double DAYS_PER_YEAR = 365.24;

    const struct body jupiter = body(
        4.84143144246472090, -1.16032004402742839,
        -1.03622044471123109e-01, 0.0, 1.66007664274403694e-03 * DAYS_PER_YEAR,
        7.69901118419740425e-03 * DAYS_PER_YEAR,
        -6.90460016972063023e-05 * DAYS_PER_YEAR, 0.0,
        9.54791938424326609e-04 * SOLAR_MASS);

    const struct body saturn = body(
        8.34336671824457987, 4.12479856412430479,
        -4.03523417114321381e-01, 0.0,
        -2.76742510726862411e-03 * DAYS_PER_YEAR,
        4.99852801234917238e-03 * DAYS_PER_YEAR,
        2.30417297573763929e-05 * DAYS_PER_YEAR, 0.0,
        2.85885980666130812e-04 * SOLAR_MASS);

    const struct body uranus = body(
        1.28943695621391310e+01, -1.51111514016986312e+01,
        -2.23307578892655734e-01, 0.0,
        2.96460137564761618e-03 * DAYS_PER_YEAR,
        2.37847173959480950e-03 * DAYS_PER_YEAR,
        -2.96589568540237556e-05 * DAYS_PER_YEAR, 0.0,
        4.36624404335156298e-05 * SOLAR_MASS);

    const struct body neptune = body(
        1.53796971148509165e+01, -2.59193146099879641e+01,
        1.79258772950371181e-01, 0.0,
        2.68067772490389322e-03 * DAYS_PER_YEAR,
        1.62824170038242295e-03 * DAYS_PER_YEAR,
        -9.51592254519715870e-05 * DAYS_PER_YEAR, 0.0,
        5.15138902046611451e-05 * SOLAR_MASS);

    const struct body sun = body(0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, SOLAR_MASS);

    xt::xtensor_fixed<double, xt::xshape<nb>> mass = {
        sun.mass, jupiter.mass, saturn.mass,
        uranus.mass, neptune.mass};

    int num, i;
    // std::string argv;

    double e;
    xt::xtensor_fixed<double, xt::xshape<3, nb>> x, v;

    xt::view(x, xt::all(), 0) = xt::xtensor_fixed<double, xt::xshape<3>>({sun.x, sun.y, sun.z});
    xt::view(x, xt::all(), 1) = xt::xtensor_fixed<double, xt::xshape<3>>({jupiter.x, jupiter.y, jupiter.z});
    xt::view(x, xt::all(), 2) = xt::xtensor_fixed<double, xt::xshape<3>>({saturn.x, saturn.y, saturn.z});
    xt::view(x, xt::all(), 3) = xt::xtensor_fixed<double, xt::xshape<3>>({uranus.x, uranus.y, uranus.z});
    xt::view(x, xt::all(), 4) = xt::xtensor_fixed<double, xt::xshape<3>>({neptune.x, neptune.y, neptune.z});

    xt::view(v, xt::all(), 0) = xt::xtensor_fixed<double, xt::xshape<3>>({sun.vx, sun.vy, sun.vz});
    xt::view(v, xt::all(), 1) = xt::xtensor_fixed<double, xt::xshape<3>>({jupiter.vx, jupiter.vy, jupiter.vz});
    xt::view(v, xt::all(), 2) = xt::xtensor_fixed<double, xt::xshape<3>>({saturn.vx, saturn.vy, saturn.vz});
    xt::view(v, xt::all(), 3) = xt::xtensor_fixed<double, xt::xshape<3>>({uranus.vx, uranus.vy, uranus.vz});
    xt::view(v, xt::all(), 4) = xt::xtensor_fixed<double, xt::xshape<3>>({neptune.vx, neptune.vy, neptune.vz});

    // call getarg(1, argv)
    // read (argv,*) num
    num = 1000;

    offset_momentum(0, v, mass);
    e = energy(x, v, mass);
    // workaround
    std::cout << e << std::endl;
    if (abs(e + 0.16907516382852447) > 1e-8) {
        exit(2);
    }

    for( i = 0; i < num; i++ ) {
        advance(tstep, x, v, mass);
    }
    e = energy(x, v, mass);
    // workaround
    std::cout << e << std::endl;
    if (abs(e + 0.16908760523460617) > 1e-8) {
        exit(2);
    }

    return 0;

}
