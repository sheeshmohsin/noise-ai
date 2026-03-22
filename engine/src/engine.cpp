#include "noise/engine.h"
#include "noise/deepfilter.h"
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cmath>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern "C" {
#include "rnnoise.h"
}

// Both RNNoise and DeepFilterNet process exactly 480 mono samples per frame (10ms at 48kHz)
static constexpr uint32_t DENOISE_FRAME_SIZE = 480;

// Minimum output buffer level before we start reading from it.
// This prevents periodic underruns caused by the mismatch between
// the denoiser's 480-sample frame size and the AUHAL callback size (often 128).
// Two frames (~20ms at 48kHz) provides enough cushion.
static constexpr uint32_t OUTPUT_PREFILL_SAMPLES = DENOISE_FRAME_SIZE * 2;

namespace noise {

/// Locate the DeepFilterNet ONNX model by searching several paths.
static std::string find_model_path()
{
    // Get the executable's directory
    std::string exe_dir;
#ifdef __APPLE__
    char exe_path[1024];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        std::string full(exe_path);
        auto pos = full.rfind('/');
        if (pos != std::string::npos) {
            exe_dir = full.substr(0, pos);
        }
    }
#endif

    // Candidate paths in priority order
    std::vector<std::string> candidates;
    if (!exe_dir.empty()) {
        // App bundle Resources/Models/ (primary path for installed app)
        candidates.push_back(exe_dir + "/../Resources/Models/deepfilternet.onnx");
        candidates.push_back(exe_dir + "/../models/deepfilternet.onnx");
        candidates.push_back(exe_dir + "/models/deepfilternet.onnx");
    }
    candidates.push_back("./models/deepfilternet.onnx");

    for (const auto& path : candidates) {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            fclose(f);
            fprintf(stderr, "[NoiseAI-Engine] Found DeepFilterNet model at: %s\n", path.c_str());
            return path;
        }
    }

    fprintf(stderr, "[NoiseAI-Engine] WARNING: DeepFilterNet model not found in any search path\n");
    return {};
}

Engine::Engine()
    : mode_(NoiseMode::Balanced)
    , status_(EngineStatus::Stopped)
{
}

Engine::~Engine()
{
    shutdown();
}

void Engine::load_deepfilter_model()
{
    deepfilter_ = std::make_unique<DeepFilterEngine>();
    std::string model_path = find_model_path();
    if (!model_path.empty()) {
        if (!deepfilter_->load(model_path)) {
            fprintf(stderr, "[NoiseAI-Engine] WARNING: Failed to load DeepFilterNet model\n");
        } else {
            apply_attenuation_for_mode(mode_);
        }
    }
}

bool Engine::load_deepfilter_model(const std::string& model_path)
{
    if (model_path.empty()) {
        fprintf(stderr, "[NoiseAI-Engine] load_deepfilter_model: empty path\n");
        return false;
    }

    // Verify the file exists
    FILE* f = fopen(model_path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[NoiseAI-Engine] load_deepfilter_model: file not found: %s\n",
                model_path.c_str());
        return false;
    }
    fclose(f);

    if (!deepfilter_) {
        deepfilter_ = std::make_unique<DeepFilterEngine>();
    }

    if (!deepfilter_->load(model_path)) {
        fprintf(stderr, "[NoiseAI-Engine] WARNING: Failed to load DeepFilterNet model from: %s\n",
                model_path.c_str());
        return false;
    }

    fprintf(stderr, "[NoiseAI-Engine] Loaded DeepFilterNet model from: %s\n", model_path.c_str());
    apply_attenuation_for_mode(mode_);
    return true;
}

void Engine::apply_attenuation_for_mode(NoiseMode mode)
{
    if (!deepfilter_ || !deepfilter_->is_loaded()) {
        return;
    }

    // Use override if set, otherwise 0.0 (no limit) for maximum noise suppression.
    // Voice recovery is handled via dry/wet mix post-processing.
    float atten = (atten_lim_override_ >= 0.0f) ? atten_lim_override_ : 0.0f;
    deepfilter_->set_attenuation_limit(atten);
    (void)mode; // all modes use the same default attenuation
}

