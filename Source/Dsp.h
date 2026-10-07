#pragma once
#include <cmath>
#include <complex>
#include <algorithm>

namespace eq
{
constexpr int kNumBands = 24;
constexpr int kMaxSections = 8;

enum Type { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, NumTypes };
enum Mode { Stereo = 0, Left, Right, Mid, Side };

// slope choice index -> filter order (6 dB/oct per order)
constexpr int kSlopeOrders[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };

inline bool hasGain (int t) { return t == Bell || t == LowShelf || t == HighShelf; }
inline bool isCut (int t)   { return t == LowCut || t == HighCut; }

struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct Sections { int n = 0; Coeffs c[kMaxSections]; };

inline Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// RBJ cookbook biquads
inline Coeffs biquad (int type, double fs, double f, double gainDb, double q)
{
    const double pi = 3.14159265358979323846;
    f = std::clamp (f, 5.0, fs * 0.49);
    q = std::max (q, 0.05);
    const double w0 = 2.0 * pi * f / fs, cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double sA = 2.0 * std::sqrt (A) * alpha;

    switch (type)
    {
        case Bell:
            return normalise (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
        case LowShelf:
            return normalise (A * ((A + 1) - (A - 1) * cw + sA), 2 * A * ((A - 1) - (A + 1) * cw),
                              A * ((A + 1) - (A - 1) * cw - sA),
                              (A + 1) + (A - 1) * cw + sA, -2 * ((A - 1) + (A + 1) * cw),
                              (A + 1) + (A - 1) * cw - sA);
        case HighShelf:
            return normalise (A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw),
                              A * ((A + 1) + (A - 1) * cw - sA),
                              (A + 1) - (A - 1) * cw + sA, 2 * ((A - 1) - (A + 1) * cw),
                              (A + 1) - (A - 1) * cw - sA);
        case Notch:
            return normalise (1, -2 * cw, 1, 1 + alpha, -2 * cw, 1 - alpha);
        case BandPass:
            return normalise (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
        case LowCut: // high-pass
            return normalise ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        case HighCut: // low-pass
        default:
            return normalise ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
}

inline Coeffs firstOrder (bool highpass, double fs, double f)
{
    const double pi = 3.14159265358979323846;
    f = std::clamp (f, 5.0, fs * 0.49);
    const double K = std::tan (pi * f / fs);
    const double a0 = 1.0 + K, a1 = (K - 1.0) / a0;
    if (highpass) return { 1.0 / a0, -1.0 / a0, 0, a1, 0 };
    return { K / a0, K / a0, 0, a1, 0 };
}

// Builds the cascade of biquads for one band.
inline Sections design (int type, double fs, double f, double gainDb, double q, int slopeIdx)
{
    Sections s;
    if (! isCut (type))
    {
        s.c[0] = biquad (type, fs, f, gainDb, q);
        s.n = 1;
        return s;
    }

    const double pi = 3.14159265358979323846;
    const int order = kSlopeOrders[std::clamp (slopeIdx, 0, 7)];
    const int pairs = order / 2;
    for (int k = 0; k < pairs; ++k)
    {
        double qk = 1.0 / (2.0 * std::cos (pi * (2.0 * k + 1.0) / (2.0 * order)));
        if (k == 0) qk *= q / 0.70710678; // user Q acts as resonance on the sharpest section
        s.c[s.n++] = biquad (type, fs, f, 0.0, qk);
    }
    if (order % 2 == 1)
        s.c[s.n++] = firstOrder (type == LowCut, fs, f);
    return s;
}

inline double magnitudeDb (const Sections& s, double fs, double f)
{
    const double w = 2.0 * 3.14159265358979323846 * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    double db = 0.0;
    for (int i = 0; i < s.n; ++i)
    {
        const auto& c = s.c[i];
        const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        db += 20.0 * std::log10 (std::max (std::abs (h), 1e-9));
    }
    return db;
}

// linear magnitude, z1 = e^{-jw} precomputed by the caller
inline double magnitude (const Sections& s, std::complex<double> z1)
{
    const auto z2 = z1 * z1;
    double m = 1.0;
    for (int i = 0; i < s.n; ++i)
    {
        const auto& c = s.c[i];
        m *= std::abs ((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
    }
    return m;
}
} // namespace eq
