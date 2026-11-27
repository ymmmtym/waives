// Headless test driver: instantiates the real AudioProcessor and runs the same
// processBlock() the plugin uses, without a host or an audio device.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TapeElements.h"
#include "TestUtilities.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace waives::test;

namespace
{
    void setParameterValue (MyFirstTapeMachineAudioProcessor& processor,
                            const char* parameterID,
                            float realValue)
    {
        auto* parameter = processor.getValueTreeState().getParameter (parameterID);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    }

    // The chain is fully wet unless a test says otherwise, and the noise
    // generator would make sample comparisons meaningless.
    void makeDeterministic (MyFirstTapeMachineAudioProcessor& processor)
    {
        setParameterValue (processor, ParamIDs::bypass, 0.0f);
        setParameterValue (processor, ParamIDs::hiss, -96.0f);
        setParameterValue (processor, ParamIDs::mix, 100.0f);
    }

    void processAll (MyFirstTapeMachineAudioProcessor& processor,
                     juce::AudioBuffer<float>& buffer,
                     int blockSize)
    {
        juce::MidiBuffer midi;

        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            const auto length = juce::jmin (blockSize, buffer.getNumSamples() - start);

            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(), start, length);
            processor.processBlock (view, midi);
        }
    }

    // Sine in, `seconds` of it, wrapped in a buffer long enough for the tail.
    juce::AudioBuffer<float> makeSine (double sampleRate, double frequency,
                                       double seconds, int numChannels = 2)
    {
        juce::AudioBuffer<float> buffer (numChannels, (int) (sampleRate * seconds));
        fillWithSine (buffer, frequency, sampleRate);
        return buffer;
    }

    // Single-bin DFT: how much of `frequency` is in the window.
    double magnitudeAt (const juce::AudioBuffer<float>& buffer, double frequency,
                        double sampleRate, int channel, int start, int length)
    {
        double real = 0.0;
        double imaginary = 0.0;

        const auto increment = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        const auto* samples = buffer.getReadPointer (channel);

        for (int i = 0; i < length; ++i)
        {
            const auto phase = increment * i;

            real += samples[start + i] * std::cos (phase);
            imaginary -= samples[start + i] * std::sin (phase);
        }

        return 2.0 * std::sqrt (real * real + imaginary * imaginary) / length;
    }

    double rmsDb (const juce::AudioBuffer<float>& buffer, int channel, int start, int length)
    {
        double sum = 0.0;

        for (int i = 0; i < length; ++i)
        {
            const auto value = (double) buffer.getSample (channel, start + i);
            sum += value * value;
        }

        return juce::Decibels::gainToDecibels (std::sqrt (sum / length), -120.0);
    }

    // Total harmonic distortion up to the fifth harmonic, relative to the
    // fundamental. Only the steady-state part of the buffer is measured.
    double distortionPercent (const juce::AudioBuffer<float>& buffer, double fundamental,
                              double sampleRate)
    {
        const auto settle = (int) (sampleRate * 0.3);
        const auto length = buffer.getNumSamples() - settle - 4;

        if (length <= 0)
            return 0.0;

        const auto fundamentalLevel = magnitudeAt (buffer, fundamental, sampleRate, 0, settle, length);

        if (fundamentalLevel <= 0.0)
            return 0.0;

        double harmonicEnergy = 0.0;

        for (int harmonic = 2; harmonic <= 5; ++harmonic)
        {
            const auto level = magnitudeAt (buffer, fundamental * harmonic, sampleRate, 0, settle, length);
            harmonicEnergy += level * level;
        }

        return 100.0 * std::sqrt (harmonicEnergy) / fundamentalLevel;
    }

    struct RenderResult
    {
        juce::AudioBuffer<float> buffer;
        double distortion = 0.0;
        double peak = 0.0;
        double rms = 0.0;
        bool finite = true;
    };

    // Runs a 1 kHz sine through the processor with the current settings and
    // returns the steady-state measurements.
    RenderResult render (MyFirstTapeMachineAudioProcessor& processor,
                         double sampleRate,
                         double frequency = 1000.0,
                         double inputLevel = 0.5,
                         int blockSize = 512)
    {
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        auto buffer = makeSine (sampleRate, frequency, 1.0);
        buffer.applyGain ((float) inputLevel);

        processAll (processor, buffer, blockSize);

        RenderResult result;
        result.buffer = buffer;
        result.distortion = distortionPercent (buffer, frequency, sampleRate);
        result.rms = rmsDb (buffer, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = buffer.getSample (channel, i);

                result.peak = std::max (result.peak, (double) std::abs (value));
                result.finite = result.finite && std::isfinite (value);
            }
        }

        return result;
    }

    // Bypassed, the plugin has to hand the input back unprocessed but still
    // delayed by the latency it reports: the host compensates for that delay, so
    // a bypassed signal that arrives early would slide against the dry tracks.
    void runBypassCheck (MyFirstTapeMachineAudioProcessor& processor,
                         double sampleRate, int numChannels, int blockSize,
                         const char* what, bool hostBypass = false)
    {
        if (! hostBypass)
            setParameterValue (processor, ParamIDs::bypass, 1.0f);

        // The trims belong to the tape path: a bypassed plugin hands back the
        // input it was given, not the input with INPUT and OUTPUT applied.
        setParameterValue (processor, ParamIDs::input, 6.0f);
        setParameterValue (processor, ParamIDs::output, -12.0f);

        processor.setPlayConfigDetails (numChannels, numChannels, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        const auto latency = processor.getReportedLatency();
        const int numSamples = 8192;

        juce::AudioBuffer<float> input (numChannels, numSamples);
        fillWithSine (input);

        juce::AudioBuffer<float> output (input);

        // Once to settle the bypass fade and fill the delay line, then again on
        // a fresh copy for the comparison.
        processAll (processor, output, blockSize);

        output.makeCopyOf (input);
        processAll (processor, output, blockSize);

        bool matches = latency > 0;

        for (int channel = 0; channel < numChannels; ++channel)
            for (int i = latency; i < numSamples; ++i)
                matches = matches && std::abs (output.getSample (channel, i)
                                               - input.getSample (channel, i - latency)) < 1.0e-6f;

        check (matches, what);
    }
}

