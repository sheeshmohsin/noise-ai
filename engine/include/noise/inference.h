#pragma once

#include <cstddef>
#include <string>

namespace noise {

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    bool load_model(const std::string& model_path);
    bool run(const float* input, float* output, size_t num_frames);
    bool is_loaded() const;

private:
    bool loaded_ = false;
};

} // namespace noise
