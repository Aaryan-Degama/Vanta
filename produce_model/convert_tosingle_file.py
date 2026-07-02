import onnx

"""
This script loads the exported ONNX models and saves them back to disk as single,
self-contained files. By default, ONNX might split large models into a separate .onnx 
and .data file, which is harder to bundle into mobile apps.
"""

# Load the models into memory (ONNX will automatically find any associated .data files)
image_model = onnx.load(".onnx_model/clip_image_encoder.onnx")
text_model = onnx.load(".onnx_model/clip_text_encoder.onnx")

# Save the models back out, forcing all weights to be bundled internally
onnx.save_model(image_model, ".onnx_model/clip_image.onnx", save_as_external_data=False)
onnx.save_model(text_model, ".onnx_model/clip_text.onnx", save_as_external_data=False)
