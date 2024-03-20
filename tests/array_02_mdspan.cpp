#include <vector>
#include <mdspan/mdspan.hpp>

struct A {
    std::vector<double> arr1_data;
};

int main1() {

    std::vector<double> arr2_data = {1.0, 2.0, 3.0, 2.0, 5.0, 7.0, 2.0, 5.0, 7.0};
    std::vector<double> arr3_data = {1.0, 2.0, 3.0, 2.0, 5.0, 7.0, 2.0, 5.0, 7.0};

    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 2>> arr1{arr2_data.data(), 3, 3};

    arr3_data[0] = 4.0;
    arr2_data[0] = 4.0;

    return 0;
}

int main() {

    struct A aobj;
    aobj.arr1_data = {1.0, 2.0, 3.0, 2.0, 5.0, 7.0, 2.0, 5.0, 7.0};

    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 2>> arr1{aobj.arr1_data.data(), 3, 3};

    aobj.arr1_data[0] = 4.0;

    return 0;
}