float Engine::dry_mix_for_mode(NoiseMode mode)
{
    switch (mode) {
        case NoiseMode::Balanced:   return 0.02f;  // 2% original — voice-safe
        case NoiseMode::MaxQuality: return 0.01f;  // 1% original — max noise removal
        default:                    return 0.0f;    // CpuSaver uses RNNoise, no mix
    }
}

bool Engine::init(const AudioFormat& format)
{
    // Clean up any previous state
    if (rnnoise_state_) {
        rnnoise_destroy(static_cast<DenoiseState*>(rnnoise_state_));
        rnnoise_state_ = nullptr;
    }

    channels_ = format.channels;

    // Create RNNoise state with built-in model (always available as fallback)
    rnnoise_state_ = rnnoise_create(nullptr);
    if (!rnnoise_state_) {
        fprintf(stderr, "[NoiseAI-Engine] ERROR: rnnoise_create() returned null!\n");
        status_ = EngineStatus::Error;
        return false;
    }
    fprintf(stderr, "[NoiseAI-Engine] RNNoise state created successfully (channels=%u)\n",
            channels_);

    // Load DeepFilterNet model if not already loaded
    if (!deepfilter_ || !deepfilter_->is_loaded()) {
        load_deepfilter_model();
    } else {
        // Reset state on re-init (e.g., device change)
        deepfilter_->reset_state();
    }

    // Clear buffers
    input_buffer_.clear();
    output_buffer_.clear();
    output_read_pos_ = 0;
    output_started_ = false;

    // Reserve reasonable capacity to avoid reallocations during processing
    input_buffer_.reserve(DENOISE_FRAME_SIZE * 4);
    output_buffer_.reserve(DENOISE_FRAME_SIZE * 4);

    debug_frame_count_ = 0;

    // Reset overload state
    overload_count_ = 0;
    overload_cooldown_ = 0;
    in_overload_ = false;

    status_ = EngineStatus::Running;
    return true;
}

void Engine::shutdown()
{
    if (rnnoise_state_) {
        rnnoise_destroy(static_cast<DenoiseState*>(rnnoise_state_));
        rnnoise_state_ = nullptr;
    }
    // Keep deepfilter_ alive across shutdown/init cycles to avoid reloading the model.
    // Just reset its state.
    if (deepfilter_) {
        deepfilter_->reset_state();
    }
    input_buffer_.clear();
    output_buffer_.clear();
    output_read_pos_ = 0;
    output_started_ = false;
    overload_count_ = 0;
    overload_cooldown_ = 0;
    in_overload_ = false;
    status_ = EngineStatus::Stopped;
}

