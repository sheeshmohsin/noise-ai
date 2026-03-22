#include "noise_bridge.h"
#include "noise/engine.h"
#include "noise/shared_ring_buffer.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdio>

// Context struct that holds both the Engine and shared memory buffer
struct NoiseEngineContext {
    noise::Engine engine;
    std::unique_ptr<noise::SharedRingBuffer> shared_buffer;
};

extern "C" {

NoiseEngineHandle noise_engine_create(uint32_t sample_rate, uint32_t channels, uint32_t frames_per_buffer)
{
    auto* ctx = new NoiseEngineContext();
    noise::AudioFormat format{};
    format.sample_rate = sample_rate;
    format.channels = channels;
    format.frames_per_buffer = frames_per_buffer;
    ctx->engine.init(format);
    return static_cast<NoiseEngineHandle>(ctx);
}

void noise_engine_destroy(NoiseEngineHandle handle)
{
    if (handle) {
        auto* ctx = static_cast<NoiseEngineContext*>(handle);
        ctx->engine.shutdown();
        ctx->shared_buffer.reset();
        delete ctx;
    }
}

int noise_engine_start(NoiseEngineHandle handle)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    // Engine was already initialized in noise_engine_create().
    // Just set the status to Running without re-initializing.
    noise::AudioFormat format{};
    format.sample_rate = 48000;
    format.channels = 2;
    format.frames_per_buffer = 128;
    return ctx->engine.init(format) ? 1 : 0;
}

void noise_engine_stop(NoiseEngineHandle handle)
{
    if (!handle) return;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    ctx->engine.shutdown();
}

int noise_engine_process(NoiseEngineHandle handle, const float* input, float* output, uint32_t num_frames)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return ctx->engine.process_frame(input, output, num_frames) ? 1 : 0;
}

void noise_engine_set_mode(NoiseEngineHandle handle, int mode)
{
    if (!handle) return;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    ctx->engine.set_mode(static_cast<noise::NoiseMode>(mode));
}

int noise_engine_get_mode(NoiseEngineHandle handle)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return static_cast<int>(ctx->engine.get_mode());
}

int noise_engine_get_status(NoiseEngineHandle handle)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return static_cast<int>(ctx->engine.get_status());
}

void noise_engine_set_dry_mix(NoiseEngineHandle handle, float mix)
{
    if (!handle) return;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    ctx->engine.set_dry_mix(mix);
}

float noise_engine_get_dry_mix(NoiseEngineHandle handle)
{
    if (!handle) return 0.0f;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return ctx->engine.get_dry_mix();
}

void noise_engine_set_attenuation_limit(NoiseEngineHandle handle, float db)
{
    if (!handle) return;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    ctx->engine.set_attenuation_limit(db);
}

float noise_engine_get_attenuation_limit(NoiseEngineHandle handle)
{
    if (!handle) return 0.0f;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return ctx->engine.get_attenuation_limit();
}

int noise_engine_is_overloaded(NoiseEngineHandle handle)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return ctx->engine.is_overloaded() ? 1 : 0;
}

int noise_engine_load_deepfilter_model(NoiseEngineHandle handle, const char* model_path)
{
    if (!handle || !model_path) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    return ctx->engine.load_deepfilter_model(std::string(model_path)) ? 1 : 0;
}


int noise_engine_process_and_write(NoiseEngineHandle handle, const float* input, uint32_t num_frames)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    if (!ctx->shared_buffer || !ctx->shared_buffer->is_valid()) {
        // Log once if shared buffer is missing
        static bool logged_no_shm = false;
        if (!logged_no_shm) {
            fprintf(stderr, "[NoiseAI-Bridge] process_and_write: shared buffer not valid\n");
            logged_no_shm = true;
        }
        return 0;
    }

    const uint32_t channels = 2;
    const uint32_t total_samples = num_frames * channels;

    // Allocate a temporary buffer for denoised output.
    // For real-time safety this could be pre-allocated, but the engine's
    // internal buffering already handles the heavy lifting.
    // Use a thread-local buffer to avoid per-call allocations.
    thread_local std::vector<float> output_buf;
    if (output_buf.size() < total_samples) {
        output_buf.resize(total_samples);
    }

    // Process through RNNoise
    if (!ctx->engine.process_frame(input, output_buf.data(), num_frames)) {
        static bool logged_process_fail = false;
        if (!logged_process_fail) {
            fprintf(stderr, "[NoiseAI-Bridge] process_and_write: process_frame returned false "
                    "(engine status=%d)\n",
                    static_cast<int>(ctx->engine.get_status()));
            logged_process_fail = true;
        }
        return 0;
    }

    // Write denoised audio to shared memory
    bool write_ok = ctx->shared_buffer->write(output_buf.data(), total_samples);

    // Log first successful write
    static bool logged_first_write = false;
    if (!logged_first_write && write_ok) {
        fprintf(stderr, "[NoiseAI-Bridge] process_and_write: first successful write "
                "(%u frames, %u samples)\n", num_frames, total_samples);
        logged_first_write = true;
    }

    return write_ok ? 1 : 0;
}

// Shared memory functions

int noise_shm_create(NoiseEngineHandle handle, uint32_t sample_rate, uint32_t channels)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);

    // ~100ms of audio at the given sample rate and channel count.
    // Kept small to minimise latency; the consumer flushes stale data on
    // connect anyway, so a large buffer only adds unnecessary delay.
    const size_t capacity = static_cast<size_t>(sample_rate * channels / 10);

    ctx->shared_buffer = std::make_unique<noise::SharedRingBuffer>(
        "/com.noiseai.audio", capacity, channels, sample_rate, true);

    return ctx->shared_buffer->is_valid() ? 1 : 0;
}

void noise_shm_destroy(NoiseEngineHandle handle)
{
    if (!handle) return;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    ctx->shared_buffer.reset();
}

int noise_shm_write(NoiseEngineHandle handle, const float* data, uint32_t num_samples)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    if (!ctx->shared_buffer || !ctx->shared_buffer->is_valid()) return 0;
    return ctx->shared_buffer->write(data, num_samples) ? 1 : 0;
}

int noise_shm_is_active(NoiseEngineHandle handle)
{
    if (!handle) return 0;
    auto* ctx = static_cast<NoiseEngineContext*>(handle);
    if (!ctx->shared_buffer) return 0;
    return ctx->shared_buffer->is_active() ? 1 : 0;
}

} // extern "C"
