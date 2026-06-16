# ONNX Model Exporter & Runtime Setup

This repository contains the scripts necessary to export PyTorch models (like CLIP) into the ONNX format and prepare them for on-device inference using ONNX Runtime.

## Prerequisites

Before getting started, ensure you have Python 3.8+ installed. It is highly recommended to use a virtual environment to avoid dependency conflicts.

```bash
# Optional: Create and activate a virtual environment
python -m venv venv
source venv/bin/activate  # On Windows use `venv\Scripts\activate`

pip install -r requirements.txt
```

## To get the quantized ONNX Model of precision float16

```bash
mkdir ../.onnx_model

# Export the models
python get_onnx_CLIP.py

# Converts the exported model to a single file 
python convert_tosingle_file.py

# Quantize the models for production
python quantize.py


# MACOS/Linux

# Export the models
python3 get_onnx_CLIP.py

# Converts the exported model to a single file 
python3 convert_tosingle_file.py

# Quantize the models for production
python3 quantize.py


```

Now you have 2 files `clip_image_fp16.onnx` and `clip_text_fp16.onnx` these 2 files are the ones that will provide us with the image embeddings for our project

☝️ upar wali file ke alava baki sab not used so del kar lena ya mat karna up to you