#include <iostream>
#include <vector>
#include <mdspan/mdspan.hpp>

void print(Kokkos::mdspan<double, Kokkos::dextents<int32_t, 1>>& arr) {
    for( int32_t i = 0; i < arr.size(); i++ ) {
        std::cout << arr[i] << "\n";
    }
}

int main() {

    std::vector<double> arr1_data = {1.0, 2.0, 3.0, 2.0, 5.0, 7.0, 2.0, 5.0, 7.0};
    std::vector<double> arr2_data = {5.0, 6.0, 7.0};

    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 2>> arr1{arr1_data.data(), 3, 3};

    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 1>> arr2{arr2_data.data(), 3};

    print( arr2 );

    return 0;
}
