#include <algorithm>
#include <cstddef>

namespace noise {

void apply_limiter(float* audio, size_t num_samples, float threshold)
{
    for (size_t i = 0; i < num_samples; ++i) {
        audio[i] = std::clamp(audio[i], -threshold, threshold);
    }
}

} // namespace noise
