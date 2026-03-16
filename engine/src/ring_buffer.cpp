#include "noise/ring_buffer.h"
#include <algorithm>
#include <cstring>

namespace noise {

SPSCRingBuffer::SPSCRingBuffer(size_t capacity)
    : buffer_(capacity)
    , capacity_(capacity)
    , write_pos_(0)
    , read_pos_(0)
{
}

SPSCRingBuffer::~SPSCRingBuffer() = default;

bool SPSCRingBuffer::write(const float* data, size_t num_samples)
{
    const size_t w = write_pos_.load(std::memory_order_relaxed);
    const size_t r = read_pos_.load(std::memory_order_acquire);

    size_t available = capacity_ - (w - r);
    if (num_samples > available) {
        return false;
    }

    const size_t w_index = w % capacity_;
    const size_t first_chunk = std::min(num_samples, capacity_ - w_index);
    const size_t second_chunk = num_samples - first_chunk;

    std::memcpy(buffer_.data() + w_index, data, first_chunk * sizeof(float));
    if (second_chunk > 0) {
        std::memcpy(buffer_.data(), data + first_chunk, second_chunk * sizeof(float));
    }

    write_pos_.store(w + num_samples, std::memory_order_release);
    return true;
}

bool SPSCRingBuffer::read(float* data, size_t num_samples)
{
    const size_t r = read_pos_.load(std::memory_order_relaxed);
    const size_t w = write_pos_.load(std::memory_order_acquire);

    size_t available = w - r;
    if (num_samples > available) {
        return false;
    }

    const size_t r_index = r % capacity_;
    const size_t first_chunk = std::min(num_samples, capacity_ - r_index);
    const size_t second_chunk = num_samples - first_chunk;

    std::memcpy(data, buffer_.data() + r_index, first_chunk * sizeof(float));
    if (second_chunk > 0) {
        std::memcpy(data + first_chunk, buffer_.data(), second_chunk * sizeof(float));
    }

    read_pos_.store(r + num_samples, std::memory_order_release);
    return true;
}

size_t SPSCRingBuffer::available_read() const
{
    const size_t w = write_pos_.load(std::memory_order_acquire);
    const size_t r = read_pos_.load(std::memory_order_relaxed);
    return w - r;
}

size_t SPSCRingBuffer::available_write() const
{
    const size_t w = write_pos_.load(std::memory_order_relaxed);
    const size_t r = read_pos_.load(std::memory_order_acquire);
    return capacity_ - (w - r);
}

void SPSCRingBuffer::reset()
{
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
}

} // namespace noise
