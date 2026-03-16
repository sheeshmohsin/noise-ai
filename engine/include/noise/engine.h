#pragma once

#include "noise/types.h"
#include <cstdint>
#include <vector>

namespace noise {

class Engine {
public:
    Engine();
    ~Engine();

    bool init(const AudioFormat& format);
    void shutdown();

    /// Process interleaved stereo float32 audio through RNNoise.
    /// num_frames = number of frames (each frame has `channels` samples).
    bool process_frame(const float* input, float* output, uint32_t num_frames);

    void set_mode(NoiseMode mode);
    NoiseMode get_mode() const;

    EngineStatus get_status() const;

private:
    NoiseMode mode_;
    EngineStatus status_;
    uint32_t channels_ = 2;

    // RNNoise state (DenoiseState* cast to void* to avoid exposing the C header)
    void* rnnoise_state_ = nullptr;

    // Buffering for RNNoise's fixed 480-sample frame requirement.
    // Input: accumulated mono samples waiting to be processed.
    std::vector<float> input_buffer_;
    // Output: processed mono samples ready for consumption.
    std::vector<float> output_buffer_;
    size_t output_read_pos_ = 0;
    // Whether we have accumulated enough output samples to start draining.
    // Prevents periodic underruns caused by frame size misalignment.
    bool output_started_ = false;
    // Debug: count of RNNoise frames processed (for periodic logging)
    uint32_t debug_frame_count_ = 0;
};

} // namespace noise
