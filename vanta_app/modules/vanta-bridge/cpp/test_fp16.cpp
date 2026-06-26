#include <iostream>
#include <onnxruntime_cxx_api.h>

int main() {
    Ort::Float16_t f;
    f.value = 15360; // 1.0 in fp16
    float val = static_cast<float>(f.value); // wait, if I cast f to float, does it compile?
    // float val2 = static_cast<float>(f); // wait!
    std::cout << val << std::endl;
}
