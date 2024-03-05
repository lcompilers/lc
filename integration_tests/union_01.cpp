#include <iostream>

union u_type {
    int32_t integer32;
    float real32;
    double real64;
    int64_t integer64;
};

#define assert(cond) if( !(cond) ) { \
    exit(2); \
} \

void test_union() {
    union u_type unionobj;
    unionobj.integer32 = 1;
    std::cout << unionobj.integer32 << std::endl;
    assert( unionobj.integer32 == 1 );

    unionobj.real32 = 2.0;
    std::cout << unionobj.real32 << std::endl;
    assert( abs(unionobj.real32 - 2.0) <= 1e-6 );

    unionobj.real64 = 3.5;
    std::cout << unionobj.real64 << std::endl;
    assert( abs(unionobj.real64 - 3.5) <= 1e-12 );

    unionobj.integer64 = 4;
    std::cout << unionobj.integer64 << std::endl;
    assert( unionobj.integer64 == 4 );
}

int main() {

test_union();

}
