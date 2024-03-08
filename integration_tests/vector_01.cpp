#include <vector>
#include <iostream>

std::vector<int32_t> fill_list_i32(int32_t size) {
    std::vector<int32_t> aarg = {0, 1, 2, 3, 4};
    int32_t i;
    for( i = 0; i < size; i++) {
        aarg.push_back(i + 5);
    }
    return aarg;
}

#define assert(cond) if( !(cond) ) { \
    exit(2); \
} \

void test_list_01() {
    std::vector<int32_t> a = {};
    std::vector<double> f = {1.0, 2.0, 3.0, 4.0, 5.0};
    int32_t i;

    a = fill_list_i32(10);

    for( i = 0; i < 10; i++ ) {
        f.push_back(float(i + 6));
    }

    for( i = 0; i < 15; i++ ) {
        std::cout << f[i] << std::endl;
    }

    for( i = 0; i < 15; i++ ) {
        assert( (f[i] - double(a[i])) == 1.0 );
    }

    for( i = 0; i < 15; i++ ) {
        f[i] = f[i] + double(i);
    }

    for( i = 0; i < 15; i++ ) {
        assert( (f[i] - double(a[i])) == (double(i) + 1.0) );
    }
}


void tests() {
    test_list_01();
}

int main() {
    tests();
}
