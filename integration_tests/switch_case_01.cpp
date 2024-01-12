#include <iostream>

int switch_case_with_no_fall_through(int x) {
    int value;
    switch(x) {
        case 1: {
            value = 2*x;
            break;
        }
        case 2: {
            value = 3*x;
            break;
        }
        case 3: {
            value = 4*x;
            break;
        }
        case 4: {
            value = 5*x;
            break;
        }
        default: {
            value = 6*x;
        }
    }
    return value;
}


int main() {
    int value;
    value = switch_case_with_no_fall_through(1);
    std::cout<<value<<std::endl;
    if( value != 2 ) {
        exit(2);
    }

    value = switch_case_with_no_fall_through(2);
    std::cout<<value<<std::endl;
    if( value != 6 ) {
        exit(2);
    }

    value = switch_case_with_no_fall_through(3);
    std::cout<<value<<std::endl;
    if( value != 12 ) {
        exit(2);
    }

    value = switch_case_with_no_fall_through(4);
    std::cout<<value<<std::endl;
    if( value != 20 ) {
        exit(2);
    }

    value = switch_case_with_no_fall_through(7);
    std::cout<<value<<std::endl;
    if( value != 42 ) {
        exit(2);
    }

    return 0;
}
