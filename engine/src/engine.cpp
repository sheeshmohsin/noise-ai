#include "noise/engine.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cmath>

extern "C" {
#include "rnnoise.h"
}

// RNNoise processes exactly 480 mono samples per frame (10ms at 48kHz)
static constexpr uint32_t RNNOISE_FRAME_SIZE = 480;

// Minimum output buffer level before we start reading from it.
// This prevents periodic underruns caused by the mismatch between
// RNNoise's 480-sample frame size and the AUHAL callback size (often 128).
// Two RNNoise frames (~20ms at 48kHz) provides enough cushion.
static constexpr uint32_t OUTPUT_PREFILL_SAMPLES = RNNOISE_FRAME_SIZE * 2;

namespace noise {

Engine::Engine()
    : mode_(NoiseMode::Balanced)
    , status_(EngineStatus::Stopped)
{
}

Engine::~Engine()
{
    shutdown();
}

bool Engine::init(const AudioFormat& format)
{
    // Clean up any previous state
    if (rnnoise_state_) {
        rnnoise_destroy(static_cast<DenoiseState*>(rnnoise_state_));
        rnnoise_state_ = nullptr;
    }

    channels_ = format.channels;

    // Create RNNoise state with built-in model
    rnnoise_state_ = rnnoise_create(nullptr);
    if (!rnnoise_state_) {
        fprintf(stderr, "[NoiseAI-Engine] ERROR: rnnoise_create() returned null!\n");
        status_ = EngineStatus::Error;
        return false;
    }
    fprintf(stderr, "[NoiseAI-Engine] RNNoise state created successfully (channels=%u)\n",
            channels_);

    // Clear buffers
    input_buffer_.clear();
    output_buffer_.clear();
    output_read_pos_ = 0;
    output_started_ = false;

    // Reserve reasonable capacity to avoid reallocations during processing
    input_buffer_.reserve(RNNOISE_FRAME_SIZE * 4);
    output_buffer_.reserve(RNNOISE_FRAME_SIZE * 4);

    debug_frame_count_ = 0;

    status_ = EngineStatus::Running;
    return true;
}

void Engine::shutdown()
{
    if (rnnoise_state_) {
        rnnoise_destroy(static_cast<DenoiseState*>(rnnoise_state_));
        rnnoise_state_ = nullptr;
    }
    input_buffer_.clear();
    output_buffer_.clear();
    output_read_pos_ = 0;
    output_started_ = false;
    status_ = EngineStatus::Stopped;
}

bool Engine::process_frame(const float* input, float* output, uint32_t num_frames)
{
    if (status_ != EngineStatus::Running) {
        return false;
    }

    // If RNNoise is not available, fall back to passthrough
    if (!rnnoise_state_) {
        std::memcpy(output, input, num_frames * channels_ * sizeof(float));
        return true;
    }

    auto* state = static_cast<DenoiseState*>(rnnoise_state_);

    // Step 1: Convert interleaved stereo to mono and append to input_buffer_
    // Each "frame" has `channels_` samples interleaved.
    for (uint32_t i = 0; i < num_frames; ++i) {
        float mono_sample = 0.0f;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            mono_sample += input[i * channels_ + ch];
        }
        mono_sample /= static_cast<float>(channels_);
        input_buffer_.push_back(mono_sample);
    }

    // Step 2: Process all complete 480-sample chunks through RNNoise
    while (input_buffer_.size() >= RNNOISE_FRAME_SIZE) {
        // Scale float [-1, 1] to int16 range [-32768, 32768] as RNNoise expects
        float rnn_input[RNNOISE_FRAME_SIZE];
        float rnn_output[RNNOISE_FRAME_SIZE];

        for (uint32_t j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
            rnn_input[j] = input_buffer_[j] * 32768.0f;
        }

        // Process through RNNoise
        float vad_prob = rnnoise_process_frame(state, rnn_output, rnn_input);

        // Debug logging: log the first frame and then every ~5 seconds
        // (at 48kHz with 480 samples per frame, ~100 frames/sec, so every 500 frames)
        ++debug_frame_count_;
        if (debug_frame_count_ == 1 || (debug_frame_count_ % 500) == 0) {
            // Compute input RMS (in int16 scale)
            float input_rms = 0.0f;
            for (uint32_t j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                input_rms += rnn_input[j] * rnn_input[j];
            }
            input_rms = std::sqrt(input_rms / RNNOISE_FRAME_SIZE);

            // Compute output RMS (in int16 scale)
            float output_rms = 0.0f;
            for (uint32_t j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                output_rms += rnn_output[j] * rnn_output[j];
            }
            output_rms = std::sqrt(output_rms / RNNOISE_FRAME_SIZE);

            fprintf(stderr, "[NoiseAI-Engine] RNNoise frame #%u: VAD=%.3f, "
                    "input_rms=%.1f, output_rms=%.1f, reduction=%.1fdB\n",
                    debug_frame_count_, vad_prob, input_rms, output_rms,
                    (input_rms > 0.001f) ? 20.0f * std::log10(output_rms / input_rms) : 0.0f);
        }

        // Scale back to float [-1, 1] range and append to output buffer
        for (uint32_t j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
            output_buffer_.push_back(rnn_output[j] / 32768.0f);
        }

        // Remove processed samples from input buffer
        input_buffer_.erase(input_buffer_.begin(),
                            input_buffer_.begin() + RNNOISE_FRAME_SIZE);
    }

    // Step 3: Convert processed mono output back to interleaved stereo.
    // We need exactly num_frames mono samples from the output buffer.
    //
    // To avoid periodic underruns caused by the mismatch between
    // RNNoise's 480-sample frame and the AUHAL callback size, we
    // wait until OUTPUT_PREFILL_SAMPLES have accumulated before we
    // start draining the output buffer.
    const size_t available = output_buffer_.size() - output_read_pos_;

    if (!output_started_) {
        if (available >= OUTPUT_PREFILL_SAMPLES) {
            output_started_ = true;
            fprintf(stderr, "[NoiseAI-Engine] Output prefill complete (%zu samples), "
                    "starting output\n", available);
        }
    }

    if (output_started_ && available >= num_frames) {
        // We have enough processed samples
        for (uint32_t i = 0; i < num_frames; ++i) {
            float sample = output_buffer_[output_read_pos_ + i];
            // Clamp to prevent any overflow
            sample = std::max(-1.0f, std::min(1.0f, sample));
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                output[i * channels_ + ch] = sample;
            }
        }
        output_read_pos_ += num_frames;

        // Compact the output buffer when we've consumed a reasonable amount
        if (output_read_pos_ > RNNOISE_FRAME_SIZE * 2) {
            output_buffer_.erase(output_buffer_.begin(),
                                 output_buffer_.begin() + static_cast<ptrdiff_t>(output_read_pos_));
            output_read_pos_ = 0;
        }
    } else {
        // Not enough processed output yet (initial buffering period).
        // Output silence to avoid glitches while the buffer fills up.
        std::memset(output, 0, num_frames * channels_ * sizeof(float));
    }

    return true;
}

void Engine::set_mode(NoiseMode mode)
{
    mode_ = mode;
}

NoiseMode Engine::get_mode() const
{
    return mode_;
}

EngineStatus Engine::get_status() const
{
    return status_;
}

} // namespace noise
