#include "io_handler.hpp"
#include "noise/shared_ring_buffer.h"
#include <cstdio>
#include <cstring>

IOHandler::IOHandler() = default;

// Destructor must be defined in the .cpp where SharedRingBuffer is a complete type
IOHandler::~IOHandler() = default;

OSStatus IOHandler::OnStartIO() {
    fprintf(stderr, "[NoiseAI-Driver] OnStartIO called\n");
    // Always disconnect first to ensure we get a fresh shared memory mapping.
    // This handles the case where the app was restarted while IO was still active.
    shared_buffer_.reset();
    was_connected_ = false;
    was_underrun_ = false;
    was_ever_connected_ = false;
    reconnect_countdown_ = 0;
    last_heartbeat_ = 0;
    stale_count_ = 0;

    // Try to connect to the (possibly new) shared memory segment
    if (ensureConnected()) {
        fprintf(stderr, "[NoiseAI-Driver] Connected to shared memory on start\n");
    } else {
        fprintf(stderr, "[NoiseAI-Driver] Shared memory not available on start (will retry on each read)\n");
    }
    return kAudioHardwareNoError;
}

void IOHandler::OnStopIO() {
    fprintf(stderr, "[NoiseAI-Driver] OnStopIO called\n");
    // Release the shared memory mapping
    shared_buffer_.reset();
    was_connected_ = false;
    was_underrun_ = false;
    reconnect_countdown_ = 0;
    last_heartbeat_ = 0;
    stale_count_ = 0;
}

bool IOHandler::ensureConnected() {
    if (shared_buffer_ && shared_buffer_->is_valid()) {
        return true;
    }

    // Throttle reconnection attempts to avoid doing shm_open syscalls
    // on every audio callback (~hundreds of times per second).
    if (reconnect_countdown_ > 0) {
        --reconnect_countdown_;
        return false;
    }
    // At 48kHz with 512-frame buffers, ~94 callbacks/sec.
    // Use a short throttle (~50ms) during the initial connection phase
    // to avoid a multi-second delay when the driver starts before the
    // app has created shared memory.  Once connected (and later
    // disconnected), fall back to a longer interval.
    reconnect_countdown_ = was_ever_connected_ ? 47 : 5;

    // Try to open the shared memory as consumer (create=false).
    // If the app hasn't created it yet, this will fail silently and
    // we'll output silence until it becomes available.
    shared_buffer_ = std::make_unique<noise::SharedRingBuffer>(
        "/com.noiseai.audio", 0, kChannels,
        static_cast<uint32_t>(kSampleRate), false);

    if (!shared_buffer_->is_valid()) {
        shared_buffer_.reset();
        return false;
    }

    // Discard any audio that the producer buffered before we connected.
    // Without this, the driver would read stale data from seconds ago,
    // causing the first ~2s of a recording to be shifted/delayed.
    shared_buffer_->flush();

    // Successfully connected - reset countdown and remember we connected
    reconnect_countdown_ = 0;
    was_ever_connected_ = true;
    return true;
}

void IOHandler::OnReadClientInput(
    const std::shared_ptr<aspl::Client>& /*client*/,
    const std::shared_ptr<aspl::Stream>& /*stream*/,
    Float64 /*zeroTimestamp*/,
    Float64 /*timestamp*/,
    void* bytes,
    UInt32 bytesCount)
{
    const UInt32 numChannels = kChannels;
    const UInt32 bytesPerSample = sizeof(Float32);
    const UInt32 numFrames = bytesCount / (numChannels * bytesPerSample);
    const UInt32 totalSamples = numFrames * numChannels;

    Float32* output = static_cast<Float32*>(bytes);

    // Try to connect if not connected yet
    if (!ensureConnected()) {
        // No shared memory available - output silence
        if (was_connected_) {
            fprintf(stderr, "[NoiseAI-Driver] Lost connection to shared memory\n");
            was_connected_ = false;
        }
        std::memset(output, 0, bytesCount);
        return;
    }

    if (!was_connected_) {
        fprintf(stderr, "[NoiseAI-Driver] Connected to shared memory (cap=%zu, active=%d)\n",
                shared_buffer_->available_read() + shared_buffer_->available_write(),
                shared_buffer_->is_active() ? 1 : 0);
        was_connected_ = true;
        was_underrun_ = false;
    }

    // Check if the producer is still active.
    // If the producer has set active=0 (clean shutdown) or the heartbeat
    // has gone stale (crash / app restarted), disconnect so that
    // ensureConnected() will pick up a new shared memory segment.
    if (!shared_buffer_->is_active()) {
        fprintf(stderr, "[NoiseAI-Driver] Producer inactive, disconnecting to allow reconnect\n");
        shared_buffer_.reset();
        was_connected_ = false;
        last_heartbeat_ = 0;
        stale_count_ = 0;
        std::memset(output, 0, bytesCount);
        return;
    }

    // Heartbeat-based stale detection: if the heartbeat counter hasn't
    // advanced for kStaleThreshold consecutive callbacks, the producer
    // is presumed dead (e.g. crashed without setting active=0).
    {
        const uint64_t hb = shared_buffer_->get_heartbeat();
        if (hb == last_heartbeat_) {
            ++stale_count_;
            if (stale_count_ >= kStaleThreshold) {
                fprintf(stderr, "[NoiseAI-Driver] Heartbeat stale for %d callbacks, "
                        "disconnecting to allow reconnect\n", stale_count_);
                shared_buffer_.reset();
                was_connected_ = false;
                last_heartbeat_ = 0;
                stale_count_ = 0;
                std::memset(output, 0, bytesCount);
                return;
            }
        } else {
            last_heartbeat_ = hb;
            stale_count_ = 0;
        }
    }

    // Try to read from the shared ring buffer
    if (!shared_buffer_->read(output, totalSamples)) {
        // Not enough data (underrun) - output silence
        if (!was_underrun_) {
            fprintf(stderr, "[NoiseAI-Driver] Underrun: need %u samples, available %zu\n",
                    totalSamples, shared_buffer_->available_read());
            was_underrun_ = true;
        }
        std::memset(output, 0, bytesCount);
    } else {
        if (was_underrun_) {
            fprintf(stderr, "[NoiseAI-Driver] Recovered from underrun\n");
            was_underrun_ = false;
        }
    }
}
