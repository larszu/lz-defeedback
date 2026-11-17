#pragma once

#include "Constants.h"

#include <cmath>
#include <complex>
#include <vector>

namespace lzdf
{

// Iterative radix-2 complex FFT. Size must be a power of two.
class Fft
{
public:
    explicit Fft (int size = 4096) { prepare (size); }

    void prepare (int size)
    {
        n = size;
        bits = 0;
        while ((1 << bits) < n)
            ++bits;

        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }

        twiddle.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * lzdf::kPi * i / n;
            twiddle[(size_t) i] = { (float) std::cos (a), (float) std::sin (a) };
        }
    }

    int size() const noexcept { return n; }

    void forward (std::complex<float>* data) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i])
                std::swap (data[i], data[rev[(size_t) i]]);

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const auto t = twiddle[(size_t) (k * step)] * data[start + k + half];
                    data[start + k + half] = data[start + k] - t;
                    data[start + k] += t;
                }
        }
    }

private:
    int n = 0, bits = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> twiddle;
};

} // namespace lzdf
