#include "CLIP_model.hpp"


bool CLIP_session::load(){
    if( loaded ) {
        std::cout<< "Model already loaded.";
        return true;
    }
    std::string model_path = ".onnx_model/clip_image_fp16.onnx";

    try{
        std::cout << "Loading CLIP Model ...\n";
        vision_session = std::make_unique<Ort::Session>(env, model_path.c_str(),session_options);
        
        std::cout << "Model successfully loaded into memory!\n";
        loaded = true;

        return true;
    }catch(const std::exception& e){
        std::cerr << "Error loading ONNX model: " << e.what() << std::endl;

        return false;
    }
}



std::vector<float> CLIP_session::get_embedding(const CLIP_instance& img) {
    if(!loaded){     
        throw std::runtime_error("Model not Loaded, can not create embedding. Load the model first. try <your_model>.load()");
    }
    cv::Mat image = img.get_img();

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
        embedding[i] = static_cast<float>(fp16[i]);
    }
    
    float norm = 0.0f;

    for(float x : embedding)
        norm += x * x;

    norm = std::sqrt(norm);

    for(float& x : embedding)
        x /= norm;
    
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