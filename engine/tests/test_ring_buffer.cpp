#include "noise/ring_buffer.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define ASSERT(cond, msg)                                         \
    do {                                                          \
        if (!(cond)) {                                            \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
            return 1;                                             \
        }                                                        \
    } while (0)

int main()
{
    constexpr size_t kCapacity = 1024;
    constexpr size_t kNumSamples = 128;

    noise::SPSCRingBuffer rb(kCapacity);

    // Initially empty
    ASSERT(rb.available_read() == 0, "initial available_read should be 0");
    ASSERT(rb.available_write() == kCapacity, "initial available_write should be capacity");

    // Write test data
    float write_buf[kNumSamples];
    for (size_t i = 0; i < kNumSamples; ++i) {
        write_buf[i] = static_cast<float>(i) * 0.1f;
    }

    bool ok = rb.write(write_buf, kNumSamples);
    ASSERT(ok, "write should succeed");
    ASSERT(rb.available_read() == kNumSamples, "available_read should be 128 after write");
    ASSERT(rb.available_write() == kCapacity - kNumSamples, "available_write should decrease");

    // Read data back
    float read_buf[kNumSamples];
    ok = rb.read(read_buf, kNumSamples);
    ASSERT(ok, "read should succeed");
    ASSERT(rb.available_read() == 0, "available_read should be 0 after full read");

    // Verify values
    for (size_t i = 0; i < kNumSamples; ++i) {
        float expected = static_cast<float>(i) * 0.1f;
        ASSERT(std::fabs(read_buf[i] - expected) < 1e-6f, "read value mismatch");
    }

    // Read from empty buffer should fail
    ok = rb.read(read_buf, 1);
    ASSERT(!ok, "read from empty buffer should fail");

    // Write more than capacity should fail
    float big_buf[kCapacity + 1];
    ok = rb.write(big_buf, kCapacity + 1);
    ASSERT(!ok, "write beyond capacity should fail");

    // Reset
    rb.write(write_buf, kNumSamples);
    rb.reset();
    ASSERT(rb.available_read() == 0, "available_read should be 0 after reset");

    std::printf("All ring buffer tests passed.\n");
    return 0;
}
