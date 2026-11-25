// Reports what one band costs to run. Not a test: the numbers depend on the
// machine, so they are recorded in the README rather than asserted.

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
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

    // The two configurations the README reports: one bell, and all four bands
    // doing work.
    struct Scenario
    {
        const char* name;
        std::array<float, 4> gainDb;
    };

    constexpr Scenario scenarios[] = {
        { "1 band",  {  6.0f,  0.0f, 0.0f,  0.0f } },
        { "4 bands", {  6.0f, -4.0f, 3.0f,  2.0f } },
    };

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

    const auto setParameter = [&processor] (const char* parameterID, float realValue)
    {
        auto* parameter = processor.getValueTreeState().getParameter (parameterID);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    };

    // Spread the bands across the spectrum so each one filters a different
    // region, the way a mastering session uses them.
    constexpr std::array<float, 4> centreHz { 100.0f, 500.0f, 2000.0f, 8000.0f };
    constexpr std::array<float, 4> bandQ    { 0.7f, 1.0f, 1.4f, 0.7f };

    for (int band = 0; band < 4; ++band)
    {
        setParameter (ParamIDs::frequency[(size_t) band], centreHz[(size_t) band]);
        setParameter (ParamIDs::q[(size_t) band], bandQ[(size_t) band]);
    }

    std::printf ("%8s %9s %7s %12s %12s %12s %13s\n",
                 "scenario", "rate", "block", "audio (s)", "wall (ms)", "realtime", "cpu (1 core)");

    for (const auto& scenario : scenarios)
    {
        for (int band = 0; band < 4; ++band)
            setParameter (ParamIDs::gain[(size_t) band], scenario.gainDb[(size_t) band]);

        for (const auto& config : configs)
        {
            const auto wallMilliseconds = measure (processor, config);
            const auto realtimeFactor = secondsOfAudio * 1000.0 / juce::jmax (1.0e-9, wallMilliseconds);
            const auto cpuPercent = 100.0 / realtimeFactor;

            std::printf ("%8s %9.0f %7d %12.1f %12.2f %11.1fx %12.3f%%\n",
                         scenario.name, config.sampleRate, config.blockSize, secondsOfAudio,
                         wallMilliseconds, realtimeFactor, cpuPercent);
        }
    }

    std::printf ("\nlatency: %d samples\n", processor.getLatencySamples());
    std::printf ("stereo in/out, 1 kHz sine; bands at 100 / 500 / 2000 / 8000 Hz\n");

    return 0;
}
