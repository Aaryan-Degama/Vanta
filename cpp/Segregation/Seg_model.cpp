#include "Seg_model.hpp"

bool Face_embedding::load(const std::string& detector_path, const std::string& extractor_path) {
    /*
    Loads the Buffalo_sc face detector and extractor ONNX models into memory
    Uses shared ONNX Runtime environment with optimized session options
    
    Inputs:
        detector_path: File path to the detector ONNX model file
                      Model detects faces and outputs bounding boxes + keypoints
        extractor_path: File path to the extractor ONNX model file
                       Model extracts 512-dimensional embeddings from aligned faces
    
    Output:
        Returns true if both models loaded successfully, false if any error occurs
    
    Error Handling:
        - Checks if each model is already loaded (avoids duplicate loading)
        - Catches and prints exceptions during model loading
        - Returns false on any ONNX runtime errors
        
    Side Effects:
        - Sets d_loaded = true when detector successfully loads
        - Sets e_loaded = true when extractor successfully loads
        - Prints status messages to stdout/stderr during loading
    */

    if (!d_loaded) {
        try {
            // :: To be removed ::
            std::cout << "Loading Detector of Buffalo_sc Model ...\n";
            detector_session = std::make_unique<Ort::Session>(shared_env, detector_path.c_str(), session_options_d);
            d_loaded = true;
        } catch {
            return false;
        }
    }    
    
    if (!e_loaded) {
        try {
            // :: To be removed ::
            std::cout << "Loading Extractor of Buffalo_sc Model ...\n";
            extractor_session = std::make_unique<Ort::Session>(shared_env, extractor_path.c_str(), session_options_e);
            e_loaded = true;
        } catch {
            return false;
        }        
    }
    return true;
}

bool Face_embedding::unload() {
    /*
    Unloads and frees memory allocated by the detector and extractor ONNX models
    Uses unique_ptr reset() to properly release ONNX Runtime session resources
    
    Output:
        Returns true if unload completes successfully
        
    Side Effects:
        - Calls reset() on detector_session if d_loaded is true
        - Calls reset() on extractor_session if e_loaded is true
        - Sets both d_loaded and e_loaded flags to false
        - Prints status message to stdout
        
    Important:
        - Must be called before program termination to avoid memory leaks
        - Safe to call multiple times (flags prevent double-unload)
        - After calling, load() can be used to reload models if needed
    */

    if (d_loaded) detector_session.reset(); 
    if (e_loaded) extractor_session.reset();
    d_loaded = false;
    e_loaded = false;

    // :: To be removed ::
    std::cout << "Models unloaded.\n";
    
    return true;
}

void Face_embedding::l2Normalize(std::vector<float>& embedding) {
    /*
    Performs L2 (Euclidean) normalization on an embedding vector to unit length
    Essential for face similarity calculations using cosine distance
    
    Input/Output:
        embedding: A vector of float values that will be normalized in-place
                  Typically a 512-dimensional face embedding
    
    Algorithm:
        1. Calculates L2 norm: norm = sqrt(sum(value^2)) for all values
        2. Divides each value by the norm to get unit vector
        3. Result: magnitude of normalized vector equals 1.0
    
    Mathematical Formula:
        normalized[i] = embedding[i] / sqrt(sum(embedding[i]^2))
        
    Properties After Normalization:
        - Magnitude (norm) = 1.0
        - Cosine similarity between embeddings can be computed as dot product
        - More numerically stable for distance comparisons
        - Invariant to scaling of input embedding
        
    Edge Case:
        - If norm <= 0 (zero vector), no normalization is performed
        - Prevents division by zero errors
    */

    float norm = 0.0f;
    for (float val : embedding) norm += val * val;
    norm = std::sqrt(norm);
    if (norm > 0.0f) {
        for (float& val : embedding) val /= norm;
    }
}

