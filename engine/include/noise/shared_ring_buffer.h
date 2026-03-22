#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace noise {

// Layout of the shared memory region.
// Placed at the start of the mmap'd area, followed by float audio data.
struct SharedRingBufferHeader {
    alignas(64) std::atomic<uint64_t> write_pos;
    alignas(64) std::atomic<uint64_t> read_pos;
    uint32_t capacity;       // in samples (float32)
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t active;         // 1 if producer is active, 0 if not
    alignas(64) std::atomic<uint64_t> heartbeat;  // incremented by producer on every write
};

class SharedRingBuffer {
public:
    // Create or open shared memory segment.
    // name: POSIX shm name (e.g., "/com.noiseai.audio")
    // capacity: number of float samples the buffer holds
    // channels: number of audio channels
    // sample_rate: audio sample rate
    // create: true = create (producer/app), false = open existing (consumer/driver)
    SharedRingBuffer(const char* name, size_t capacity, uint32_t channels,
                     uint32_t sample_rate, bool create);
    ~SharedRingBuffer();

    // Non-copyable, non-movable
    SharedRingBuffer(const SharedRingBuffer&) = delete;
    SharedRingBuffer& operator=(const SharedRingBuffer&) = delete;

    // Producer (app) writes captured audio
    bool write(const float* data, size_t num_samples);

    // Consumer (driver) reads processed audio
    bool read(float* data, size_t num_samples);

    // Discard all buffered data by advancing read_pos to write_pos.
    // Called by the consumer when it first connects so it starts
    // reading fresh audio instead of stale data that accumulated
    // while no consumer was attached.
    void flush();

    size_t available_read() const;
    size_t available_write() const;

    bool is_active() const;
    void set_active(bool active);

    // Returns the current heartbeat counter (incremented on every write).
    // Used by the consumer to detect a stale/dead producer.
    uint64_t get_heartbeat() const;

    bool is_valid() const { return shm_ptr_ != nullptr; }

private:
    SharedRingBufferHeader* header_ = nullptr;
    float* data_ = nullptr;
    void* shm_ptr_ = nullptr;
    size_t shm_size_ = 0;
    int shm_fd_ = -1;
    bool is_creator_ = false;
    char shm_name_[256] = {};
};

} // namespace noise
