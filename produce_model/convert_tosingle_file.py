import onnx

# 1. Load the model (ONNX automatically finds the .data file in the same folder)
image_model = onnx.load(".onnx_model/clip_image_encoder.onnx")
text_model = onnx.load(".onnx_model/clip_text_encoder.onnx")

# 2. Save it back as a single file, explicitly turning off external data
onnx.save_model(image_model, ".onnx_model/clip_image.onnx", save_as_external_data=False)

onnx.save_model(text_model, ".onnx_model/clip_text.onnx", save_as_external_data=False)
