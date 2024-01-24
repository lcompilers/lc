#include <iostream>
#include <complex>

using namespace std::complex_literals;

// module derived_types_01_m_01
struct X {
    float r;
    int i;
};

struct Y {
    std::complex<double> c;
    struct X d;
};

void set(struct X& a) {
    a.i = 1;
    a.r = 1.5;
}
// module derived_types_01_m_01

// module derived_types_01_m_02
struct Z {
    std::complex<double> k;
    struct Y l;
};
// module derived_types_01_m_02

int main() {

    struct X b;
    struct Z c;

    b.i = 5;
    b.r = 3.5;
    std::cout << b.i << " " << b.r << std::endl;
    if( b.i != 5 ) {
        exit(2);
    }
    if( b.r != 3.5 ) {
        exit(2);
    }

    set(b);
    std::cout << b.i << " " << b.r << std::endl;
    if( b.i != 1 ) {
        exit(2);
    }
    if( b.r != 1.5 ) {
        exit(2);
    }

    c.l.d.r = 2.0;
    c.l.d.i = 2;
    c.k = 2.0 + 2i;
    std::cout << c.l.d.r << " " << c.l.d.i << " " << c.k << std::endl;
    if( c.l.d.r != 2.0 ) {
        exit(2);
    }
    if( c.l.d.i != 2 ) {
        exit(2);
    }
    if( c.k != 2.0 + 2i ) {
        exit(2);
    }

    set(c.l.d);
    std::cout << c.l.d.r << " " << c.l.d.i << " " << c.k << std::endl;
    if( c.l.d.r != 1.5 ) {
        exit(2);
    }
    if( c.l.d.i != 1.0 ) {
        exit(2);
    }
    if( c.k != 2.0 + 2i ) {
        exit(2);
    }

    return 0;
}
