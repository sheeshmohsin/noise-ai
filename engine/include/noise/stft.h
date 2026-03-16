#pragma once

#include <cstddef>

namespace noise {

class STFT {
public:
    STFT(size_t fft_size, size_t hop_size);
    ~STFT();

    void forward(const float* time_domain, float* freq_real, float* freq_imag);
    void inverse(const float* freq_real, const float* freq_imag, float* time_domain);

private:
    size_t fft_size_;
    size_t hop_size_;
};

} // namespace noise
