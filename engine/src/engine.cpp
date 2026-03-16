#include "noise/engine.h"
#include <cstring>

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

bool Engine::init(const AudioFormat& /*format*/)
{
    status_ = EngineStatus::Running;
    return true;
}

void Engine::shutdown()
{
    status_ = EngineStatus::Stopped;
}

bool Engine::process_frame(const float* input, float* output, uint32_t num_frames)
{
    if (status_ != EngineStatus::Running) {
        return false;
    }
    // Passthrough: copy input to output
    std::memcpy(output, input, num_frames * sizeof(float));
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
