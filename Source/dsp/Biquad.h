#pragma once

#include <cmath>
#include <array>

namespace meridian
{

inline constexpr double kPi = 3.14159265358979323846;

/** Allocation-free biquad (transposed direct form II) with RBJ cookbook designs. */
struct BiquadCoefs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    static BiquadCoefs fromRaw (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        BiquadCoefs c;
        c.b0 = (float) (b0 / a0); c.b1 = (float) (b1 / a0); c.b2 = (float) (b2 / a0);
        c.a1 = (float) (a1 / a0); c.a2 = (float) (a2 / a0);
        return c;
    }

    static BiquadCoefs peak (double sr, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr;
        const double alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
        return fromRaw (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    static BiquadCoefs lowShelf (double sr, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr;
        const double alpha = std::sin (w) / (2.0 * q), cw = std::cos (w), s = 2.0 * std::sqrt (A) * alpha;
        return fromRaw (A * ((A + 1) - (A - 1) * cw + s), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - s),
                        (A + 1) + (A - 1) * cw + s, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s);
    }

    static BiquadCoefs highShelf (double sr, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr;
        const double alpha = std::sin (w) / (2.0 * q), cw = std::cos (w), s = 2.0 * std::sqrt (A) * alpha;
        return fromRaw (A * ((A + 1) + (A - 1) * cw + s), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - s),
                        (A + 1) - (A - 1) * cw + s, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s);
    }

    static BiquadCoefs highPass (double sr, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / sr, alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
        return fromRaw ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    static BiquadCoefs lowPass (double sr, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / sr, alpha = std::sin (w) / (2.0 * q), cw = std::cos (w);
        return fromRaw ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
};

/** |H(f)|^2 of a biquad at frequency f. */
inline double magnitudeSquared (const BiquadCoefs& c, double f, double sr) noexcept
{
    const double w = 2.0 * kPi * f / sr;
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
    const double dr = 1.0 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
    return (nr * nr + ni * ni) / (dr * dr + di * di);
}

struct Biquad
{
    BiquadCoefs c;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

/** One-pole smoother / filter helper. */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double sr, double hz) noexcept { a = (float) std::exp (-2.0 * kPi * hz / sr); }
    inline float lp (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() noexcept { z = 0.0f; }
};

} // namespace meridian
