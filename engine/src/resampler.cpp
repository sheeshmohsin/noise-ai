#include "noise/resampler.h"
#include <cstring>

namespace noise {

Resampler::Resampler(uint32_t input_rate, uint32_t output_rate)
    : input_rate_(input_rate)
    , output_rate_(output_rate)
{
    // Rates will be used when real resampling is implemented
    (void)input_rate_;
    (void)output_rate_;
}

Resampler::~Resampler() = default;

void Resampler::process(const float* input, size_t input_frames,
                        float* output, size_t& output_frames)
{
    // Stub: passthrough (assumes same rate for now)
    output_frames = input_frames;
    std::memcpy(output, input, input_frames * sizeof(float));
}

} // namespace noise