void Engine::process_mono_chunk()
{
    float df_input[DENOISE_FRAME_SIZE];
    float df_output[DENOISE_FRAME_SIZE];

    bool use_deepfilter = (mode_ != NoiseMode::CpuSaver)
                          && deepfilter_
                          && deepfilter_->is_loaded();

    // --- Overload handling: passthrough if inference is consistently too slow ---
    if (in_overload_) {
        if (overload_cooldown_ > 0) {
            --overload_cooldown_;
            // Passthrough: copy input directly to output
            for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
                output_buffer_.push_back(input_buffer_[j]);
            }
            input_buffer_.erase(input_buffer_.begin(),
                                input_buffer_.begin() + DENOISE_FRAME_SIZE);
            return;
        }
        // Cooldown expired — try inference again
        fprintf(stderr, "[NoiseAI-Engine] Overload cooldown expired, resuming inference\n");
        in_overload_ = false;
        overload_count_ = 0;
    }

    // --- Time the inference ---
    auto t_start = std::chrono::steady_clock::now();

    if (use_deepfilter) {
        // DeepFilterNet: input is raw float [-1, 1]
        for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
            df_input[j] = input_buffer_[j];
        }

        bool ok = deepfilter_->process_frame(df_input, df_output);

        // Dry/wet mix: blend denoised output with original to recover suppressed voice
        const float dry = (dry_mix_override_ >= 0.0f) ? dry_mix_override_ : dry_mix_for_mode(mode_);
        const float wet = 1.0f - dry;
        if (dry > 0.0f) {
            for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
                df_output[j] = wet * df_output[j] + dry * df_input[j];
            }
        }

        ++debug_frame_count_;
        if (debug_frame_count_ == 1 || (debug_frame_count_ % 500) == 0) {
            float input_rms = 0.0f;
            float output_rms = 0.0f;
            for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
                input_rms += df_input[j] * df_input[j];
                output_rms += df_output[j] * df_output[j];
            }
            input_rms = std::sqrt(input_rms / DENOISE_FRAME_SIZE);
            output_rms = std::sqrt(output_rms / DENOISE_FRAME_SIZE);

            fprintf(stderr, "[NoiseAI-Engine] DeepFilter frame #%u: ok=%d, "
                    "input_rms=%.6f, output_rms=%.6f, reduction=%.1fdB, dry_mix=%.2f\n",
                    debug_frame_count_, ok, input_rms, output_rms,
                    (input_rms > 0.00001f) ? 20.0f * std::log10(output_rms / input_rms) : 0.0f,
                    dry);
        }

        // Output is already in [-1, 1] range
        for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
            output_buffer_.push_back(df_output[j]);
        }
    } else {
        // RNNoise: scale float [-1, 1] to int16 range [-32768, 32768]
        float rnn_input[DENOISE_FRAME_SIZE];
        float rnn_output[DENOISE_FRAME_SIZE];

        for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
            rnn_input[j] = input_buffer_[j] * 32768.0f;
        }

        auto* state = static_cast<DenoiseState*>(rnnoise_state_);
        float vad_prob = rnnoise_process_frame(state, rnn_output, rnn_input);

        ++debug_frame_count_;
        if (debug_frame_count_ == 1 || (debug_frame_count_ % 500) == 0) {
            float input_rms = 0.0f;
            for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
                input_rms += rnn_input[j] * rnn_input[j];
            }
            input_rms = std::sqrt(input_rms / DENOISE_FRAME_SIZE);
            float output_rms = 0.0f;
            for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
                output_rms += rnn_output[j] * rnn_output[j];
            }
            output_rms = std::sqrt(output_rms / DENOISE_FRAME_SIZE);

            fprintf(stderr, "[NoiseAI-Engine] RNNoise frame #%u: VAD=%.3f, "
                    "input_rms=%.1f, output_rms=%.1f, reduction=%.1fdB\n",
                    debug_frame_count_, vad_prob, input_rms, output_rms,
                    (input_rms > 0.001f) ? 20.0f * std::log10(output_rms / input_rms) : 0.0f);
        }

        // Scale back to float [-1, 1] range
        for (uint32_t j = 0; j < DENOISE_FRAME_SIZE; ++j) {
            output_buffer_.push_back(rnn_output[j] / 32768.0f);
        }
    }

    // --- Measure elapsed time and update overload state ---
    auto t_elapsed = std::chrono::steady_clock::now() - t_start;
    float elapsed_ms = std::chrono::duration<float, std::milli>(t_elapsed).count();

    if (elapsed_ms > OVERLOAD_THRESHOLD_MS) {
        ++overload_count_;
        if (overload_count_ >= OVERLOAD_TRIGGER_COUNT) {
            in_overload_ = true;
            overload_cooldown_ = OVERLOAD_COOLDOWN_FRAMES;
            fprintf(stderr, "[NoiseAI-Engine] WARNING: Overload detected "
                    "(%u consecutive slow frames, last=%.1fms). "
                    "Falling back to passthrough for %u frames.\n",
                    overload_count_, elapsed_ms, OVERLOAD_COOLDOWN_FRAMES);
        }
    } else {
        // Reset on any fast frame — must be consecutive to trigger
        overload_count_ = 0;
    }

    // Remove processed samples from input buffer
    input_buffer_.erase(input_buffer_.begin(),
                        input_buffer_.begin() + DENOISE_FRAME_SIZE);
}

