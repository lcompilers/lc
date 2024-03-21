#include <iostream>
#include <vector>
#include <mdspan/mdspan.hpp>

void print2(Kokkos::mdspan<double, Kokkos::dextents<int32_t, 2>>& arr) {
    for( int32_t i = 0; i < arr.extent(0); i++ ) {
        for( int32_t j = 0; j < arr.extent(1) ; j++ ) {
            std::cout << arr(i, j) << " ";
        }
    }
}

void print1(Kokkos::mdspan<double, Kokkos::dextents<int32_t, 1>>& arr) {
    for( int32_t i = 0; i < arr.size(); i++ ) {
        std::cout << arr[i] << " ";
    }
    std::cout << "\n";
}

int main() {

    std::vector<double> arr1_data = {1.0, 2.0, 3.0, 2.0, 5.0, 7.0, 2.0, 5.0, 7.0};
    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 2>> arr1(arr1_data.data(), 3, 3);

    std::vector<double> arr2_data = {5.0, 6.0, 7.0};
    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 1>> arr2(arr2_data.data(), 3);
    Kokkos::mdspan<double, Kokkos::dextents<int32_t, 1>> res;
    std::vector<double> res_data_correct = {7.0, 11.0, 14.0};

    std::vector<double> res_data; res_data.reserve(3);
    res = Kokkos::mdspan<double, Kokkos::extents<int32_t, 3>>(res_data.data());
    res = Kokkos::submdspan(arr1, 1, Kokkos::full_extent);
    for( int32_t i = 0; i < res.size(); i++ ) {
      res[i] = res[i] + arr2[i];
    }

    print2(arr1);
    print1(arr2);
    print1(res);


    for( int32_t i = 0; i < res.size(); i++ ) {
        if( res[i] != res_data_correct[i] ) {
            exit(2);
        }
    }

    return 0;
}
