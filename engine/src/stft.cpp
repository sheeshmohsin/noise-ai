#include "noise/stft.h"
#include <cstring>

namespace noise {

STFT::STFT(size_t fft_size, size_t hop_size)
    : fft_size_(fft_size)
    , hop_size_(hop_size)
{
    // hop_size_ will be used when real STFT overlap-add is implemented
    (void)hop_size_;
}

STFT::~STFT() = default;

void STFT::forward(const float* /*time_domain*/, float* freq_real, float* freq_imag)
{
    // Stub: zero output
    std::memset(freq_real, 0, fft_size_ * sizeof(float));
    std::memset(freq_imag, 0, fft_size_ * sizeof(float));
}

void STFT::inverse(const float* /*freq_real*/, const float* /*freq_imag*/, float* time_domain)
{
    // Stub: zero output
    std::memset(time_domain, 0, fft_size_ * sizeof(float));
}

} // namespace noise
