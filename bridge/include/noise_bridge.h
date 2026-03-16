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

// Shared memory ring buffer for IPC between app and driver
int noise_shm_create(NoiseEngineHandle handle, uint32_t sample_rate, uint32_t channels);
void noise_shm_destroy(NoiseEngineHandle handle);
int noise_shm_write(NoiseEngineHandle handle, const float* data, uint32_t num_samples);
int noise_shm_is_active(NoiseEngineHandle handle);

#ifdef __cplusplus
}
#endif
