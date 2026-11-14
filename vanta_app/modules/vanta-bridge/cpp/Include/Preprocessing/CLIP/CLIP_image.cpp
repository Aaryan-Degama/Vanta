#include "CLIP_image.hpp"

#include <algorithm>    // std::min, std::max

CLIP_instance::CLIP_instance (std::string &path) {
    image = load_img(path);
}

CLIP_instance::CLIP_instance ( cv::Mat& img ) {
    CLIP_instance::image = img; 
}

cv::Mat CLIP_instance::load_img(std::string& path){
    img_path = path;
    image = cv::imread(img_path);

    if(image.empty()){
        return image;
    }

    // CLIP reference preprocessing (short side to 224, aspect ratio kept, then centre-crop 224 x 224),
    // done as crop-then-resize: the centre square is exactly the region that ends up in the window,
    // and resizing only it keeps an extreme aspect ratio (e.g. 2 x 5000 px) from being upscaled whole.
    int side = std::min(image.cols, image.rows);
    cv::Mat square = image(cv::Rect((image.cols - side) / 2, (image.rows - side) / 2, side, side));
    cv::Mat resized;
    cv::resize(square, resized, cv::Size(224, 224), 0, 0, side > 224 ? cv::INTER_AREA : cv::INTER_CUBIC);
    image = resized;
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);  // BGR to RGB
    image.convertTo(image, CV_32F, 1.0 / 255.0);    // Convert pixels to float32 (0.0 to 1.0)
    

    // Normalizing to CLIP Standard
    std::vector<cv::Mat> channels(3);
    cv::split(image, channels);                     // Split into R, G, B channels

    for (int i = 0; i < 3; ++i) {
        // (Pixel - Mean) / Std_dev for each specific channel
        channels[i] = (channels[i] - mean[i]) / std_dev[i];
    }

    // Allocate NCHW contiguous block
    cv::Mat blob(1, 3 * 224 * 224, CV_32F);
    float* blob_data = blob.ptr<float>();

    for (int c = 0; c < 3; ++c) {
        // Channels are already 224x224 CV_32FC1
        cv::Mat channel_continuous;
        if (!channels[c].isContinuous()) {
            channels[c].copyTo(channel_continuous);
        } else {
            channel_continuous = channels[c];
        }
        std::memcpy(blob_data + c * 224 * 224, channel_continuous.ptr<float>(), 224 * 224 * sizeof(float));
    }

    image = blob;

    return image;
}


cv::Mat CLIP_instance::get_img() const {
    return image;
}