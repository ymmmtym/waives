#pragma once

// Valve-amp building blocks: the two non-linear stages, the interstage
// coupling capacitor and the supply sag. The filters come from
// shared/WaivesDsp.h, the same set the other plugins use.

#include "WaivesDsp.h"

#include <cmath>

namespace waives::amp
{
    using waives::dsp::Biquad;

    constexpr double pi = 3.14159265358979323846;

    // Grid bias of the first stage. A symmetric curve only produces odd
    // harmonics, which reads as fuzz; the bias is what gives a valve its even
    // ones. Subtracting tanh(bias) puts the output at zero for a zero input, so
    // the curve adds no DC offset.
    inline float preampCurve (float x, float bias) noexcept
    {
        return std::tanh (x + bias) - std::tanh (bias);
    }

    // The power stage clips harder and is symmetric: by the time a signal gets
    // here it has already picked up its asymmetry.
    inline float powerCurve (float x, float knee) noexcept
    {
        return std::tanh (knee * x) / knee;
    }

    // One-pole high pass: the coupling capacitor between the two stages, which
    // keeps the bias the first valve added out of the second one.
    class OnePoleHighPass
    {
    public:
        void setCutoff (double sampleRate, double frequency) noexcept
        {
            // y[n] = a * (y[n-1] + x[n] - x[n-1]) with a = exp(-2*pi*fc/fs).
            // a is close to 1 for a low corner; 1 - a would put the corner at
            // Nyquist and leave almost nothing but the top octave.
            coefficient = (float) std::exp (-2.0 * pi * frequency / sampleRate);
        }

        void reset() noexcept { previousInput = 0.0f; previousOutput = 0.0f; }

        float processSample (float x) noexcept
        {
            const auto y = coefficient * (previousOutput + x - previousInput);

            previousInput = x;
            previousOutput = y;

            return y;
        }

    private:
        float coefficient = 1.0f;
        float previousInput = 0.0f;
        float previousOutput = 0.0f;
    };

    // Rectified envelope for the sag. The attack is quick enough to duck on the
    // first transient of a chord; the release is slow enough to hear the supply
    // recover afterwards.
    class EnvelopeFollower
    {
    public:
        void prepare (double sampleRate, double attackMs, double releaseMs) noexcept
        {
            attackCoefficient  = (float) (1.0 - std::exp (-1000.0 / (attackMs * sampleRate)));
            releaseCoefficient = (float) (1.0 - std::exp (-1000.0 / (releaseMs * sampleRate)));
            envelope = 0.0f;
        }

        void reset() noexcept { envelope = 0.0f; }

        float processSample (float x) noexcept
        {
            const auto rectified = std::abs (x);
            const auto coefficient = rectified > envelope ? attackCoefficient : releaseCoefficient;

            envelope += coefficient * (rectified - envelope);

            return envelope;
        }

    private:
        float attackCoefficient = 0.5f;
        float releaseCoefficient = 1.0e-4f;
        float envelope = 0.0f;
    };
}