bool Face_embedding::is_loaded() {
    /*
    Checks if both detector and extractor models are successfully loaded in memory
    
    Output:
        Returns true if both d_loaded AND e_loaded flags are true
        Returns false if either model is not loaded
        
    Usage:
        - Guards against calling get_faces() or get_embedding() without loaded models
        - Used internally by get_faces() and get_embedding() at start for validation
        - Should be checked before processing images to avoid runtime errors
    */
    return d_loaded && e_loaded;
}

cv::Mat load_img_seg(const std::string& PATH){
    return cv::imread(image_path);
}

std::vector<FaceResult> Face_embedding::nms(std::vector<FaceResult>& proposals, float iou_threshold) {
    /*
    Non-Maximum Suppression (NMS) - Removes overlapping face detections and keeps only the most confident ones
    
    Input -
        proposals: A vector of FaceResult objects containing all detected faces with their bounding boxes and confidence scores
        iou_threshold: The Intersection over Union threshold for suppressing overlapping detections (typically 0.4)
                      Detections with IoU > threshold are considered duplicates and removed
    
    Output -
        Returns a vector of FaceResult objects with overlapping detections removed, sorted by confidence score (highest first)
        
    Process:
        1. Sorts proposals by confidence score in descending order
        2. Iterates through sorted proposals, keeping high-confidence detections
        3. For each kept detection, calculates IoU with remaining proposals
        4. Suppresses (removes) proposals with IoU > threshold to avoid duplicate detections
    */
    
    std::vector<FaceResult> results;
    std::sort(proposals.begin(), proposals.end(), [](const FaceResult& a, const FaceResult& b) {
        return a.confidence > b.confidence;
    });

    std::vector<bool> suppressed(proposals.size(), false);
    for (size_t i = 0; i < proposals.size(); i++) {
        if (suppressed[i]) continue;
        results.push_back(proposals[i]);
        
        for (size_t j = i + 1; j < proposals.size(); j++) {
            if (suppressed[j]) continue;
            cv::Rect2f inter = proposals[i].bbox & proposals[j].bbox;
            float inter_area = inter.area();
            float union_area = proposals[i].bbox.area() + proposals[j].bbox.area() - inter_area;
            if (union_area > 0.0f && (inter_area / union_area) > iou_threshold) {
                suppressed[j] = true;
            }
        }
    }
    return results;
}

