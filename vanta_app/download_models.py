import urllib.request
import os
import ssl

ssl._create_default_https_context = ssl._create_unverified_context

url_w600k = "https://huggingface.co/Aitrepreneur/insightface/resolve/main/models/buffalo_l/w600k_r50.onnx"
url_det10g = "https://huggingface.co/Aitrepreneur/insightface/resolve/main/models/buffalo_l/det_10g.onnx"

dest_dir = "/home/naukarji/Github/Vanta/vanta_app/android/app/src/main/assets/VantaModels/"

temp_w600k = os.path.join(dest_dir, "w600k_r50_fp32.onnx")
temp_det10g = os.path.join(dest_dir, "det_10g_fp32.onnx")
final_w600k = os.path.join(dest_dir, "w600k_r50.onnx")
final_det10g = os.path.join(dest_dir, "det_10g.onnx")

print("Downloading w600k_r50.onnx (FP32)...")
urllib.request.urlretrieve(url_w600k, temp_w600k)

print("Downloading det_10g.onnx (FP32)...")
urllib.request.urlretrieve(url_det10g, temp_det10g)

print("Renaming downloaded models...")
os.rename(temp_w600k, final_w600k)
os.rename(temp_det10g, final_det10g)

print("Done! FP32 Models are ready. Please rebuild the app.")
