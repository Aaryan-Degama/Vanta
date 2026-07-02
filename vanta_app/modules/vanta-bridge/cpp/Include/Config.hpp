#pragma once

/**
 * @file Config.hpp
 * Runtime configuration singleton for the Vanta C++ engine.
 *
 * The Android Kotlin layer calls set_models_dir() once at module creation so
 * model and tokenizer paths are derived from the actual app files directory
 * instead of a hard-coded package path.
 */

#include <string>
#include <mutex>

class VantaConfig {
public:
    /**
     * Returns the singleton instance.
     */
    static VantaConfig& instance() {
        static VantaConfig cfg;
        return cfg;
    }

    /**
     * Sets the root directory that contains ONNX models and tokenizer files.
     * Must be called before any model is loaded.
     *
     * @param models_dir Absolute path ending with `/VantaModels`.
     */
    void set_models_dir(const std::string& models_dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        models_dir_ = models_dir;
    }

    /**
     * Returns the configured models directory, or an empty string if not set.
     */
    std::string models_dir() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return models_dir_;
    }

    /**
     * Builds a full path to a file inside the models directory.
     *
     * @param filename Name of the model/tokenizer file.
     * @return Absolute path to the file.
     */
    std::string model_path(const std::string& filename) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return models_dir_ + "/" + filename;
    }

    void set_crops_dir(const std::string& crops_dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        crops_dir_ = crops_dir;
    }

    std::string crops_dir() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return crops_dir_;
    }

    std::string crop_path(const std::string& filename) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return crops_dir_ + "/" + filename;
    }

private:
    VantaConfig() = default;
    ~VantaConfig() = default;

    // Non-copyable singleton.
    VantaConfig(const VantaConfig&) = delete;
    VantaConfig& operator=(const VantaConfig&) = delete;

    mutable std::mutex mutex_;
    std::string models_dir_;
    std::string crops_dir_;
};
