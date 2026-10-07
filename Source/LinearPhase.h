#pragma once
#include "Dsp.h"
#include <array>
#include <vector>

// Builds linear-phase FIR impulse responses (as a 2x2 L/R matrix) from the band list.
namespace eq
{
struct BandDesc { int type = 0, mode = 0, slope = 1; double freq = 1000, gain = 0, q = 1; };

inline void fftInPlace (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// ir[0]=L->L, ir[1]=R->L, ir[2]=L->R, ir[3]=R->R. Cross terms only filled when needCross.
inline void buildIRs (const std::vector<BandDesc>& bands, double fs, int N, bool mono,
                      std::array<std::vector<float>, 4>& ir, bool& needCross)
{
    const double pi = 3.14159265358979323846;
    const int K = N / 2 + 1;
    std::vector<std::complex<double>> z1 ((size_t) K);
    for (int k = 0; k < K; ++k)
        z1[(size_t) k] = std::polar (1.0, -2.0 * pi * std::max (k * fs / N, 0.01) / fs);

    std::vector<double> LL ((size_t) K, 1.0), LR ((size_t) K, 0.0), RL ((size_t) K, 0.0), RR ((size_t) K, 1.0), H ((size_t) K);
    needCross = false;

    for (const auto& b : bands)
    {
        const auto sec = design (b.type, fs, b.freq, b.gain, b.q, b.slope);
        for (int k = 0; k < K; ++k) H[(size_t) k] = magnitude (sec, z1[(size_t) k]);
        const int mode = mono ? (int) Stereo : b.mode;
        if (mode == Mid || mode == Side) needCross = true;

        for (size_t k = 0; k < (size_t) K; ++k)
        {
            const double h = H[k];
            double a = h, bb = 0, c = 0, d = h;
            switch (mode)
            {
                case Left:  d = 1.0; break;
                case Right: a = 1.0; break;
                case Mid:   a = d = (h + 1) / 2; bb = c = (h - 1) / 2; break;
                case Side:  a = d = (1 + h) / 2; bb = c = (1 - h) / 2; break;
                default: break;
            }
            const double ll = a * LL[k] + bb * RL[k], lr = a * LR[k] + bb * RR[k];
            const double rl = c * LL[k] + d * RL[k],  rr = c * LR[k] + d * RR[k];
            LL[k] = ll; LR[k] = lr; RL[k] = rl; RR[k] = rr;
        }
    }

    auto make = [&] (const std::vector<double>& S, std::vector<float>& out)
    {
        std::vector<std::complex<double>> X ((size_t) N);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double v = S[(size_t) k] * ((k & 1) ? -1.0 : 1.0); // (-1)^k = delay of N/2 samples
            X[(size_t) k] = v;
            if (k > 0 && k < N / 2) X[(size_t) (N - k)] = v;
        }
        fftInPlace (X); // real & symmetric spectrum: forward == inverse * N
        out.resize ((size_t) N);
        for (int n = 0; n < N; ++n)
        {
            const double t = 2.0 * pi * n / N;
            const double w = 0.42 - 0.5 * std::cos (t) + 0.08 * std::cos (2.0 * t); // Blackman, peak at N/2
            out[(size_t) n] = (float) (X[(size_t) n].real() / N * w);
        }
    };
    make (LL, ir[0]);
    make (RR, ir[3]);
    if (needCross) { make (LR, ir[1]); make (RL, ir[2]); }
}
} // namespace eq
