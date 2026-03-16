#pragma once

#include "noise/types.h"
#include <cstdint>

namespace noise {

class Engine {
public:
    Engine();
    ~Engine();

    bool init(const AudioFormat& format);
    void shutdown();

    bool process_frame(const float* input, float* output, uint32_t num_frames);

    void set_mode(NoiseMode mode);
    NoiseMode get_mode() const;

    EngineStatus get_status() const;

private:
    NoiseMode mode_;
    EngineStatus status_;
};

} // namespace noise
