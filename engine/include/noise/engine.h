#pragma once

#include "noise/types.h"
#include "noise/deepfilter.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace noise {

class Engine {
public:
    Engine();
    ~Engine();

    bool init(const AudioFormat& format);
    void shutdown();

    /// Process interleaved stereo float32 audio through the active denoiser.
    /// num_frames = number of frames (each frame has `channels` samples).
    /// Routes to RNNoise (CpuSaver) or DeepFilterNet (Balanced/MaxQuality).
    bool process_frame(const float* input, float* output, uint32_t num_frames);

    void set_mode(NoiseMode mode);
    NoiseMode get_mode() const;

    EngineStatus get_status() const;

    /// Override the dry mix ratio. Set to -1.0 to reset to mode default.
    void set_dry_mix(float mix);
    float get_dry_mix() const;

    /// Override the attenuation limit in dB. Set to -1.0 to reset to mode default.
    void set_attenuation_limit(float db);
    float get_attenuation_limit() const;

private:
    /// Process a single 480-sample mono frame through the active denoiser.
    /// rnn_input is in RNNoise int16-scaled format for CpuSaver mode,
    /// or raw float [-1,1] for DeepFilterNet modes.
    void process_mono_chunk();

    /// Try to find and load the DeepFilterNet ONNX model.
    void load_deepfilter_model();

    /// Apply the appropriate DeepFilterNet attenuation limit for the given mode.
    void apply_attenuation_for_mode(NoiseMode mode);

    /// Return the dry mix ratio for the given mode (0.0 = fully denoised).
    /// Used for dry/wet blending to recover voice suppressed by aggressive denoising.
    static float dry_mix_for_mode(NoiseMode mode);

    NoiseMode mode_;
    EngineStatus status_;
    float dry_mix_override_ = -1.0f;   // -1.0 = use mode default
    float atten_lim_override_ = -1.0f; // -1.0 = use mode default
    uint32_t channels_ = 2;

    // RNNoise state (DenoiseState* cast to void* to avoid exposing the C header)
    void* rnnoise_state_ = nullptr;

    // DeepFilterNet engine (ONNX Runtime based)
    std::unique_ptr<DeepFilterEngine> deepfilter_;

    // Buffering for the fixed 480-sample frame requirement (shared by both denoisers).
    // Input: accumulated mono samples waiting to be processed.
    std::vector<float> input_buffer_;
    // Output: processed mono samples ready for consumption.
    std::vector<float> output_buffer_;
    size_t output_read_pos_ = 0;
    // Whether we have accumulated enough output samples to start draining.
    // Prevents periodic underruns caused by frame size misalignment.
    bool output_started_ = false;
    // Debug: count of frames processed (for periodic logging)
    uint32_t debug_frame_count_ = 0;
};

} // namespace noise
