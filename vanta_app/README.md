# For the CLIP embedding to be generated we need the following 

## 1. Text embedding needs 

```bash
# Loads the vocab.json file from official clip-vit-base-patch32 model from HuggingFace
curl -L -o vanta_app/android/app/src/main/assets/VantaModels/vocab.json https://huggingface.co/openai/clip-vit-base-patch32/resolve/main/vocab.json 

# Loads the merges.txt file from official clip-vit-base-patch32 model from HuggingFace
curl -L -o vanta_app/android/app/src/main/assets/VantaModels/merges.txt https://huggingface.co/openai/clip-vit-base-patch32/resolve/main/merges.txt
``` 

## 2. We also need to get the Quantised model of CLIP to the application

```bash
# Get to the produce_model directory
cd produce_model 
```

### First get the libraries downloaded from requirements.txt

```bash
pip install -r requirements.txt
```

### Next run the python files to get the quantized model and save it in the assets folder 

```bash
mkdir .onnx_model

# To get the models in f32  format
python3 get_onnx_CLIP.py

# To get it in one single file
python3 convert_tosingle_file.py

# To Quantise the model to f16 format
python3 quantize.py
```

## 3. Now we need to copy the models to the android app
We have the onnx models in `produce_model/.onnx_model` directory

```bash
cd .onnx_model

# copy these files to the android app
mv clip_image_fp16.onnx clip_text_fp16.onnx ../../vanta_app/android/app/src/main/assets/VantaModels/clip_image_fp16.onnx

cd ../..

```

## 4. Lets do the same for buffalo_sc 
The model exists in the `vanta_app/modules/vanta-bridge/cpp/Include/Preprocessing/Segregation/.model/buffalo_sc`

```bash
# Navigate to the buffalo_sc model directory
cd vanta_app/modules/vanta-bridge/cpp/Include/Preprocessing/Segregation/.model/buffalo_sc

# Copy the two buffalo_sc ONNX model files to the android app assets
cp det_500m.onnx w600k_mbf.onnx ../../../../../../../../android/app/src/main/assets/VantaModels/

# Return to the original directory
cd ../../../../../

```