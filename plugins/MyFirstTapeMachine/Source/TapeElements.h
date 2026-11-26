#pragma once

// Small, allocation-free building blocks for the tape chain: RBJ biquad
// coefficients (returned by value), a transposed direct form II biquad, a
// fractional-delay line for wow and flutter, and the hiss generator.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace waives::tape
{
    // { b0, b1, b2, a1, a2 } with a0 already divided out.
    using Coefficients = std::array<float, 5>;

    inline Coefficients identity() noexcept { return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; }

    inline Coefficients normalise (double b0, double b1, double b2,
                                   double a0, double a1, double a2) noexcept
    {
        const auto inverse = 1.0 / a0;

        return { (float) (b0 * inverse), (float) (b1 * inverse), (float) (b2 * inverse),
                 (float) (a1 * inverse), (float) (a2 * inverse) };
    }

    // The cookbook formulas, clamped so a parameter at the top of its range
    // cannot push a corner past Nyquist and blow the filter up.
    inline double clampFrequency (double sampleRate, double frequency) noexcept
    {
        return std::clamp (frequency, 10.0, sampleRate * 0.49);
    }

    inline Coefficients lowPass (double sampleRate, double frequency, double q) noexcept
    {
        const auto omega = 2.0 * 3.14159265358979323846 * clampFrequency (sampleRate, frequency) / sampleRate;
        const auto alpha = std::sin (omega) / (2.0 * std::max (0.05, q));
        const auto cosine = std::cos (omega);

        return normalise ((1.0 - cosine) * 0.5, 1.0 - cosine, (1.0 - cosine) * 0.5,
                          1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
    }

    inline Coefficients highPass (double sampleRate, double frequency, double q) noexcept
    {
        const auto omega = 2.0 * 3.14159265358979323846 * clampFrequency (sampleRate, frequency) / sampleRate;
        const auto alpha = std::sin (omega) / (2.0 * std::max (0.05, q));
        const auto cosine = std::cos (omega);

        return normalise ((1.0 + cosine) * 0.5, -(1.0 + cosine), (1.0 + cosine) * 0.5,
                          1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
    }

    inline Coefficients lowShelf (double sampleRate, double frequency, double gainDb,
                                  double slope = 0.9) noexcept
    {
        const auto amplitude = std::pow (10.0, gainDb / 40.0);
        const auto omega = 2.0 * 3.14159265358979323846 * clampFrequency (sampleRate, frequency) / sampleRate;
        const auto cosine = std::cos (omega);
        const auto sine = std::sin (omega);
        const auto alpha = sine * 0.5 * std::sqrt ((amplitude + 1.0 / amplitude)
                                                     * (1.0 / slope - 1.0) + 2.0);
        const auto twoRootAmplitudeAlpha = 2.0 * std::sqrt (amplitude) * alpha;

        return normalise (amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine
                                        + twoRootAmplitudeAlpha),
                          2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosine),
                          amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine
                                        - twoRootAmplitudeAlpha),
                          (amplitude + 1.0) + (amplitude - 1.0) * cosine + twoRootAmplitudeAlpha,
                          -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosine),
                          (amplitude + 1.0) + (amplitude - 1.0) * cosine - twoRootAmplitudeAlpha);
    }

    inline Coefficients highShelf (double sampleRate, double frequency, double gainDb,
                                   double slope = 0.9) noexcept
    {
        const auto amplitude = std::pow (10.0, gainDb / 40.0);
        const auto omega = 2.0 * 3.14159265358979323846 * clampFrequency (sampleRate, frequency) / sampleRate;
        const auto cosine = std::cos (omega);
        const auto sine = std::sin (omega);
        const auto alpha = sine * 0.5 * std::sqrt ((amplitude + 1.0 / amplitude)
                                                     * (1.0 / slope - 1.0) + 2.0);
        const auto twoRootAmplitudeAlpha = 2.0 * std::sqrt (amplitude) * alpha;

        return normalise (amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine
                                        + twoRootAmplitudeAlpha),
                          -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosine),
                          amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine
                                        - twoRootAmplitudeAlpha),
                          (amplitude + 1.0) - (amplitude - 1.0) * cosine + twoRootAmplitudeAlpha,
                          2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosine),
                          (amplitude + 1.0) - (amplitude - 1.0) * cosine - twoRootAmplitudeAlpha);
    }

    inline Coefficients bell (double sampleRate, double frequency, double gainDb,
                              double q) noexcept
    {
        const auto amplitude = std::pow (10.0, gainDb / 40.0);
        const auto omega = 2.0 * 3.14159265358979323846 * clampFrequency (sampleRate, frequency) / sampleRate;
        const auto alpha = std::sin (omega) / (2.0 * std::max (0.05, q));
        const auto cosine = std::cos (omega);

        return normalise (1.0 + alpha * amplitude, -2.0 * cosine, 1.0 - alpha * amplitude,
                          1.0 + alpha / amplitude, -2.0 * cosine, 1.0 - alpha / amplitude);
    }

    // Transposed direct form II: two floats of state, so retuning it from the
    // audio thread never allocates.
    class Biquad
    {
    public:
        void setCoefficients (const Coefficients& normalised) noexcept
        {
            b0 = normalised[0];
            b1 = normalised[1];
            b2 = normalised[2];
            a1 = normalised[3];
            a2 = normalised[4];
        }

        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }

        float processSample (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    // One delay line per channel, read with linear interpolation so the read
    // position can move by a fraction of a sample. Only prepare() allocates.
    class DelayLine
    {
    public:
        void prepare (int numChannels, int samplesNeeded)
        {
            channels = std::max (1, numChannels);
            capacity = std::max (4, samplesNeeded);

            buffer.assign ((size_t) channels * (size_t) capacity, 0.0f);
            writeIndex = 0;
        }

        void reset() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        int getCapacity() const noexcept { return capacity; }

        void write (int channel, float value) noexcept
        {
            if (channel < 0 || channel >= channels)
                return;

            buffer[(size_t) channel * (size_t) capacity + (size_t) writeIndex] = value;
        }

        /** delaySamples is clamped to [1, capacity - 2] so a stray value can
            never read outside the line. */
        float read (int channel, float delaySamples) const noexcept
        {
            if (channel < 0 || channel >= channels)
                return 0.0f;

            const auto clamped = std::clamp (delaySamples, 1.0f, (float) capacity - 2.0f);
            const auto whole = (int) clamped;
            const auto fraction = clamped - (float) whole;

            const auto* line = buffer.data() + (size_t) channel * (size_t) capacity;

            auto indexA = writeIndex - whole;

            if (indexA < 0)
                indexA += capacity;

            auto indexB = indexA - 1;

            if (indexB < 0)
                indexB += capacity;

            return line[indexA] + fraction * (line[indexB] - line[indexA]);
        }

        void advance() noexcept
        {
            if (++writeIndex >= capacity)
                writeIndex = 0;
        }

    private:
        std::vector<float> buffer;
        int channels = 1;
        int capacity = 4;
        int writeIndex = 0;
    };

    // Deterministic white noise: a xorshift instead of juce::Random keeps the
    // generator per channel and free of any shared state.
    class Noise
    {
    public:
        void seed (std::uint32_t value) noexcept { state = value == 0 ? 1u : value; }

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return (float) (std::int32_t) state * 4.656613e-10f;
        }

    private:
        std::uint32_t state = 0x9e3779b9u;
    };

    // The saturator: a tanh curve with a slight bias, which is what turns a
    // symmetric soft clip into the second-harmonic content tape is known for.
    // Dividing by the drive keeps the small-signal gain at exactly 1, so
    // raising drive changes the character rather than the level.
    inline float saturate (float x, float drive, float bias) noexcept
    {
        const auto amount = std::max (1.0e-4f, drive);
        const auto offset = bias * amount;

        return (std::tanh (amount * x + offset) - std::tanh (offset)) / amount;
    }
}
