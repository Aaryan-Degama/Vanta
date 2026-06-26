#pragma once

#include "CLIP_image.hpp"

#include <vector>
#include <string>
#include <memory>
#include <onnxruntime_cxx_api.h>    // ONNXRuntime
#include <opencv2/core.hpp>

// :: To be removed ::
#include <stdexcept>



class CLIP_session{

    private:
    Ort::Env env{
        ORT_LOGGING_LEVEL_WARNING,
        "CLIP"
    };

    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> vision_session;
    std::unique_ptr<Ort::Session> text_session;

    Ort::MemoryInfo memory_info = 
        Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault
        );

    Ort::AllocatorWithDefaultOptions allocator;
    
    bool loaded = false;

    public:
    bool load();            // To load the model on to memory  
    bool unload();          // To free the memory
    bool is_loaded();

    std::vector<float> get_embedding(const CLIP_instance& img);    //To get the CLIP embedding
    std::vector<float> get_text_embedding(const std::vector<int64_t>& tokens); // To get the text embedding
};
