#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace noise {

class SPSCRingBuffer {
public:
    explicit SPSCRingBuffer(size_t capacity);
    ~SPSCRingBuffer();

    bool write(const float* data, size_t num_samples);
    bool read(float* data, size_t num_samples);

    size_t available_read() const;
    size_t available_write() const;

    void reset();

private:
    std::vector<float> buffer_;
    size_t capacity_;
    alignas(64) std::atomic<size_t> write_pos_;
    alignas(64) std::atomic<size_t> read_pos_;
};

} // namespace noise
