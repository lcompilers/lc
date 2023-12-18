#include <stdio.h>

void subrout(int x) {
    printf("%s %d\n", "x is", x);
}

int func(int x) {
    return x + 2;
}

int main() {
    int p = 26;
    subrout(p);
    p = func(p);
    printf("%s %d\n", "p is", p);
    return 0;
}
