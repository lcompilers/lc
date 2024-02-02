#include <iostream>
#include <cmath>

int main() {

    float myreal, x, y, theta, a;
    x = 0.42;
    y = 0.35;
    myreal = 9.1;
    theta = 1.5;
    a = 0.4;

    {
        #define z -(x*2 + y*2) * cos(theta)
        float *v;
        v = &myreal;
        std::cout << a + z << " " << a - z << " " << *v << std::endl;
        *v = *v * 4.6;
    }

    std::cout << myreal << std::endl;

    if (abs(myreal - 41.86) > 1e-5) {
        exit(2);
    }

    return 0;

}
