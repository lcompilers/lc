#include <torch/torch.h>
#include <iostream>

void check(const torch::Tensor& tensor=torch::empty({1})) {
    float array[5] = {4.0, 2.0, 2.0, 12.0, 2.0};
    std::cout << tensor << std::endl;
    if( torch::any(torch::abs(tensor - torch::from_blob(array, {5})) > 1e-6).item<bool>() ) {
      exit(2);
    }
}

int main() {
    torch::Tensor tensor = torch::ones(5);
    tensor[0] = 2.0;
    tensor[3] = 6.0;
    tensor = 2 * tensor;
    check(tensor);
    std::cout << tensor << std::endl;
}
