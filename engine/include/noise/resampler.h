#pragma once

#include <cstddef>
#include <cstdint>

namespace noise {

class Resampler {
public:
    Resampler(uint32_t input_rate, uint32_t output_rate);
    ~Resampler();

    void process(const float* input, size_t input_frames,
                 float* output, size_t& output_frames);

private:
    uint32_t input_rate_;
    uint32_t output_rate_;
};

} // namespace noise
