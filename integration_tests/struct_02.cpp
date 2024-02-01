#include <iostream>
#include "struct_02_m_01.h"
#include "struct_02_m_02.h"

using namespace std::complex_literals;

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
