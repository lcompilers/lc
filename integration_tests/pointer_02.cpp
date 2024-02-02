#include <iostream>

int main() {

    int* p1;
    int t1 = 2, t2 = 1;
    int i;

    std::cout << t1 << t2 << std::endl;

    if (t1 > t2) {
        p1 = &t1;
    } else {
        p1 = &t2;
    }

    std::cout << *p1 << std::endl;

    if (*p1 == t2) {
        exit(2);
    }

    i = *p1;
    if (i == t2) {
        exit(2);
    }

    return 0;
}
