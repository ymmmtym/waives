#pragma once

// Tape-specific building blocks on top of shared/WaivesDsp.h: the fractional
// delay line for wow and flutter, and the asymmetric saturator.

#include "WaivesDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace waives::tape
{
    // The shared filters keep their names here, so the chain reads the same as
    // it did when these lived in this header.
    using waives::dsp::bell;
    using waives::dsp::Biquad;
    using waives::dsp::Coefficients;
    using waives::dsp::highPass;
    using waives::dsp::highShelf;
    using waives::dsp::identity;
    using waives::dsp::lowPass;
    using waives::dsp::lowShelf;
    using waives::dsp::Noise;

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

    // The saturator: a tanh curve with a slight bias, which is what turns a
    // symmetric soft clip into the second-harmonic content tape is known for.
    // Dividing by the drive keeps the small-signal gain at exactly 1, so
    // raising drive changes the character rather than the level.
    inline float saturate (float x, float drive, float bias) noexcept
    {
        const auto amount = std::max (1.0e-4f, drive);
        const auto offset = bias * amount;

        // The bias costs slope at zero: the curve is only unity for quiet audio
        // once that slope (sech^2) is divided back out.
        const auto slope = std::max (1.0e-3f, 1.0f - std::tanh (offset) * std::tanh (offset));

        return (std::tanh (amount * x + offset) - std::tanh (offset)) / (amount * slope);
    }
}
