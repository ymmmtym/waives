#pragma once

// Small helpers shared by the headless plugin tests. They deliberately avoid a
// test framework: every plugin links this header, prints one line per check and
// returns 0/1, which is all ctest needs.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

namespace waives::test
{
    inline int& failureCount() noexcept
    {
        static int count = 0;
        return count;
    }

    inline void check (bool condition, const char* what)
    {
        std::printf ("%s  %s\n", condition ? "[ ok ]" : "[FAIL]", what);

        if (! condition)
            ++failureCount();
    }

    inline bool approxEqual (float a, float b, float tolerance = 1.0e-5f)
    {
        return std::abs (a - b) <= tolerance;
    }

    inline juce::AudioProcessor::BusesLayout makeLayout (const juce::AudioChannelSet& in,
                                                         const juce::AudioChannelSet& out)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return layout;
    }

    inline void fillWithSine (juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples[i] = 0.5f * std::sin (0.05f * (float) (i + channel * 7));
        }
    }

    inline void fillWithSine (juce::AudioBuffer<float>& buffer, double frequency, double sampleRate)
    {
        const auto increment = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples[i] = (float) std::sin (increment * i);
        }
    }

    inline bool buffersMatch (const juce::AudioBuffer<float>& a,
                              const juce::AudioBuffer<float>& b,
                              float tolerance = 1.0e-6f)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int channel = 0; channel < a.getNumChannels(); ++channel)
        {
            const auto* left  = a.getReadPointer (channel);
            const auto* right = b.getReadPointer (channel);

            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                // A NaN would make the difference comparison false and pass.
                if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                    return false;

                if (std::abs (left[i] - right[i]) > tolerance)
                    return false;
            }
        }

        return true;
    }

    /** Prints the summary and returns the process exit code. */
    inline int report()
    {
        if (failureCount() == 0)
            std::printf ("\nAll checks passed.\n");
        else
            std::printf ("\n%d check(s) failed.\n", failureCount());

        return failureCount() == 0 ? 0 : 1;
    }
}