int main()
{
    // The editor smoke test at the end needs a message manager.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    MyFirstTapeMachineAudioProcessor processor;

    check (processor.getName() == "MyFirstTapeMachine", "getName() returns the plugin name");
    check (processor.getTailLengthSeconds() > 0.0, "the tape tail length is non-zero");
    check (! processor.acceptsMidi() && ! processor.producesMidi(), "plugin is audio-only");

    const auto stereo = makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    const auto mono   = makeLayout (juce::AudioChannelSet::mono(),   juce::AudioChannelSet::mono());
    const auto asym   = makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono());

    check (processor.setBusesLayout (stereo), "stereo in/out layout accepted");
    check (processor.setBusesLayout (mono),   "mono in/out layout accepted");
    check (! processor.setBusesLayout (asym), "asymmetric layout rejected");
    check (processor.setBusesLayout (stereo), "stereo layout restored");

    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    check (std::abs (processor.getCurrentSampleRate() - sampleRate) < 1.0e-9,
           "prepareToPlay() publishes the sample rate");
    check (processor.getCurrentBlockSize() == blockSize, "prepareToPlay() publishes the block size");

    // The delay line that carries wow and flutter is part of the sound, so the
    // host has to be told, or the track would drift out of time.
    const auto latency = processor.getLatencySamples();
    check (latency > 0, "the transport delay is reported as latency");
    check (latency < (int) (sampleRate * 0.01), "the reported latency stays under 10 ms");

    // Hosts are free to call processBlock with any block size, including tiny
    // ones and sizes larger than the one announced in prepareToPlay().
    runBypassCheck (processor, 44100.0,  2, 1,    "bypass is the input delayed by the reported latency: stereo 44.1 kHz, 1-sample block");
    runBypassCheck (processor, 48000.0,  2, 17,   "bypass is the input delayed by the reported latency: stereo 48 kHz, 17-sample block");
    runBypassCheck (processor, 96000.0,  2, 512,  "bypass is the input delayed by the reported latency: stereo 96 kHz, 512-sample block");
    runBypassCheck (processor, 192000.0, 1, 4096, "bypass is the input delayed by the reported latency: mono 192 kHz, 4096-sample block");

    // The wrapper's bypass (VST3/AU) has to take the same route; JUCE's default
    // would hand the input back early and fight the host's compensation.
    runBypassCheck (processor, 48000.0,  2, 512,  "the host bypass is the input delayed by the reported latency: stereo 48 kHz",
                    true);

    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    //==========================================================================
    // Parameters
    auto& apvts = processor.getValueTreeState();

    check (processor.getParameters().size() == 9,
           "the plugin publishes input/drive/tone/wow/flutter/hiss/mix/output and bypass");

    auto* inputParam   = apvts.getParameter (ParamIDs::input);
    auto* driveParam   = apvts.getParameter (ParamIDs::drive);
    auto* toneParam    = apvts.getParameter (ParamIDs::tone);
    auto* wowParam     = apvts.getParameter (ParamIDs::wow);
    auto* flutterParam = apvts.getParameter (ParamIDs::flutter);
    auto* hissParam    = apvts.getParameter (ParamIDs::hiss);
    auto* mixParam     = apvts.getParameter (ParamIDs::mix);
    auto* outputParam  = apvts.getParameter (ParamIDs::output);

    check (inputParam != nullptr && driveParam != nullptr && toneParam != nullptr
               && wowParam != nullptr && flutterParam != nullptr && hissParam != nullptr
               && mixParam != nullptr && outputParam != nullptr,
           "every knob has a parameter behind it");

    if (toneParam == nullptr || driveParam == nullptr || mixParam == nullptr)
    {
        std::printf ("\nCannot continue without parameters.\n");
        return 1;
    }

    check (approxEqual (toneParam->getNormalisableRange().start, 2000.0f)
               && approxEqual (toneParam->getNormalisableRange().end, 18000.0f),
           "tone range is 2 kHz .. 18 kHz");
    check (approxEqual (wowParam->getNormalisableRange().start, 0.0f)
               && approxEqual (wowParam->getNormalisableRange().end, 100.0f),
           "wow range is 0 .. 100 %");
    check (approxEqual (mixParam->convertFrom0to1 (mixParam->getDefaultValue()), 100.0f, 1.0e-3f),
           "mix defaults to fully wet");
    check (approxEqual (driveParam->convertFrom0to1 (driveParam->getDefaultValue()), 2.0f, 1.0e-3f),
           "drive defaults to 2");
    check (std::abs (toneParam->convertFrom0to1 (0.5f) - 6000.0f) < 10.0f,
           "the tone knob is logarithmic around 6 kHz");

    const auto textRoundTrips = [] (juce::RangedAudioParameter& param, float realValue)
    {
        const auto first = param.getText (param.convertTo0to1 (realValue), 0);
        const auto second = param.getText (param.getValueForText (first), 0);
        return first == second;
    };

    check (textRoundTrips (*toneParam, 12345.0f), "tone text conversion round-trips");
    check (textRoundTrips (*driveParam, 6.5f), "drive text conversion round-trips");
    check (textRoundTrips (*wowParam, 42.5f), "wow text conversion round-trips");
    check (textRoundTrips (*hissParam, -71.3f), "hiss text conversion round-trips");
    check (toneParam->getText (toneParam->convertTo0to1 (12000.0f), 0) == "12.00 kHz",
           "tone is displayed in kHz");

    //==========================================================================
    // Saturation: more drive means more harmonics, and the level stays usable.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::input, 0.0f);
        setParameterValue (processor, ParamIDs::output, 0.0f);
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);
        setParameterValue (processor, ParamIDs::tone, 18000.0f);

        setParameterValue (processor, ParamIDs::drive, 0.0f);
        const auto clean = render (processor, sampleRate);

        setParameterValue (processor, ParamIDs::drive, 5.0f);
        const auto medium = render (processor, sampleRate);

        setParameterValue (processor, ParamIDs::drive, 10.0f);
        const auto hot = render (processor, sampleRate);

        check (clean.finite && medium.finite && hot.finite, "the saturator stays finite");
        check (hot.distortion > medium.distortion, "more drive adds more harmonics");
        check (medium.distortion > clean.distortion, "drive 5 is dirtier than drive 0");
        check (clean.distortion < 0.5, "drive 0 is close to clean");
        check (hot.distortion > 8.0, "drive 10 is audibly saturated");

        // Tape compresses rather than blows up: a full-scale input must not
        // come out above full scale however hard it is driven.
        check (hot.peak < 1.0, "a hot input is compressed, not amplified past full scale");

        // The second harmonic is the tape signature; a symmetric curve would
        // leave it out entirely.
        const auto settle = (int) (sampleRate * 0.3);
        const auto length = hot.buffer.getNumSamples() - settle - 4;
        const auto second = magnitudeAt (hot.buffer, 2000.0, sampleRate, 0, settle, length);
        const auto third  = magnitudeAt (hot.buffer, 3000.0, sampleRate, 0, settle, length);

        check (second > 0.0 && third > 0.0, "the saturation produces both even and odd harmonics");

        // Level is mostly compensated: 10 dB of drive must not be a 10 dB drop.
        check (hot.rms > clean.rms - 12.0, "the drive knob does not collapse the output level");

        // The chain has to stay level-accurate when nothing is driven: a
        // full-scale sine measures -3.0 dB RMS.
        // -6 dBFS in is -9.03 dB RMS out when the chain is clean.
        check (std::abs (clean.rms - (-9.03)) < 0.6, "the clean chain is level-accurate");
    }

    //==========================================================================
    // Tone: the playback loss control has to take the top end away.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::drive, 0.0f);
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);
        setParameterValue (processor, ParamIDs::tone, 18000.0f);

        const auto open = render (processor, sampleRate, 8000.0);

        setParameterValue (processor, ParamIDs::tone, 2500.0f);
        const auto closed = render (processor, sampleRate, 8000.0);

        const auto lossDb = closed.rms - open.rms;
        check (lossDb < -9.0, "tone at 2.5 kHz takes more than 9 dB off an 8 kHz tone");

        // The head bump is fixed, but it must be there: the low end is lifted
        // slightly compared with the mids.
        setParameterValue (processor, ParamIDs::tone, 18000.0f);
        const auto low = render (processor, sampleRate, 60.0);
        const auto mid = render (processor, sampleRate, 600.0);

        check (low.rms > mid.rms, "the head bump lifts the low end above the mids");
    }

    //==========================================================================
    // Wow and flutter: the modulated read has to move the signal.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::drive, 0.0f);
        setParameterValue (processor, ParamIDs::tone, 18000.0f);
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);

        const auto steady = render (processor, sampleRate, 1000.0);

        setParameterValue (processor, ParamIDs::wow, 100.0f);
        setParameterValue (processor, ParamIDs::flutter, 100.0f);
        const auto wobbly = render (processor, sampleRate, 1000.0);

        check (steady.finite && wobbly.finite, "modulated and unmodulated output stay finite");

        double difference = 0.0;
        const auto start = (int) (sampleRate * 0.3);
        const auto length = (int) (sampleRate * 0.5);

        for (int i = 0; i < length; ++i)
        {
            const auto delta = (double) steady.buffer.getSample (0, start + i)
                             - (double) wobbly.buffer.getSample (0, start + i);
            difference += delta * delta;
        }

        const auto differenceDb = juce::Decibels::gainToDecibels (std::sqrt (difference / length), -120.0);
        check (differenceDb > -40.0, "wow and flutter audibly change the signal");

        // Two runs with the modulation off must be identical: no hidden state
        // leaks between renders.
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);
        const auto again = render (processor, sampleRate, 1000.0);

        check (buffersMatch (steady.buffer, again.buffer, 0.0f),
               "an unmodulated render is deterministic");
    }

    //==========================================================================
    // Mix and hiss.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::drive, 10.0f);
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);

        setParameterValue (processor, ParamIDs::mix, 100.0f);
        const auto wet = render (processor, sampleRate);

        setParameterValue (processor, ParamIDs::mix, 0.0f);
        const auto dry = render (processor, sampleRate);

        check (dry.distortion < wet.distortion * 0.5,
               "mix at 0 % leaves out the saturation");

        setParameterValue (processor, ParamIDs::mix, 100.0f);
        setParameterValue (processor, ParamIDs::drive, 0.0f);
        setParameterValue (processor, ParamIDs::input, -12.0f);
        setParameterValue (processor, ParamIDs::hiss, -30.0f);

        juce::AudioBuffer<float> silence (2, (int) sampleRate);
        silence.clear();

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        processAll (processor, silence, blockSize);

        const auto withHiss = rmsDb (silence, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        setParameterValue (processor, ParamIDs::hiss, -96.0f);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> silenceAgain (2, (int) sampleRate);
        silenceAgain.clear();
        processAll (processor, silenceAgain, blockSize);

        const auto withoutHiss = rmsDb (silenceAgain, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        check (withHiss > -35.0, "hiss at -30 dB puts noise on silence");
        check (withoutHiss < -90.0, "hiss at -96 dB is silent");
    }

    //==========================================================================
    // Extreme settings must not produce NaN, infinity or a runaway.
    {
        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            makeDeterministic (processor);
            setParameterValue (processor, ParamIDs::input, 12.0f);
            setParameterValue (processor, ParamIDs::output, 12.0f);
            setParameterValue (processor, ParamIDs::drive, 10.0f);
            setParameterValue (processor, ParamIDs::tone, 2000.0f);
            setParameterValue (processor, ParamIDs::wow, 100.0f);
            setParameterValue (processor, ParamIDs::flutter, 100.0f);
            setParameterValue (processor, ParamIDs::hiss, -24.0f);

            const auto hot = render (processor, rate, 400.0, 0.95);

            const auto label = juce::String ("extremes stay finite at ")
                             + juce::String (rate / 1000.0, 1) + " kHz";

            check (hot.finite, label.toRawUTF8());
            check (hot.peak < 12.0, "the output trim caps the level, no runaway");
        }
    }

    //==========================================================================
    // A knob move has to reach the sound without a prepareToPlay() in between:
    // a host only calls that on sample-rate or buffer changes.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::tone, 18000.0f);
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);
        setParameterValue (processor, ParamIDs::drive, 0.0f);

        const auto clean = render (processor, sampleRate, 1000.0);

        // Same instance, no fresh prepare: this is what turning a knob does.
        setParameterValue (processor, ParamIDs::drive, 10.0f);

        juce::AudioBuffer<float> driven = makeSine (sampleRate, 1000.0, 1.0);

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processAll (processor, driven, blockSize);

        const auto drivenDistortion = distortionPercent (driven, 1000.0, sampleRate);

        check (drivenDistortion > clean.distortion * 5.0,
               "a drive move reaches the sound without a fresh prepare");

        // Tone is retuned from a smoothed ramp rather than per sample; the same
        // applies to it.
        setParameterValue (processor, ParamIDs::drive, 0.0f);
        setParameterValue (processor, ParamIDs::tone, 18000.0f);

        juce::AudioBuffer<float> open = makeSine (sampleRate, 8000.0, 1.0);
        processAll (processor, open, blockSize);
        const auto openLevel = rmsDb (open, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        setParameterValue (processor, ParamIDs::tone, 2500.0f);

        juce::AudioBuffer<float> closed = makeSine (sampleRate, 8000.0, 1.0);
        processAll (processor, closed, blockSize);
        const auto closedLevel = rmsDb (closed, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        check (closedLevel < openLevel - 6.0,
               "a tone move reaches the sound without a fresh prepare");

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
    }

    //==========================================================================
    // Meter: it shows what leaves the plugin, so the output trim is in it.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::input, 0.0f);
        setParameterValue (processor, ParamIDs::output, 12.0f);

        constexpr int meterBlockSize = 512;
        processor.setPlayConfigDetails (1, 1, sampleRate, meterBlockSize);
        processor.prepareToPlay (sampleRate, meterBlockSize);

        // The meter holds the peak until it is read, so start from a clean
        // slate instead of whatever the earlier blocks left behind.
        processor.getMeterLevel (0);

        auto buffer = makeSine (sampleRate, 1000.0, 1.0, 1);
        buffer.applyGain (0.25f);
        processAll (processor, buffer, meterBlockSize);

        const auto peak = processor.getMeterLevel (0);
        const auto delivered = buffer.getMagnitude (0, 0, buffer.getNumSamples());

        check (peak > 0.0f, "the meter reports a peak at all");
        check (std::abs (peak - delivered) < 1.0e-4f,
               "the meter shows the sample after the output trim");

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
    }


    //==========================================================================
    // Smoothing: a knob jump inside a long block still ramps.
    {
        makeDeterministic (processor);
        setParameterValue (processor, ParamIDs::drive, 0.0f);
        setParameterValue (processor, ParamIDs::wow, 0.0f);
        setParameterValue (processor, ParamIDs::flutter, 0.0f);
        setParameterValue (processor, ParamIDs::mix, 100.0f);
        setParameterValue (processor, ParamIDs::input, 0.0f);
        setParameterValue (processor, ParamIDs::output, -12.0f);

        constexpr int longBlock = 4096;
        processor.setPlayConfigDetails (1, 1, sampleRate, longBlock);
        processor.prepareToPlay (sampleRate, longBlock);

        // Prime the delay line, or the first samples of the measured block would
        // still be silence and an instant gain jump would pass the "early" check.
        juce::AudioBuffer<float> prime (1, (int) sampleRate);
        fillWithSine (prime, 1000.0, sampleRate);
        prime.applyGain (0.05f);
        processAll (processor, prime, longBlock);

        setParameterValue (processor, ParamIDs::output, 12.0f);   // knob jump

        juce::AudioBuffer<float> input (1, longBlock);
        juce::AudioBuffer<float> output (1, longBlock);
        fillWithSine (input, 1000.0, sampleRate);
        fillWithSine (output, 1000.0, sampleRate);
        input.applyGain (0.05f);
        output.applyGain (0.05f);

        juce::MidiBuffer midi;
        processor.processBlock (output, midi);

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        // Measured past the delay. An instant jump would put the -6 dBFS sine at
        // about -18 dB straight away; this window is still climbing out of the
        // old -41 dB, which is what the ramp is for.
        const auto latency = processor.getReportedLatency();
        const auto early = rmsDb (output, 0, latency, 64);
        const auto late  = rmsDb (output, 0, longBlock - 480, 480);

        check (early < -25.0, "a 24 dB output jump is ramped, not applied instantly");
        check (late > -20.0, "the ramp reaches the new output level by the end of the block");
    }

    //==========================================================================
    // State round-trip through the host's save/load hooks.
    {
        for (const auto* id : { ParamIDs::input, ParamIDs::drive, ParamIDs::tone, ParamIDs::wow,
                                ParamIDs::flutter, ParamIDs::hiss, ParamIDs::mix, ParamIDs::output })
            setParameterValue (processor, id, 0.0f);

        setParameterValue (processor, ParamIDs::drive, 7.25f);
        setParameterValue (processor, ParamIDs::tone, 4321.0f);
        setParameterValue (processor, ParamIDs::flutter, 66.0f);

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        check (state.getSize() > 0, "getStateInformation() produces data");

        MyFirstTapeMachineAudioProcessor restored;
        restored.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        restored.prepareToPlay (sampleRate, blockSize);
        restored.setStateInformation (state.getData(), (int) state.getSize());

        auto& restoredState = restored.getValueTreeState();

        check (std::abs (restoredState.getRawParameterValue (ParamIDs::drive)->load() - 7.25f) < 1.0e-3f,
               "state restores the drive");
        check (std::abs (restoredState.getRawParameterValue (ParamIDs::tone)->load() - 4321.0f) < 1.0f,
               "state restores the tone");
        check (std::abs (restoredState.getRawParameterValue (ParamIDs::flutter)->load() - 66.0f) < 1.0e-3f,
               "state restores the flutter");

        const char garbage[] = "this is not a plugin state";
        restored.setStateInformation (garbage, (int) sizeof (garbage));
        check (std::abs (restoredState.getRawParameterValue (ParamIDs::drive)->load() - 7.25f) < 1.0e-3f,
               "garbage state is ignored");
    }

    //==========================================================================
    // Factory presets.
    {
        check (processor.getNumPrograms() >= 3, "factory presets are published");

        bool namesAreFilled = true;

        for (int program = 0; program < processor.getNumPrograms(); ++program)
            namesAreFilled = namesAreFilled && processor.getProgramName (program).isNotEmpty();

        check (namesAreFilled, "every preset has a name");
        check (processor.getProgramName (processor.getNumPrograms()).isEmpty(),
               "an out-of-range preset name is empty");

        processor.setCurrentProgram (1);
        check (processor.getCurrentProgram() == 1, "setCurrentProgram() selects the program");

        const auto driveAtPresetOne = apvts.getRawParameterValue (ParamIDs::drive)->load();
        processor.setCurrentProgram (2);
        check (std::abs (apvts.getRawParameterValue (ParamIDs::drive)->load() - driveAtPresetOne) > 0.1f,
               "presets change the parameters");

        processor.setCurrentProgram (processor.getNumPrograms());
        check (processor.getCurrentProgram() == 2, "an out-of-range program is ignored");
    }

    //==========================================================================
    // Editor smoke test: the knobs exist, point at their parameters and do not
    // collide with each other at the smallest allowed size.
    {
        MyFirstTapeMachineAudioProcessor editorProcessor;
        editorProcessor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        editorProcessor.prepareToPlay (sampleRate, blockSize);

        std::unique_ptr<MyFirstTapeMachineAudioProcessorEditor> editor (
            static_cast<MyFirstTapeMachineAudioProcessorEditor*> (editorProcessor.createEditor()));

        check (editor != nullptr, "createEditor() returns an editor");

        if (editor != nullptr)
        {
            editor->setSize (560, 470);

            auto* driveSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("driveSlider"));
            auto* toneSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("toneSlider"));
            auto* mixSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("mixSlider"));

            check (driveSlider != nullptr && toneSlider != nullptr && mixSlider != nullptr,
                   "the drive, tone and mix knobs are there");

            auto* constrainer = editor->getConstrainer();
            check (constrainer != nullptr
                       && constrainer->getMinimumWidth() == 560
                       && constrainer->getMinimumHeight() == 470,
                   "560x470 is the smallest size the layout allows");

            if (driveSlider != nullptr && toneSlider != nullptr)
            {
                setParameterValue (editorProcessor, ParamIDs::drive, 8.5f);
                setParameterValue (editorProcessor, ParamIDs::tone, 3456.0f);

                check (std::abs (driveSlider->getValue() - 8.5) < 0.01,
                       "the drive parameter reaches its knob");
                check (std::abs (toneSlider->getValue() - 3456.0) < 1.0,
                       "the tone parameter reaches its knob");

                check (! toneSlider->getBounds().intersects (driveSlider->getBounds()),
                       "the knobs do not overlap at the minimum size");

                editor->setSize (1000, 760);
                check (toneSlider->getBounds().getHeight() > 0,
                       "the layout survives the largest size");
            }

            editor.reset();
            editorProcessor.releaseResources();
        }
    }

    processor.releaseResources();

    return waives::test::report();
}
