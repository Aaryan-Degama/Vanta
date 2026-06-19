#include <iostream>
#include <vector>
#include <opencv2/opencv.hpp>
#include "Seg_model.hpp"

#include <chrono>

int main() {
    std::cout << "--- Vanta End-to-End Face Pipeline ---\n";

    Face_embedding model;
    auto now = std::chrono::system_clock::now();


    if (!model.load("./.model/buffalo_sc/det_500m.onnx","./.model/buffalo_sc/w600k_mbf.onnx")) {
        std::cerr << "Failed to load models. Exiting.\n";
        return -1;
    }

    auto elapsed = std::chrono::system_clock::now() - now;
    auto time = elapsed.count()/1e6;
    std::cout <<"time : " << time <<'\n';


    std::string image_path = "./data/DSC_5543.jpg"; 
    cv::Mat test_img = cv::imread(image_path);

    if (test_img.empty()) {
        std::cerr << "Error: Could not read image at " << image_path << "\n";
        return -1;
    }
    std::cout << "\nLoaded image: " << image_path << " (" << test_img.cols << "x" << test_img.rows << ")\n";

    now = std::chrono::system_clock::now();
    // 1. Run Real Detection
    std::cout << "Scanning image for faces...\n";
    std::vector<FaceResult> detected_faces = model.get_faces(test_img);

    elapsed = std::chrono::system_clock::now() - now;
    time = elapsed.count()/1e6;
    std::cout << "time : " << time<<'\n';



    if (detected_faces.empty()) {
        std::cout << "No faces were detected in this image.\n";
        return 0;
    }

    std::cout << "Successfully found " << detected_faces.size() << " face(s)!\n";
    
    now = std::chrono::system_clock::now();

    // 2. Run Real Extraction
    std::cout << "Extracting embeddings...\n";
    std::vector<std::vector<float>> embeddings = model.get_embedding(test_img, detected_faces);

    elapsed = std::chrono::system_clock::now() - now;
    time = elapsed.count()/1e6;
    std::cout << "time : " << time<<'\n';

    // 3. Print Results
    for (size_t i = 0; i < detected_faces.size(); ++i) {
        std::cout << "\n--- Face #" << (i + 1) << " ---\n";
        std::cout << "  Confidence: " << (detected_faces[i].confidence * 100) << "%\n";
        std::cout << "  Bounding Box: [X: " << detected_faces[i].bbox.x 
                  << ", Y: " << detected_faces[i].bbox.y 
                  << ", W: " << detected_faces[i].bbox.width 
                  << ", H: " << detected_faces[i].bbox.height << "]\n";
        
        std::cout << "  Embedding: [ ";
        for (int j = 0; j < 5; ++j) std::cout << embeddings[i][j] << " ";
        std::cout << "... ]\n";
    }

    std::cout << "\nCleaning up...\n";
    model.unload();

    return 0;
}