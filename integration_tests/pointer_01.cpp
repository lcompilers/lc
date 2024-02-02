#include <iostream>
#include <complex>

using namespace std::complex_literals;

#define check(p, t, v) if( *p != v ) { \
        exit(2); \
    } \
    if( t != v ) { \
        exit(2); \
    } \

int main() {

    int* p1;
    double* p2;
    int t1 = 2;
    double t2 = 2.0;
    std::complex<double>* p3;
    std::complex<double> t3 = 2.0 + 1i*3.0;

    p1 = &t1;
    p2 = &t2;
    p3 = &t3;
    *p1 = 1;
    *p2 = 4.0;

    std::cout << *p1 << std::endl;
    std::cout << t1 << std::endl;
    check(p1, t1, 1)

    t1 = *p2 + *p1;

    std::cout << *p1 << std::endl;
    std::cout << t1 << std::endl;
    check(p1, t1, 5);

    t1 = 8;

    std::cout << *p1 << std::endl;
    std::cout << t1 << std::endl;
    check(p1, t1, 8);

    *p3 = 2.0 * (*p3);
    std::cout << *p3 << std::endl;
    std::cout << t3 << std::endl;
    check(p3, t3, 4.0 + 1i*6.0);

}
