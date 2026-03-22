#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* NoiseEngineHandle;

NoiseEngineHandle noise_engine_create(uint32_t sample_rate, uint32_t channels, uint32_t frames_per_buffer);
void noise_engine_destroy(NoiseEngineHandle handle);
int noise_engine_start(NoiseEngineHandle handle);
void noise_engine_stop(NoiseEngineHandle handle);
int noise_engine_process(NoiseEngineHandle handle, const float* input, float* output, uint32_t num_frames);
void noise_engine_set_mode(NoiseEngineHandle handle, int mode);  // 0=CpuSaver, 1=Balanced, 2=MaxQuality
int noise_engine_get_mode(NoiseEngineHandle handle);
int noise_engine_get_status(NoiseEngineHandle handle);  // 0=Stopped, 1=Running, 2=Error

// Dry mix override: set to -1.0 to reset to mode default
void noise_engine_set_dry_mix(NoiseEngineHandle handle, float mix);
float noise_engine_get_dry_mix(NoiseEngineHandle handle);

// Attenuation limit override in dB: set to -1.0 to reset to mode default
void noise_engine_set_attenuation_limit(NoiseEngineHandle handle, float db);
float noise_engine_get_attenuation_limit(NoiseEngineHandle handle);

// Process audio through RNNoise and write denoised output to shared memory.
// input: interleaved stereo float32 audio
// num_frames: number of frames (each frame = 2 samples for stereo)
int noise_engine_process_and_write(NoiseEngineHandle handle, const float* input, uint32_t num_frames);

// Returns 1 if the engine is in overload passthrough mode, 0 otherwise.
int noise_engine_is_overloaded(NoiseEngineHandle handle);

// Shared memory ring buffer for IPC between app and driver
int noise_shm_create(NoiseEngineHandle handle, uint32_t sample_rate, uint32_t channels);
void noise_shm_destroy(NoiseEngineHandle handle);
int noise_shm_write(NoiseEngineHandle handle, const float* data, uint32_t num_samples);
int noise_shm_is_active(NoiseEngineHandle handle);

#ifdef __cplusplus
}
#endif
