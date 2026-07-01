#pragma once

#include <vector>
#include <string>
#include <memory>
#include <map>
#include <algorithm>

// :: To be removed ::
#include <stdexcept>
#include <iostream>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/calib3d.hpp>
#include "Config.hpp"

struct FaceResult {
    cv::Rect2f bbox; 
    float confidence; 
    std::vector<cv::Point2f> keypoints; 
    std::vector<float> embedding; 
};

class Face_embedding {
private:

    bool d_loaded = false;
    bool e_loaded = false;

    Ort::Env shared_env{ORT_LOGGING_LEVEL_WARNING, "VANTA_PIPELINE"};

    Ort::SessionOptions session_options_d;
    std::unique_ptr<Ort::Session> detector_session;
    Ort::MemoryInfo memory_info_d = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::SessionOptions session_options_e;
    std::unique_ptr<Ort::Session> extractor_session;
    Ort::MemoryInfo memory_info_e = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    const float kTargetLandmarks[5][2] = {
        {38.2946f, 51.6963f}, 
        {73.5318f, 51.5014f}, 
        {56.0252f, 71.7366f}, 
        {41.5493f, 92.3655f}, 
        {70.7299f, 92.2041f}  
    };

    void l2Normalize(std::vector<float>& embedding);
    
    // Core Computer Vision post-processing algorithm to filter duplicate bounding boxes
    std::vector<FaceResult> nms(std::vector<FaceResult>& proposals, float iou_threshold);


    // Face model paths are resolved at load() time from VantaConfig so the
    // engine does not depend on a hard-coded application package path.
    std::string detector_path;
    std::string extractor_path;

public:
    bool load();
    bool unload();
    bool is_loaded();

    cv::Mat load_img_seg(const std::string& PATH);
    std::vector<FaceResult> get_faces(const cv::Mat& image);
    std::vector<std::vector<float>> get_embedding(const cv::Mat& image, const std::vector<FaceResult>& faces);
};