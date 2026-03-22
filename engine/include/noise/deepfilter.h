#pragma once

#include <string>
#include <memory>

namespace noise {

class DeepFilterEngine {
public:
    DeepFilterEngine();
    ~DeepFilterEngine();

    // Load the ONNX model
    bool load(const std::string& model_path);

    // Process exactly 480 mono float32 samples (PCM, range [-1, 1])
    // Returns true on success
    bool process_frame(const float* input, float* output);

    // Reset recurrent state (e.g., on mode switch or device change)
    void reset_state();

    // Set the maximum attenuation limit in dB.
    // Higher values = less aggressive suppression = better voice preservation.
    // 0.0 = no limit (most aggressive), 12.0 = balanced, 6.0 = more aggressive.
    void set_attenuation_limit(float db);

    bool is_loaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace noise
