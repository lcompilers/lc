#include <iostream>
#include <span>
using namespace std;

int main()
{
    int arr[] = { 1, 2, 3, 4, 5 };

    std::span<int> span_arr(arr);

    for (const auto& num : span_arr) {
        std::cout << num << " ";
    }
    return 0;
}
