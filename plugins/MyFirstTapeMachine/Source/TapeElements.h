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
    using waives::dsp::DelayLine;
using waives::dsp::Noise;

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
