#include "CLIP_image.hpp"

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

    cv::resize(image, image, cv::Size(224, 224));   // Resize to 224 x 224
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);  // BGR to RGB
    image.convertTo(image, CV_32F, 1.0 / 255.0);    // Convert pixels to float32 (0.0 to 1.0)
    

    // Normalizing to CLIP Standard
    std::vector<cv::Mat> channels(3);
    cv::split(image, channels);                     // Split into R, G, B channels

    for (int i = 0; i < 3; ++i) {
        // (Pixel - Mean) / Std_dev for each specific channel
        channels[i] = (channels[i] - mean[i]) / std_dev[i];
    }

    cv::merge(channels, image);                     // HWC to CHW
    
    image = cv::dnn::blobFromImage(image, 1.0, cv::Size(), cv::Scalar(), false, false);



    std::cout << image.dims << std::endl;           

    for(int i = 0; i < image.dims; i++)
        std::cout << image.size[i] << " ";  

    return image;
}


cv::Mat CLIP_instance::get_img() const {
    return image;
}