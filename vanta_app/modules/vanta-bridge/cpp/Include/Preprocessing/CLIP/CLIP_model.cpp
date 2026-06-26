#include "CLIP_model.hpp"
#include <android/log.h>

#define LOG_TAG "VantaEngine"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool CLIP_session::load(){
    if( loaded ) {
        return true;
    }

    std::string model_path = "/data/user/0/com.aaryan_ka.VantaApp/files/VantaModels/clip_image_fp16.onnx";
    std::string text_model_path = "/data/user/0/com.aaryan_ka.VantaApp/files/VantaModels/clip_text_fp16.onnx";

    try{
        // :: To be removed ::
        std::cout << "Loading CLIP Model ...\n";

        vision_session = std::make_unique<Ort::Session>(env, model_path.c_str(),session_options);
        text_session = std::make_unique<Ort::Session>(env, text_model_path.c_str(),session_options);
        
        // :: To be removed ::
        std::cout << "Model successfully loaded into memory!\n";
        
        loaded = true;

        return true;
    }catch(const std::exception& e){
        LOGE("Error loading ONNX model: %s", e.what());
        return false;
    }
}



std::vector<float> CLIP_session::get_embedding(const CLIP_instance& img) {
    if(!loaded){     
        // :: To be removed ::
        throw std::runtime_error("Model not Loaded, can not create embedding. Load the model first. try <your_model>.load()");
    }
    cv::Mat image = img.get_img();

    if (image.empty()) {
        throw std::runtime_error("Empty image matrix - file could not be read or is invalid.");
    }

    std::vector<Ort::Float16_t> fp16_data(      
        1 * 3 * 224 * 224
    );

    float* src = image.ptr<float>();

    for (size_t i = 0; i < fp16_data.size(); ++i) {
        fp16_data[i] = Ort::Float16_t(src[i]);
    }
    
    std::vector<int64_t> input_shape = {
        1,
        3,
        224,
        224
    };

    Ort::Value input_tensor =
        Ort::Value::CreateTensor<Ort::Float16_t>(
            memory_info,
            fp16_data.data(),
            fp16_data.size(),
            input_shape.data(),
            input_shape.size()
        );



    auto input_name =
        vision_session->GetInputNameAllocated(0, allocator);

    auto output_name =
        vision_session->GetOutputNameAllocated(0, allocator);

    const char* input_names[] = {
        input_name.get()
    };

    const char* output_names[] = {
        output_name.get()
    };


    auto output = vision_session->Run(
        Ort::RunOptions{nullptr},
        input_names,
        &input_tensor,
        1,
        output_names,
        1
    );
    const Ort::Float16_t* fp16 =
    output[0].GetTensorData<Ort::Float16_t>();

    auto shape =
    output[0]
        .GetTensorTypeAndShapeInfo()
        .GetShape();

    size_t dim = shape.back();  // 512
    
    std::vector<float> embedding(dim);
    
    for(size_t i = 0; i < dim; ++i) {
        embedding[i] = fp16[i].ToFloat();
    }
    
    float norm = 0.0f;

    for(float x : embedding)
        norm += x * x;

    norm = std::sqrt(norm);

    for(float& x : embedding)
        x /= norm;
    
    return embedding;
}

std::vector<float> CLIP_session::get_text_embedding(const std::vector<int64_t>& tokens) {
    if(!loaded || !text_session) {     
        throw std::runtime_error("Text Model not Loaded");
    }

    std::vector<int64_t> input_shape = {
        1,
        static_cast<int64_t>(tokens.size())
    };

    // Need int32_t copy because the ONNX model expects int32 input
    std::vector<int32_t> tokens_int32(tokens.begin(), tokens.end());

    Ort::Value input_tensor =
        Ort::Value::CreateTensor<int32_t>(
            memory_info,
            tokens_int32.data(),
            tokens_int32.size(),
            input_shape.data(),
            input_shape.size()
        );

    const char* input_names[] = {"TEXT"};
    const char* output_names[] = {"TEXT_EMBEDDING"};

    auto output_tensors = text_session->Run(
        Ort::RunOptions{nullptr},
        input_names,
        &input_tensor,
        1,
        output_names,
        1
    );

    Ort::Value& output_tensor = output_tensors.front();
    const Ort::Float16_t* output_data = output_tensor.GetTensorData<Ort::Float16_t>();
    size_t output_count = output_tensor.GetTensorTypeAndShapeInfo().GetElementCount();

    std::vector<float> embedding(output_count);
    for(size_t i = 0; i < output_count; ++i) {
        embedding[i] = output_data[i].ToFloat();
    }

    // L2-normalize to match image embeddings (required for cosine similarity)
    float norm = 0.0f;
    for(float x : embedding)
        norm += x * x;
    norm = std::sqrt(norm);
    if(norm > 0.0f) {
        for(float& x : embedding)
            x /= norm;
    }

    return embedding;
}



bool CLIP_session::is_loaded() {
    return loaded;
}


bool CLIP_session::unload(){
    if(!loaded)
        return true;

    vision_session.reset();

    loaded = false;

    std::cout << "Model unloaded.\n";

    return true;
}