std::vector<FaceResult> Face_embedding::get_faces(const cv::Mat& image) {
    /*
    MAIN FUNCTION: Detects all faces in an image using the Buffalo_sc detector model
    
    Input - 
        image: The input image in cv::Mat format (BGR color space, any size)
               Must be a non-empty image for processing
    
    Output - 
        Returns a vector of FaceResult objects containing:
        - Bounding box coordinates and dimensions (cv::Rect2f)
        - Face detection confidence score (0.0 to 1.0)
        - 5 facial keypoints (eye centers, nose tip, mouth corners) as cv::Point2f
        - Results are scaled back to original image dimensions
        
    ALGORITHM STEPS:
    
    1. VALIDATION: Check if detector/extractor models are loaded and image is valid
    
    2. IMAGE PREPROCESSING (Letterbox):
       - Resize image to fit 640x640 while maintaining aspect ratio
       - Pad with zeros (black borders) to reach exactly 640x640
       - Calculate scaling factor for later coordinate transformation
    
    3. NORMALIZATION:
       - Convert image to float32
       - Normalize pixel values: (pixel / 128.0) - (127.5 / 128.0)
       - Reorder channels: BGR -> RGB (OpenCV standard conversion)
    
    4. TENSOR CREATION:
       - Convert normalized image to tensor format: (1, 3, 640, 640)
       - Format: 1 batch, 3 color channels, 640x640 resolution
    
    5. INFERENCE:
       - Run ONNX detector session with input tensor
       - Detector outputs feature maps at 3 different scales (stride 8, 16, 32)
    
    6. OUTPUT PARSING:
       - Extract and organize detector outputs by feature map scale:
         * score_maps: Confidence scores for face presence at each anchor
         * bbox_maps: Bounding box deltas (offsets from anchor positions)
         * kps_maps: Facial keypoints (5 landmarks: eyes, nose, mouth corners)
       - Determine stride (8, 16, or 32) based on output tensor shapes
    
    7. ANCHOR PROCESSING:
       - Iterate through each spatial position in feature maps
       - For each anchor point, check if confidence exceeds threshold (0.5)
       - Handle logit outputs: convert with sigmoid activation if needed
       - Decode predictions:
         * Calculate face center (cx, cy) from anchor position
         * Calculate bounding box from delta offsets
         * Calculate keypoint positions from offset predictions
    
    8. NON-MAXIMUM SUPPRESSION (NMS):
       - Remove overlapping detections
       - Keep only the highest-confidence detections
       - NMS threshold: 0.4 (suppress if IoU > 0.4)
    
    9. COORDINATE SCALING:
       - Scale all bounding boxes and keypoints back to original image size
       - Undo the scaling factor applied during preprocessing
    
    For more details on FaceResult structure, see "Seg_model.hpp"
    */

    std::vector<FaceResult> final_results;
    if (!is_loaded() || image.empty()) return final_results;

    // Image Padding (Letterbox)
    float scale = std::min(640.0f / image.cols, 640.0f / image.rows);
    int new_w = std::round(image.cols * scale);
    int new_h = std::round(image.rows * scale);
    
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_w, new_h));
    
    cv::Mat padded = cv::Mat::zeros(640, 640, CV_8UC3); 
    resized.copyTo(padded(cv::Rect(0, 0, new_w, new_h)));

    cv::Mat float_img;
    padded.convertTo(float_img, CV_32FC3, 1.0f / 128.0f, -127.5f / 128.0f);

    std::vector<float> input_tensor_values(1 * 3 * 640 * 640);
    for (int c = 0; c < 3; ++c) {
        for (int h = 0; h < 640; ++h) {
            for (int w = 0; w < 640; ++w) {
                input_tensor_values[c * 640 * 640 + h * 640 + w] = float_img.at<cv::Vec3f>(h, w)[c];
            }
        }
    }

    std::vector<int64_t> input_dims = {1, 3, 640, 640};
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_d, input_tensor_values.data(), input_tensor_values.size(), 
        input_dims.data(), input_dims.size()
    );

    Ort::AllocatorWithDefaultOptions allocator;
    std::string det_input_name = detector_session->GetInputNameAllocated(0, allocator).get();
    std::vector<const char*> dynamic_det_input_node_names = {det_input_name.c_str()};

    size_t num_output_nodes = detector_session->GetOutputCount();
    std::vector<std::string> det_output_node_names_str;
    std::vector<const char*> dynamic_det_output_node_names;
    for (size_t i = 0; i < num_output_nodes; i++) {
        det_output_node_names_str.push_back(detector_session->GetOutputNameAllocated(i, allocator).get());
    }
    for (const auto& s : det_output_node_names_str) dynamic_det_output_node_names.push_back(s.c_str());

    auto output_tensors = detector_session->Run(
        Ort::RunOptions{nullptr}, 
        dynamic_det_input_node_names.data(), &input_tensor, 1, 
        dynamic_det_output_node_names.data(), dynamic_det_output_node_names.size()
    );

    // Dimensional Parsing 
    std::map<int, std::pair<float*, int>> score_maps; 
    std::map<int, float*> bbox_maps;
    std::map<int, float*> kps_maps;

    for (size_t i = 0; i < num_output_nodes; i++) {
        auto shape = output_tensors[i].GetTensorTypeAndShapeInfo().GetShape();
        
        int num_anchors = -1;
        int last_dim = -1;
        
        if (shape.size() == 3) {
            num_anchors = shape[1];
            last_dim = shape[2];
        } else if (shape.size() == 2) {
            num_anchors = shape[0];
            last_dim = shape[1];
        }

        float* data = output_tensors[i].GetTensorMutableData<float>();
        int stride = 0;

        if (num_anchors == 12800) stride = 8;
        else if (num_anchors == 3200) stride = 16;
        else if (num_anchors == 800) stride = 32;

        if (stride != 0) {
            if (last_dim == 1 || last_dim == 2) score_maps[stride] = {data, last_dim};
            else if (last_dim == 4) bbox_maps[stride] = data;
            else if (last_dim == 10) kps_maps[stride] = data;
        }
    }

    std::vector<FaceResult> proposals;
    float confidence_threshold = 0.5f;

    for (int stride : {8, 16, 32}) {

        if (score_maps.count(stride) == 0 || bbox_maps.count(stride) == 0 || kps_maps.count(stride) == 0) {
            continue; // Prevent accessing a null map entry
        }

        float* scores = score_maps[stride].first;
        int score_dim = score_maps[stride].second;
        float* bboxes = bbox_maps[stride];
        float* kps = kps_maps[stride];

        bool is_logit = (scores[0] < 0.0f || scores[0] > 1.0f);

        int feature_size = 640 / stride;
        int anchor_idx = 0;

        for (int row = 0; row < feature_size; row++) {
            for (int col = 0; col < feature_size; col++) {
                for (int k = 0; k < 2; k++) { 
                    
                    float score = (score_dim == 1) ? scores[anchor_idx] : scores[anchor_idx * 2 + 1];

                    if (is_logit) score = 1.0f / (1.0f + std::exp(-score));

                    if (score > confidence_threshold) {
                        FaceResult face;
                        face.confidence = score;

                        float cx = col * stride;
                        float cy = row * stride;

                        float dx_min = bboxes[anchor_idx * 4 + 0] * stride;
                        float dy_min = bboxes[anchor_idx * 4 + 1] * stride;
                        float dx_max = bboxes[anchor_idx * 4 + 2] * stride;
                        float dy_max = bboxes[anchor_idx * 4 + 3] * stride;

                        float x1 = cx - dx_min;
                        float y1 = cy - dy_min;
                        float x2 = cx + dx_max;
                        float y2 = cy + dy_max;
                        face.bbox = cv::Rect2f(x1, y1, x2 - x1, y2 - y1);

                        for (int pt = 0; pt < 5; pt++) {
                            float px = cx + kps[anchor_idx * 10 + pt * 2 + 0] * stride;
                            float py = cy + kps[anchor_idx * 10 + pt * 2 + 1] * stride;
                            face.keypoints.push_back(cv::Point2f(px, py));
                        }
                        proposals.push_back(face);
                    }
                    anchor_idx++;
                }
            }
        }
    }

    std::vector<FaceResult> suppressed_results = nms(proposals, 0.4f);

    for (auto& face : suppressed_results) {
        face.bbox.x /= scale;
        face.bbox.y /= scale;
        face.bbox.width /= scale;
        face.bbox.height /= scale;
        for (auto& pt : face.keypoints) {
            pt.x /= scale;
            pt.y /= scale;
        }
        final_results.push_back(face);
    }

    return final_results;
}

