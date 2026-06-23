#pragma once
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>    // imread
#include <opencv2/imgproc.hpp>      // resize, cvtColor
#include <opencv2/dnn.hpp>          // blobFromImage


#include <string>

// :: To be removed ::
#include <iostream>                


class CLIP_instance{
    private:
    cv::Mat image;
    std::string img_path;
    const float mean[3] = {0.48145466f, 0.4578275f, 0.40821073f};
    const float std_dev[3] = {0.26862954f, 0.26130258f, 0.27577711f};

    public:
    CLIP_instance(std::string& path);
    CLIP_instance(cv::Mat& img);

    // void info(); // we need to implement it to get the information about the image lie the demension it shoudl be used like a Getter

    cv::Mat load_img (std::string &file_path) ;
    cv::Mat get_img() const;


};