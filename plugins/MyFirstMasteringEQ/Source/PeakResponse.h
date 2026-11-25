#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <complex>

// Coefficient maths for the peaking filter, shared by the audio path, the
// frequency-response graph and the tests. One definition means the graph cannot
// drift away from what is heard.
namespace waives::peak
{
    struct BandSettings
    {
        float frequency = 1000.0f;
        float gainDb    = 0.0f;
        float q         = 1.0f;
    };

    // Below Nyquist the biquad maths degenerates, and Q must stay positive. Both
    // sides of the plugin clamp through this function so they agree.
    inline BandSettings clampToNyquist (BandSettings band, double sampleRate) noexcept
    {
        band.frequency = juce::jlimit (20.0f, (float) (sampleRate * 0.45), band.frequency);
        band.q         = juce::jmax (0.05f, band.q);
        return band;
    }

    /** Raw coefficients { b0, b1, b2, a0, a1, a2 } as JUCE returns them. */
    inline std::array<float, 6> makePeakCoefficients (double sampleRate,
                                                     BandSettings band) noexcept
    {
        band = clampToNyquist (band, sampleRate);

        return juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
            sampleRate, band.frequency, band.q,
            juce::Decibels::decibelsToGain (band.gainDb));
    }

    /** { b0, b1, b2, a1, a2 } with a0 divided out, ready for a biquad. */
    inline std::array<float, 5> normalise (const std::array<float, 6>& raw) noexcept
    {
        const auto a0 = raw[3];

        return { raw[0] / a0, raw[1] / a0, raw[2] / a0, raw[4] / a0, raw[5] / a0 };
    }

    /** Exact pass-through, used instead of the biquad at unity gain. */
    inline std::array<float, 5> identity() noexcept
    {
        return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }

    /** |H(e^jw)| in dB for normalised coefficients. */
    inline float responseDb (const std::array<float, 5>& normalised,
                             double sampleRate,
                             double probeHz) noexcept
    {
        const auto omega = 2.0 * juce::MathConstants<double>::pi * probeHz / sampleRate;
        const std::complex<double> z1 (std::cos (-omega), std::sin (-omega));
        const auto z2 = z1 * z1;

        const auto numerator   = (double) normalised[0] + (double) normalised[1] * z1
                               + (double) normalised[2] * z2;
        const auto denominator = 1.0 + (double) normalised[3] * z1 + (double) normalised[4] * z2;

        return (float) juce::Decibels::gainToDecibels (std::abs (numerator / denominator), -100.0);
    }

    /** Convenience overload: the response of `band` at `probeHz`. */
    inline float responseDb (double sampleRate, BandSettings band, double probeHz) noexcept
    {
        return responseDb (normalise (makePeakCoefficients (sampleRate, band)), sampleRate, probeHz);
    }
}
