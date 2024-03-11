#include <iostream>

#define assert(cond) if( !(cond) ) { \
    exit(2); \
} \

void test_loop_01() {
    int32_t i = 0;
    int32_t j = 0;

    while( false ) {
        assert( false );
    }

    while( i < 0 ) {
        assert( false );
    }

    while( i < 10 ) {
        i += 1;
    }
    std::cout<<i<<std::endl;
    assert( i == 10 );

    while( i < 20 ) {
        while( i < 15 ) {
            i += 1;
        }
        i += 1;
    }
    std::cout<<i<<std::endl;
    assert( i == 20 );

    for( i = 0; i < 5; i++ ) {
        std::cout<<i<<" "<<j<<std::endl;
        assert( i == j );
        j += 1;
    }
}

void test_loop_02() {
    int32_t i = 0;
    int32_t j = 0;

    j = 0;
    for( i = 10; i > 0; i-- ) {
        j = j + i;
    }
    std::cout<<j<<std::endl;
    assert( j == 55 );

    for( i = 0; i < 5; i++ ) {
        if( i == 3 ) {
            break;
        }
    }
    std::cout<<i<<std::endl;
    assert( i == 3 );

    j = 0;
    for( i = 0; i < 5; i++ ) {
        if( i == 3 ) {
            continue;
        }
        j += 1;
    }
    std::cout<<j<<std::endl;
    assert( j == 4 );
}

void test_loop_03() {
    int32_t i = 0;
    int32_t j = 0;
    int32_t k = 0;
    while( i < 10 ) {
        j = 0;
        while( j < 10 ) {
            k += 1;
            if( i == 0 ) {
                if( j == 3 ) {
                    continue;
                } else {
                    j += 1;
                }
            }
            j += 1;
        }
        i += 1;
    }
    std::cout<<k<<std::endl;
    assert( k == 95 );

    i = 0; j = 0; k = 0;
    while( i < 10 ) {
        j = 0;
        while( j < 10 ) {
            k += i + j;
            if( i == 5 ) {
                if( j == 4 ) {
                    break;
                } else {
                    j += 1;
                }
            }
            j += 1;
        }
        i += 1;
    }
    std::cout<<k<<std::endl;
    assert( k == 826 );

    i = 0;
    if( i == 0 ) {
        while( i < 10 ) {
            j = 0;
            if( j == 0 ) {
                while( j < 10 ) {
                    k += 1;
                    if( i == 9 ) {
                        break;
                    }
                    j += 1;
                }
                i += 1;
            }
        }
    }
    std::cout<<k<<std::endl;
    assert( k == 917 );
}

void verify() {
    test_loop_01();
    test_loop_02();
    test_loop_03();
}

int main() {

verify();

}