bool Engine::process_frame(const float* input, float* output, uint32_t num_frames)
{
    if (status_ != EngineStatus::Running) {
        return false;
    }

    // If neither denoiser is available, fall back to passthrough
    bool has_denoiser = rnnoise_state_ != nullptr
                        || (deepfilter_ && deepfilter_->is_loaded());
    if (!has_denoiser) {
        std::memcpy(output, input, num_frames * channels_ * sizeof(float));
        return true;
    }

    // Step 1: Convert interleaved stereo to mono and append to input_buffer_
    for (uint32_t i = 0; i < num_frames; ++i) {
        float mono_sample = 0.0f;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            mono_sample += input[i * channels_ + ch];
        }
        mono_sample /= static_cast<float>(channels_);
        input_buffer_.push_back(mono_sample);
    }

    // Step 2: Process all complete 480-sample chunks through the active denoiser
    while (input_buffer_.size() >= DENOISE_FRAME_SIZE) {
        process_mono_chunk();
    }

    // Step 3: Convert processed mono output back to interleaved stereo.
    const size_t available = output_buffer_.size() - output_read_pos_;

    if (!output_started_) {
        if (available >= OUTPUT_PREFILL_SAMPLES) {
            output_started_ = true;
            fprintf(stderr, "[NoiseAI-Engine] Output prefill complete (%zu samples), "
                    "starting output\n", available);
        }
    }

    if (output_started_ && available >= num_frames) {
        for (uint32_t i = 0; i < num_frames; ++i) {
            float sample = output_buffer_[output_read_pos_ + i];
            sample = std::max(-1.0f, std::min(1.0f, sample));
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                output[i * channels_ + ch] = sample;
            }
        }
        output_read_pos_ += num_frames;

        // Compact the output buffer when we've consumed a reasonable amount
        if (output_read_pos_ > DENOISE_FRAME_SIZE * 2) {
            output_buffer_.erase(output_buffer_.begin(),
                                 output_buffer_.begin() + static_cast<ptrdiff_t>(output_read_pos_));
            output_read_pos_ = 0;
        }
    } else {
        // Not enough processed output yet (initial buffering period).
        std::memset(output, 0, num_frames * channels_ * sizeof(float));
    }

    return true;
}

void Engine::set_mode(NoiseMode mode)
{
    if (mode_ != mode) {
        fprintf(stderr, "[NoiseAI-Engine] Mode changed: %d -> %d\n",
                static_cast<int>(mode_), static_cast<int>(mode));
        mode_ = mode;

        // Reset buffers on mode switch to avoid stale audio from the previous denoiser
        input_buffer_.clear();
        output_buffer_.clear();
        output_read_pos_ = 0;
        output_started_ = false;
        debug_frame_count_ = 0;

        // Reset DeepFilterNet state so it starts fresh and apply new attenuation limit
        if (deepfilter_) {
            deepfilter_->reset_state();
        }
        apply_attenuation_for_mode(mode_);
    }
}

NoiseMode Engine::get_mode() const
{
    return mode_;
}

EngineStatus Engine::get_status() const
{
    return status_;
}

bool Engine::is_overloaded() const
{
    return in_overload_;
}

void Engine::set_dry_mix(float mix)
{
    dry_mix_override_ = mix;
    fprintf(stderr, "[NoiseAI-Engine] Dry mix override set to %.3f%s\n",
            mix, mix < 0.0f ? " (using mode default)" : "");
}

float Engine::get_dry_mix() const
{
    return (dry_mix_override_ >= 0.0f) ? dry_mix_override_ : dry_mix_for_mode(mode_);
}

void Engine::set_attenuation_limit(float db)
{
    atten_lim_override_ = db;
    fprintf(stderr, "[NoiseAI-Engine] Attenuation limit override set to %.1f dB%s\n",
            db, db < 0.0f ? " (using mode default)" : "");
    // Apply immediately if DeepFilter is loaded
    apply_attenuation_for_mode(mode_);
}

float Engine::get_attenuation_limit() const
{
    return (atten_lim_override_ >= 0.0f) ? atten_lim_override_ : 0.0f;
}

} // namespace noise
