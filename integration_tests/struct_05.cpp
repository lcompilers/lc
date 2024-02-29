#include <iostream>

struct A_t {
    float y;
    int32_t x;

    constexpr A_t(float y_, int32_t x_): y(y_), x(x_) {}
};

void assert(bool condition) {
    if( !condition ) {
        exit(2);
    }
}

void f(const A_t& a) {
    std::cout << a.x << std::endl;
    std::cout << a.y << std::endl;
}

void change_struct(A_t& a) {
    a.x = a.x + 1;
    a.y = a.y + float(1);
}

void g() {
    A_t x = A_t(float(3.25), 3);
    f(x);
    assert( x.x == 3 );
    assert( double(x.y) == 3.25 );

    x.x = 5;
    x.y = float(5.5);
    f(x);
    assert( x.x == 5 );
    assert( double(x.y) == 5.5 );
    change_struct(x);
    assert( x.x == 6 );
    assert( double(x.y) == 6.5 );
}

int main() {

g();

return 0;

}
