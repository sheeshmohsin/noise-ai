#include "noise/inference.h"
#include <cstring>

namespace noise {

InferenceEngine::InferenceEngine() = default;
InferenceEngine::~InferenceEngine() = default;

bool InferenceEngine::load_model(const std::string& /*model_path*/)
{
    // Stub: pretend model loaded successfully
    loaded_ = true;
    return true;
}

bool InferenceEngine::run(const float* input, float* output, size_t num_frames)
{
    if (!loaded_) {
        return false;
    }
    // Stub: passthrough
    std::memcpy(output, input, num_frames * sizeof(float));
    return true;
}

bool InferenceEngine::is_loaded() const
{
    return loaded_;
}

} // namespace noise
