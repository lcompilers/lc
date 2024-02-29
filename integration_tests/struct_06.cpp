#include <iostream>

struct A_t {
    float y;
    int32_t x;
};

struct B_t {
    int32_t z;
    struct A_t a;
};

void init_A_t(A_t& a, float y_, int32_t x_) {
    a.y = y_;
    a.x = x_;
}

void init_B_t(B_t& b, int32_t z_, struct A_t a_) {
    b.z = z_;
    b.a = a_;
}

void assert(bool condition) {
    if( !condition ) {
        exit(2);
    }
}

void f(const B_t& b) {
    std::cout << b.z << " " << b.a.x << " " << b.a.y << std::endl;
    assert( b.z == 1 );
    assert( b.a.x == 2 );
    assert( double(b.a.y) == 3.0 );
}

void g() {
    struct A_t a1;
    struct A_t a2;
    struct B_t b;
    init_A_t(a1, float(1.0), 1);
    init_A_t(a2, float(2.0), 2);
    init_B_t(b, 1, a1);
    b.a = a2;
    b.z = 1;
    b.a.x = 2;
    b.a.y = float(3.0);
    assert( a1.x == 1 );
    assert( double(a1.y) == 1.0 );
    assert( a2.x == 2 );
    assert( double(a2.y) == 2.0 );
    f(b);
}

int main() {

g();

return 0;

}
