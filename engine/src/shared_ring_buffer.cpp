#include "noise/shared_ring_buffer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace noise {

SharedRingBuffer::SharedRingBuffer(const char* name, size_t capacity,
                                   uint32_t channels, uint32_t sample_rate,
                                   bool create)
    : is_creator_(create)
{
    // Store name for cleanup
    std::strncpy(shm_name_, name, sizeof(shm_name_) - 1);
    shm_name_[sizeof(shm_name_) - 1] = '\0';

    shm_size_ = sizeof(SharedRingBufferHeader) + capacity * sizeof(float);

    if (create) {
        // Producer: create shared memory segment
        // Unlink first to ensure a clean start
        shm_unlink(name);

        shm_fd_ = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (shm_fd_ < 0) {
            fprintf(stderr, "[NoiseAI-SHM] Producer: shm_open CREATE failed for '%s': %s\n",
                    name, strerror(errno));
            return;
        }
        fprintf(stderr, "[NoiseAI-SHM] Producer: created shm '%s', capacity=%zu samples, size=%zu bytes\n",
                name, capacity, shm_size_);

        if (ftruncate(shm_fd_, static_cast<off_t>(shm_size_)) != 0) {
            close(shm_fd_);
            shm_fd_ = -1;
            shm_unlink(name);
            return;
        }
    } else {
        // Consumer: open existing shared memory segment
        shm_fd_ = shm_open(name, O_RDWR, 0666);
        if (shm_fd_ < 0) {
            // Don't log every failure -- the producer may not have started yet.
            return;
        }
        fprintf(stderr, "[NoiseAI-SHM] Consumer: opened shm '%s'\n", name);

        // Get actual size
        struct stat sb;
        if (fstat(shm_fd_, &sb) != 0) {
            close(shm_fd_);
            shm_fd_ = -1;
            return;
        }
        shm_size_ = static_cast<size_t>(sb.st_size);
    }

    // Map the shared memory
    shm_ptr_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                     shm_fd_, 0);
    if (shm_ptr_ == MAP_FAILED) {
        shm_ptr_ = nullptr;
        close(shm_fd_);
        shm_fd_ = -1;
        if (create) {
            shm_unlink(name);
        }
        return;
    }

    header_ = static_cast<SharedRingBufferHeader*>(shm_ptr_);
    data_ = reinterpret_cast<float*>(
        static_cast<char*>(shm_ptr_) + sizeof(SharedRingBufferHeader));

    if (create) {
        // Initialize the header
        new (&header_->write_pos) std::atomic<uint64_t>(0);
        new (&header_->read_pos) std::atomic<uint64_t>(0);
        header_->capacity = static_cast<uint32_t>(capacity);
        header_->channels = channels;
        header_->sample_rate = sample_rate;
        header_->active = 1;
        fprintf(stderr, "[NoiseAI-SHM] Producer: header initialized (cap=%u, ch=%u, sr=%u, active=1)\n",
                header_->capacity, header_->channels, header_->sample_rate);
    } else {
        fprintf(stderr, "[NoiseAI-SHM] Consumer: mapped OK (cap=%u, ch=%u, sr=%u, active=%u)\n",
                header_->capacity, header_->channels, header_->sample_rate, header_->active);
    }
}

SharedRingBuffer::~SharedRingBuffer()
{
    if (is_creator_ && header_) {
        header_->active = 0;
    }

    if (shm_ptr_) {
        munmap(shm_ptr_, shm_size_);
        shm_ptr_ = nullptr;
    }

    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_fd_ = -1;
    }

    if (is_creator_) {
        shm_unlink(shm_name_);
    }
}

bool SharedRingBuffer::write(const float* data, size_t num_samples)
{
    if (!header_ || !data_) {
        // Log once using a static flag to avoid spamming
        static bool logged_invalid = false;
        if (!logged_invalid) {
            fprintf(stderr, "[NoiseAI-SHM] write: buffer not valid (header=%p, data=%p)\n",
                    (void*)header_, (void*)data_);
            logged_invalid = true;
        }
        return false;
    }

    const uint64_t w = header_->write_pos.load(std::memory_order_relaxed);
    const uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    const uint32_t cap = header_->capacity;

    uint64_t available = cap - (w - r);
    if (num_samples > available) {
        // Log overflow once
        static bool logged_overflow = false;
        if (!logged_overflow) {
            fprintf(stderr, "[NoiseAI-SHM] write: overflow (need %zu, avail %llu, cap %u)\n",
                    num_samples, available, cap);
            logged_overflow = true;
        }
        return false;
    }

    const size_t w_index = static_cast<size_t>(w % cap);
    const size_t first_chunk = std::min(num_samples, static_cast<size_t>(cap) - w_index);
    const size_t second_chunk = num_samples - first_chunk;

    std::memcpy(data_ + w_index, data, first_chunk * sizeof(float));
    if (second_chunk > 0) {
        std::memcpy(data_, data + first_chunk, second_chunk * sizeof(float));
    }

    header_->write_pos.store(w + num_samples, std::memory_order_release);
    return true;
}

bool SharedRingBuffer::read(float* data, size_t num_samples)
{
    if (!header_ || !data_) return false;

    const uint64_t r = header_->read_pos.load(std::memory_order_relaxed);
    const uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    const uint32_t cap = header_->capacity;

    uint64_t available = w - r;
    if (num_samples > available) {
        return false;
    }

    const size_t r_index = static_cast<size_t>(r % cap);
    const size_t first_chunk = std::min(num_samples, static_cast<size_t>(cap) - r_index);
    const size_t second_chunk = num_samples - first_chunk;

    std::memcpy(data, data_ + r_index, first_chunk * sizeof(float));
    if (second_chunk > 0) {
        std::memcpy(data + first_chunk, data_, second_chunk * sizeof(float));
    }

    header_->read_pos.store(r + num_samples, std::memory_order_release);
    return true;
}

void SharedRingBuffer::flush()
{
    if (!header_) return;
    // Snap read_pos to the current write_pos so all stale data is discarded.
    const uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    header_->read_pos.store(w, std::memory_order_release);
    fprintf(stderr, "[NoiseAI-SHM] flush: discarded stale data, read_pos = write_pos = %llu\n", w);
}

size_t SharedRingBuffer::available_read() const
{
    if (!header_) return 0;
    const uint64_t w = header_->write_pos.load(std::memory_order_acquire);
    const uint64_t r = header_->read_pos.load(std::memory_order_relaxed);
    return static_cast<size_t>(w - r);
}

size_t SharedRingBuffer::available_write() const
{
    if (!header_) return 0;
    const uint64_t w = header_->write_pos.load(std::memory_order_relaxed);
    const uint64_t r = header_->read_pos.load(std::memory_order_acquire);
    return static_cast<size_t>(header_->capacity - (w - r));
}

bool SharedRingBuffer::is_active() const
{
    if (!header_) return false;
    return header_->active != 0;
}

void SharedRingBuffer::set_active(bool active)
{
    if (header_) {
        header_->active = active ? 1 : 0;
    }
}

} // namespace noise
