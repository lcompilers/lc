#include <iostream>

enum Color {
    RED = 1,
    GREEN = 2,
    BLUE = 3
};

void test_color_enum() {
    std::cout << RED << " " << GREEN << " " << BLUE << std::endl;
    if( RED != 1 ) {
        exit(2);
    }
    if( GREEN != 2 ) {
        exit(2);
    }
    if( BLUE != 3 ) {
        exit(2);
    }
}

void test_selected_color(enum Color selected_color) {
    enum Color color;
    color = selected_color;
    if( color != RED ) {
        exit(2);
    }
    std::cout << color << std::endl;
}

int main() {

test_color_enum();
test_selected_color(RED);

return 0;

}
