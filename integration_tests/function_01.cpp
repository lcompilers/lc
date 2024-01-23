#include <iostream>

float f_real(const float);

float f(const float a) {
    float b, x;
    x = 2;
    b = a + f_real(0.0);
    return b;
}

float f_real(const float a) {
    float b;
    if( a == 0.0 ) {
        b = 2.0;
    } else {
        b = a + f(1.0);
    }
    return b;
}

int main() {

    float x = 5, y;
    float p = 5, q;
    float a, b, c;
    y = f(x);
    std::cout << y << std::endl;
    if( y != 7.0 ) {
        exit(2);
    }

    q = f_real(p);
    std::cout << q << std::endl;
    if( q != 8.0 ) {
        exit(2);
    }

}
