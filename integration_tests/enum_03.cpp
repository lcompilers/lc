#include <iostream>

enum Color {
    RED,
    GREEN,
    BLUE = 7,
    YELLOW,
    WHITE,
    PINK = 15,
    GREY
};

enum Integers {
    a = -5,
    b,
    c,
    d = 0,
    e,
    f = 7,
    g
};

void assert(bool condition) {
    if( !condition ) {
        exit(2);
    }
}

void test_color_enum() {
    std::cout << RED << std::endl;
    std::cout << GREEN << std::endl;
    std::cout << BLUE << std::endl;
    std::cout << YELLOW << std::endl;
    std::cout << WHITE << std::endl;
    std::cout << PINK << std::endl;
    std::cout << GREY << std::endl;

    assert( RED == 0 );
    assert( GREEN == 1 );
    assert( BLUE == 7 );
    assert( YELLOW == 8 );
    assert( WHITE == 9 );
    assert( PINK == 15 );
    assert( GREY == 16 );
}

void test_selected_color(enum Color selected_color) {
    enum Color color;
    color = selected_color;
    assert( color == YELLOW);
    std::cout << color << std::endl;
}

void test_integer(enum Integers integer, int value) {
    assert(integer == value);
}

void test_integers() {
    std::cout << a << std::endl;
    test_integer(a, -5);

    std::cout << b << std::endl;
    test_integer(b, -4);

    std::cout << c << std::endl;
    test_integer(c, -3);

    std::cout << d << std::endl;
    test_integer(d, 0);

    std::cout << e << std::endl;
    test_integer(e, 1);

    std::cout << f << std::endl;
    test_integer(f, 7);

    std::cout << g << std::endl;
    test_integer(g, 8);
}

int main() {

test_color_enum();
test_selected_color(YELLOW);
test_integers();

}
