#include <iostream>
#include "struct_03_m_01.h"

int main() {
    struct t x;
    x = new_value();
    std::cout << x.i << std::endl;
    if (x.i != 123) {
        exit(2);
    }
}
