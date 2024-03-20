#include <vector>
#include <mdspan/mdspan.hpp>

int main() {

    std::vector<double> arr1_data = {1.0, 2.0, 3.0, 2.0, 5.0, 7.0, 2.0, 5.0, 7.0};

    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 2>> arr1{arr1_data.data(), 3, 3};

    arr1_data[0] = 4.0;

    return 0;
}
