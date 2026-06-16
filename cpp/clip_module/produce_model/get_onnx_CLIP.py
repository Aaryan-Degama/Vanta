import torch
import clip

# Load the standard PyTorch CLIP model
model, preprocess = clip.load("ViT-B/32", device="cpu")
model.eval()

# ---------------------------------------------------------
# A. Export the Image Encoder (Visual)
# ---------------------------------------------------------
dummy_image = torch.randn(1, 3, 224, 224)

torch.onnx.export(
    model.visual,
    dummy_image,
    "../.onnx_model/clip_image_encoder.onnx",
    export_params=True,
    opset_version=18,  # Updated to 18 to support LayerNorm properly
    do_constant_folding=True,
    input_names=["IMAGE"],
    output_names=["IMAGE_EMBEDDING"],
    dynamic_axes={
        "IMAGE": {0: "batch_size"}, 
        "IMAGE_EMBEDDING": {0: "batch_size"}
    }
)

# ---------------------------------------------------------
# B. Export the Text Encoder
# ---------------------------------------------------------
class TextEncoderWrapper(torch.nn.Module):
    def __init__(self, clip_model):
        super().__init__()
        self.model = clip_model
        
    def forward(self, text):
        return self.model.encode_text(text)

text_model = TextEncoderWrapper(model)
text_model.eval()  # Set wrapper to eval mode to prevent training-mode export warnings

dummy_text = clip.tokenize(["search query"])

torch.onnx.export(
    text_model,
    dummy_text,
    "../.onnx_model/clip_text_encoder.onnx",
    export_params=True,
    opset_version=18,  # Updated to 18
    do_constant_folding=True,
    input_names=["TEXT"],
    output_names=["TEXT_EMBEDDING"],
    dynamic_axes={
        "TEXT": {0: "batch_size"}, 
        "TEXT_EMBEDDING": {0: "batch_size"}
    }
)