cv::Mat Face_embedding::align_face(const cv::Mat& image, const FaceResult& face) {
    cv::Mat src_pts(5, 2, CV_32FC1, (void*)face.keypoints.data());
    cv::Mat dst_pts(5, 2, CV_32FC1, (void*)kTargetLandmarks);
    
    // Cropping the Image that is alignning it
    cv::Mat transformation_matrix = cv::estimateAffinePartial2D(src_pts, dst_pts);
    
    cv::Mat aligned_face;
    cv::warpAffine(image, aligned_face, transformation_matrix, cv::Size(112, 112), cv::INTER_CUBIC);
    return aligned_face;
}

std::vector<std::vector<float>> Face_embedding::get_embedding(const cv::Mat& image, const std::vector<FaceResult>& faces) {
    /*
    Extracts 512-dimensional embeddings for each detected face using the Buffalo_sc extractor model
    
    Input - 
        image: The original input image in cv::Mat format (BGR color space)
               Must be non-empty and same image used for face detection
        faces: A vector of FaceResult objects (typically output from Face_embedding::get_faces)
               Contains bounding boxes and facial keypoints for alignment

    Output -
        Returns a vector of embeddings, one per input face:
        - Each embedding is a vector of 512 float values
        - Embeddings are L2-normalized (magnitude = 1.0)
        - Can be compared for face similarity using cosine distance or Euclidean distance
        - Empty vector if image is invalid or no faces provided
        
    ALGORITHM STEPS FOR EACH FACE:
    
    1. FACE ALIGNMENT:
       - Use 5 facial keypoints to estimate affine transformation
       - Align detected face to standard landmark positions (defined in kTargetLandmarks)
       - Crop and warp face region to normalized 112x112 size
       - Ensures consistent face orientation and scale
    
    2. COLOR SPACE CONVERSION:
       - Convert from BGR (OpenCV standard) to RGB color space
       - Required for model compatibility
    
    3. NORMALIZATION:
       - Convert to float32 format
       - Normalize pixel values: pixel / 127.5 - 1.0
       - Scales pixel values to approximately [-1, 1] range
    
    4. TENSOR CREATION:
       - Create input tensor with shape (1, 3, 112, 112)
       - Format: 1 batch, 3 RGB channels, 112x112 normalized face image
    
    5. INFERENCE:
       - Run ONNX extractor session with aligned face tensor
       - Model outputs 512-dimensional embedding vector
    
    6. L2 NORMALIZATION:
       - Normalize embedding to unit length (magnitude = 1.0)
       - Enables meaningful cosine similarity calculations
       - Formula: normalized = embedding / sqrt(sum(embedding^2))
    
    7. RETURN:
       - All embeddings collected in vector and returned
       - Order matches input faces order
    */
    
    std::vector<std::vector<float>> all_embeddings;
    if (!is_loaded() || image.empty() || faces.empty()) return all_embeddings;


    for (const auto& face : faces) {

        cv::Mat aligned_face = align_face(image, face);

        // Converting the RGB to BGR (OpenCV standard)
        cv::Mat rgb_face, float_face;
        cv::cvtColor(aligned_face, rgb_face, cv::COLOR_BGR2RGB); 
        rgb_face.convertTo(float_face, CV_32FC3, 1.0 / 127.5, -1.0);

        // Creating the values for the input tensor
        int channels = 3, height = 112, width = 112;
        std::vector<float> input_tensor_values(channels * height * width);

        
        for (int c = 0; c < channels; ++c) {
            for (int h = 0; h < height; ++h) {
                for (int w = 0; w < width; ++w) {
                    input_tensor_values[c * height * width + h * width + w] = float_face.at<cv::Vec3f>(h, w)[c];
                }
            }
        }

        std::vector<int64_t> input_dims = {1, channels, height, width}; // {Batch, channels,  height, width}
        
        // Creating the input tensor
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_e, input_tensor_values.data(), input_tensor_values.size(), 
            input_dims.data(), input_dims.size()
        );

        // Gettign the Input and output layer names
        Ort::AllocatorWithDefaultOptions allocator;
        std::string ext_input_name = extractor_session->GetInputNameAllocated(0, allocator).get();
        std::string ext_output_name = extractor_session->GetOutputNameAllocated(0, allocator).get();
        
        std::vector<const char*> dynamic_ext_input_node_names = {ext_input_name.c_str()};
        std::vector<const char*> dynamic_ext_output_node_names = {ext_output_name.c_str()};

        // Running the Extractor Model
        auto output_tensors = extractor_session->Run(
            Ort::RunOptions{nullptr}, 
            dynamic_ext_input_node_names.data(), &input_tensor, 1, 
            dynamic_ext_output_node_names.data(), 1
        );

        // To get it to float
        // output_tensor will usually in Ort::Value or Ort::Float  
        float* raw_output = output_tensors[0].GetTensorMutableData<float>();
        std::vector<float> embedding(raw_output, raw_output + 512);
        

        //2 Normalizing the embedding
        l2Normalize(embedding);
        all_embeddings.push_back(embedding);
    }

    return all_embeddings;
}