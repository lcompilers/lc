#include <iostream>

enum MolecularMass {
    water = 18,
    methane = 16,
    ammonia = 17,
    oxygen = 16
};

enum RelativeCharge {
    proton = 1,
    electron = -1,
    neutron = 0
};

void test_mm() {
    std::cout << methane << " " << oxygen << std::endl;
    if( methane != oxygen ) {
        exit(2);
    }

    std::cout << ammonia - methane << std::endl;
    if( ammonia - methane != 1 ) {
        exit(2);
    }

    std::cout << water << " " << oxygen + 2 << std::endl;
    if( water != oxygen + 2 ) {
        exit(2);
    }
}

void test_rc() {
    std::cout << proton << " " << electron << " " << neutron << std::endl;
    if( proton + electron != neutron ) {
        exit(2);
    }
}

int main() {

test_mm();
test_rc();

return 0;

}
