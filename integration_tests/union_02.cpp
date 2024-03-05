#include <iostream>

struct A_t {
    int32_t ax;
    double ay;
};

void init_A_t(struct A_t& a, int32_t ax_, double ay_) {
    a.ax = ax_;
    a.ay = ay_;
}

struct B_t {
    int64_t bx;
    double by;
};

void init_B_t(struct B_t& b, int64_t bx_, double by_) {
    b.bx = bx_;
    b.by = by_;
}

struct C_t {
    int64_t cx;
    double cy;
    double cz;
};

void init_C_t(struct C_t& c, int64_t cx_, double cy_, double cz_) {
    c.cx = cx_;
    c.cy = cy_;
    c.cz = cz_;
}

union D_t {
    struct A_t a;
    struct B_t b;
    struct C_t c;
};

#define assert(cond) if( !(cond) ) { \
    exit(2); \
} \

void test_struct_union() {
    union D_t d;

    struct A_t aobj;
    struct B_t bobj;
    struct C_t cobj;

    init_A_t(aobj, 0, 1.0);
    init_B_t(bobj, int(2), 7.0);
    init_C_t(cobj, int(5), 13.0, 8.0);

    d.a = aobj;
    std::cout << d.a.ax << " " << d.a.ay << std::endl;
    assert( d.a.ax == 0 );
    assert( abs(d.a.ay - 1.0) <= 1e-12 );

    d.b = bobj;
    std::cout << d.b.bx << " " << d.b.by << std::endl;
    assert( d.b.bx == int(2) );
    assert( abs(d.b.by - 7.0) <= 1e-12 );

    d.c = cobj;
    std::cout << d.c.cx << " " << d.c.cy << " " << d.c.cz << std::endl;
    assert( d.c.cx == 5 );
    assert( abs(d.c.cy - 13.0) <= 1e-12 );
    assert( abs(d.c.cz - 8.0) <= 1e-12 );
}

int main() {

test_struct_union();

}
