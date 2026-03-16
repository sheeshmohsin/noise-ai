#pragma once

#include <cstdint>

namespace noise {

enum class NoiseMode {
    CpuSaver,
    Balanced,
    MaxQuality
};

enum class EngineStatus {
    Stopped,
    Running,
    Error
};

struct AudioFormat {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frames_per_buffer;
};

} // namespace noise
