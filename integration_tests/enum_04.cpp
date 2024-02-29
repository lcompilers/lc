#include <iostream>
#include <xtensor/xfixed.hpp>

enum Color {
    RED,
    GREEN,
    BLUE,
    PINK,
    WHITE,
    YELLOW
};

struct Truck_t {
    float horsepower;
    int32_t seats;
    double price;
    enum Color color;
};

void init_Truck(Truck_t& truck, float horsepower_, int32_t seats_, double price_, enum Color color_) {
    truck.horsepower = horsepower_;
    truck.seats = seats_;
    truck.price = price_;
    truck.color = color_;
}

void print_Truck(const Truck_t& car) {
    std::cout << car.horsepower << " " << car.seats << " " << car.price << " " << car.color << std::endl;
}

void assert_(bool condition) {
    if( !condition ) {
        exit(2);
    }
}

void test_enum_as_struct_member() {
    xt::xtensor_fixed<Truck_t, xt::xshape<3>> cars;
    init_Truck(cars(0), 700.0, 4, 100000.0, RED);
    init_Truck(cars(1), 800.0, 5, 200000.0, BLUE);
    init_Truck(cars(2), 400.0, 4, 50000.0, WHITE);
    print_Truck(cars(0));
    print_Truck(cars(1));
    print_Truck(cars(2));
    assert_( cars(2).color == WHITE );
    assert_( cars(1).color == BLUE );
    assert_( cars(0).color == RED );
}

int main() {

test_enum_as_struct_member();

return 0;

}
