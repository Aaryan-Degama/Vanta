import onnx
from onnxconverter_common import float16

print("Loading clip_image.onnx.")
image_model = onnx.load(".onnx_model/clip_image.onnx")

print("Converting Image Encoder to FP16.")
image_model_fp16 = float16.convert_float_to_float16(image_model)

onnx.save(image_model_fp16, ".onnx_model/clip_image_fp16.onnx")
print("Success: Saved as clip_image_fp16.onnx!")

print("Loading clip_text.onnx.")
text_model = onnx.load(".onnx_model/clip_text.onnx")

print("Converting Text Encoder to FP16.")
text_model_fp16 = float16.convert_float_to_float16(text_model)

onnx.save(text_model_fp16, ".onnx_model/clip_text_fp16.onnx")
print("Success: Saved as clip_text_fp16.onnx!")