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
#include "Config.hpp"

// Least-squares 2D similarity (rotation + uniform scale + translation) mapping
// src -> dst over ALL n points; same result as Umeyama / skimage
// SimilarityTransform, which is what ArcFace training crops are aligned with.
// Writes the 2x3 matrix row-major into M: [a, -b, tx; b, a, ty].
// Returns false (M untouched) if the points are degenerate or non-finite.
// Plain math, no cv:: types, so it can be unit-tested without OpenCV libs.
inline bool fit_similarity_2d(const float src[][2], const float dst[][2], int n, double M[6]) {
    if (n < 2) return false;

    double msx = 0, msy = 0, mdx = 0, mdy = 0;
    for (int i = 0; i < n; ++i) {
        msx += src[i][0]; msy += src[i][1];
        mdx += dst[i][0]; mdy += dst[i][1];
    }
    msx /= n; msy /= n; mdx /= n; mdy /= n;

    double dot = 0, cross = 0, den = 0;
    for (int i = 0; i < n; ++i) {
        const double x = src[i][0] - msx, y = src[i][1] - msy;
        const double xd = dst[i][0] - mdx, yd = dst[i][1] - mdy;
        dot   += x * xd + y * yd;
        cross += x * yd - y * xd;
        den   += x * x + y * y;
    }
    // Negated comparisons so NaN/Inf inputs also fail.
    if (!(den > 1e-6)) return false;             // source points (nearly) coincide

    const double a = dot / den, b = cross / den;
    if (!(a * a + b * b > 1e-12)) return false;  // collapsed scale: not invertible

    M[0] = a; M[1] = -b; M[2] = mdx - (a * msx - b * msy);
    M[3] = b; M[4] =  a; M[5] = mdy - (b * msx + a * msy);
    return true;
}

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
    // Both return an EMPTY Mat if the face cannot be aligned (not exactly 5
    // keypoints, or a degenerate fit).
    cv::Mat align_face(const cv::Mat& image, const FaceResult& face);
    cv::Mat align_face(const cv::Mat& image, const FaceResult& face, int output_size);
    // Always one slot per input face; a face whose alignment failed gets an
    // EMPTY vector so indices stay aligned with `faces`.
    std::vector<std::vector<float>> get_embedding(const cv::Mat& image, const std::vector<FaceResult>& faces);
};