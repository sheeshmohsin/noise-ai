#pragma once
#include <aspl/IORequestHandler.hpp>
#include <aspl/ControlRequestHandler.hpp>
#include <memory>

namespace noise {
class SharedRingBuffer;
}

class IOHandler : public aspl::IORequestHandler, public aspl::ControlRequestHandler {
public:
    IOHandler();
    ~IOHandler() override;

    // IORequestHandler
    void OnReadClientInput(
        const std::shared_ptr<aspl::Client>& client,
        const std::shared_ptr<aspl::Stream>& stream,
        Float64 zeroTimestamp,
        Float64 timestamp,
        void* bytes,
        UInt32 bytesCount) override;

    OSStatus OnStartIO() override;
    void OnStopIO() override;

private:
    // Shared memory ring buffer for receiving audio from the app
    std::unique_ptr<noise::SharedRingBuffer> shared_buffer_;

    // Try to connect to the shared memory segment.
    // Returns true if connected (or already connected).
    bool ensureConnected();

    static constexpr double kSampleRate = 48000.0;
    static constexpr uint32_t kChannels = 2;

    // Throttle reconnection attempts (avoid syscalls on every audio callback)
    int reconnect_countdown_ = 0;

    // State tracking for transition-only logging (avoids real-time thread spam)
    bool was_connected_ = false;
    bool was_underrun_ = false;
    // Tracks whether we ever connected during this IO session.
    // Used to pick fast vs slow retry throttle in ensureConnected().
    bool was_ever_connected_ = false;

    // Heartbeat-based stale producer detection.
    // If the heartbeat hasn't changed for kStaleThreshold consecutive callbacks,
    // the producer is presumed dead and we disconnect to allow reconnection.
    uint64_t last_heartbeat_ = 0;
    int stale_count_ = 0;
    static constexpr int kStaleThreshold = 50;  // ~500ms at 48kHz/512-frame callbacks
};
