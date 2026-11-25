// Reports what one band costs to run. Not a test: the numbers depend on the
// machine, so they are recorded in the README rather than asserted.

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <cstdio>

namespace
{
    struct Config
    {
        double sampleRate;
        int blockSize;
    };

    constexpr Config configs[] = {
        { 44100.0,   64 },
        { 44100.0,  512 },
        { 48000.0,   64 },
        { 48000.0,  512 },
        { 96000.0,  512 },
        { 192000.0, 512 },
    };

    constexpr double secondsOfAudio = 10.0;

    // Processes `secondsOfAudio` through the real processor and returns the wall
    // time in milliseconds.
    double measure (MyFirstMasteringEQAudioProcessor& processor, Config config)
    {
        processor.setPlayConfigDetails (2, 2, config.sampleRate, config.blockSize);
        processor.prepareToPlay (config.sampleRate, config.blockSize);

        juce::AudioBuffer<float> buffer (2, config.blockSize);
        juce::MidiBuffer midi;

        // Warm the caches so the first block does not dominate the average.
        for (int block = 0; block < 8; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        const auto totalBlocks = (juce::int64) (secondsOfAudio * config.sampleRate / config.blockSize);
        const auto phaseIncrement = 2.0 * juce::MathConstants<double>::pi * 1000.0 / config.sampleRate;

        const auto start = juce::Time::getHighResolutionTicks();

        for (juce::int64 block = 0; block < totalBlocks; ++block)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto* samples = buffer.getWritePointer (channel);

                for (int i = 0; i < config.blockSize; ++i)
                    samples[i] = (float) std::sin (phaseIncrement * (double) (block * config.blockSize + i));
            }

            processor.processBlock (buffer, midi);
        }

        const auto elapsed = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - start);

        return elapsed * 1000.0;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    MyFirstMasteringEQAudioProcessor processor;

    // A +6 dB bell: a boost exercises the filter the way a real session does.
    const auto setParameter = [&processor] (const char* parameterID, float realValue)
    {
        auto* parameter = processor.getValueTreeState().getParameter (parameterID);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    };

    setParameter (ParamIDs::frequency[0], 1000.0f);
    setParameter (ParamIDs::gain[0], 6.0f);
    setParameter (ParamIDs::q[0], 1.0f);

    std::printf ("%9s %7s %12s %12s %12s %13s\n",
                 "rate", "block", "audio (s)", "wall (ms)", "realtime", "cpu (1 core)");

    for (const auto& config : configs)
    {
        const auto wallMilliseconds = measure (processor, config);
        const auto realtimeFactor = secondsOfAudio * 1000.0 / juce::jmax (1.0e-9, wallMilliseconds);
        const auto cpuPercent = 100.0 / realtimeFactor;

        std::printf ("%9.0f %7d %12.1f %12.2f %11.1fx %12.3f%%\n",
                     config.sampleRate, config.blockSize, secondsOfAudio,
                     wallMilliseconds, realtimeFactor, cpuPercent);
    }

    std::printf ("\nlatency: %d samples\n", processor.getLatencySamples());
    std::printf ("stereo in/out, 1 kHz sine, +6 dB bell at 1 kHz, Q 1\n");

    return 0;
}
