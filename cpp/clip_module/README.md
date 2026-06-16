```bash
# To Download ONNXRUntime ----------
mkdir -p ~/libs
cd ~/libs

wget https://github.com/microsoft/onnxruntime/releases/download/v1.22.0/onnxruntime-linux-x64-1.22.0.tgz

tar -xzf onnxruntime-linux-x64-1.22.0.tgz
# ---------------

# Compilation of project (GCC abhi kkyulki mujhe CMake nai aata tujhe aata to change karna)--------
g++ main.cpp    \
CLIP/CLIP_image.cpp     \
CLIP/CLIP_model.cpp     \
load_files/getfiles.cpp     \
-I$HOME/libs/onnxruntime-linux-x64-1.22.0/include     \
-L$HOME/libs/onnxruntime-linux-x64-1.22.0/lib     \
-lonnxruntime     \
$(pkg-config --cflags --libs opencv4)     \
-std=c++20     \
-o clip_app

```



isko hame tere wo cpp folder me dal dege ya while resolving pr do it yourself