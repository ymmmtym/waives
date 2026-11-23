// Headless test driver: instantiates the real AudioProcessor and runs the same
// processBlock() the plugin uses, without a host or an audio device.

#include "PluginProcessor.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

namespace
{
    int failures = 0;

    void check (bool condition, const char* what)
    {
        std::printf ("%s  %s\n", condition ? "[ ok ]" : "[FAIL]", what);

        if (! condition)
            ++failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-5f)
    {
        return std::abs (a - b) <= tolerance;
    }

    juce::AudioProcessor::BusesLayout makeLayout (const juce::AudioChannelSet& in,
                                                  const juce::AudioChannelSet& out)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return layout;
    }

    void fillWithSine (juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples[i] = 0.5f * std::sin (0.05f * (float) (i + channel * 7));
        }
    }

    bool buffersMatch (const juce::AudioBuffer<float>& a,
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
                if (std::abs (left[i] - right[i]) > tolerance)
                    return false;
        }

        return true;
    }

    void setParameterValue (MyFirstMasteringEQAudioProcessor& processor,
                            const char* parameterID,
                            float realValue)
    {
        auto* parameter = processor.getValueTreeState().getParameter (parameterID);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    }

    // Runs a sine of `probeHz` through the processor and returns the measured
    // gain in dB, skipping the first 250 ms so smoothing and the filter
    // transient do not pollute the result.
    double measureGainDb (MyFirstMasteringEQAudioProcessor& processor,
                          double sampleRate,
                          float centreHz, float gainDb, float q,
                          double probeHz)
    {
        setParameterValue (processor, ParamIDs::frequency, centreHz);
        setParameterValue (processor, ParamIDs::gain, gainDb);
        setParameterValue (processor, ParamIDs::q, q);

        constexpr int blockSize = 512;
        processor.setPlayConfigDetails (1, 1, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        const int settleSamples = (int) (sampleRate * 0.25);
        const int totalSamples  = settleSamples + (int) (sampleRate * 0.5);

        juce::AudioBuffer<float> buffer (1, blockSize);
        juce::MidiBuffer midi;

        const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * probeHz / sampleRate;

        double inputEnergy = 0.0;
        double outputEnergy = 0.0;
        int measuredSamples = 0;

        for (int start = 0; start < totalSamples; start += blockSize)
        {
            auto* samples = buffer.getWritePointer (0);

            for (int i = 0; i < blockSize; ++i)
                samples[i] = (float) std::sin (phaseIncrement * (start + i));

            processor.processBlock (buffer, midi);

            if (start >= settleSamples)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto input = std::sin (phaseIncrement * (start + i));
                    inputEnergy  += input * input;
                    outputEnergy += (double) samples[i] * samples[i];
                }

                measuredSamples += blockSize;
            }
        }

        const auto rmsIn  = std::sqrt (inputEnergy / measuredSamples);
        const auto rmsOut = std::sqrt (outputEnergy / measuredSamples);

        return juce::Decibels::gainToDecibels (rmsOut / rmsIn);
    }

    // Mimics a host: set the configuration, prepare, then process one block.
    void runPassThroughCheck (MyFirstMasteringEQAudioProcessor& processor,
                              double sampleRate,
                              int numChannels,
                              int numSamples,
                              const char* what)
    {
        processor.setPlayConfigDetails (numChannels, numChannels, sampleRate, numSamples);
        processor.prepareToPlay (sampleRate, numSamples);

        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        fillWithSine (buffer);

        const juce::AudioBuffer<float> reference (buffer);

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        check (buffersMatch (buffer, reference), what);
    }
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    MyFirstMasteringEQAudioProcessor processor;

    check (processor.getName().isNotEmpty(), "getName() returns a name");
    check (processor.getTailLengthSeconds() >= 0.0, "tail length is non-negative");
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
    check (processor.getCurrentBlockSize() == blockSize,
           "prepareToPlay() publishes the block size");
    check (processor.getLatencySamples() == 0, "plugin reports zero latency");

    // Hosts are free to call processBlock with any block size, including tiny
    // ones and sizes larger than the one announced in prepareToPlay().
    runPassThroughCheck (processor, 44100.0,  2, 1,     "stereo 44.1 kHz, 1-sample block untouched");
    runPassThroughCheck (processor, 48000.0,  2, 17,    "stereo 48 kHz, 17-sample block untouched");
    runPassThroughCheck (processor, 48000.0,  2, 512,   "stereo 48 kHz, 512-sample block untouched");
    runPassThroughCheck (processor, 96000.0,  2, 4096,  "stereo 96 kHz, 4096-sample block untouched");
    runPassThroughCheck (processor, 192000.0, 1, 256,   "mono 192 kHz, 256-sample block untouched");

    check (std::abs (processor.getCurrentSampleRate() - 192000.0) < 1.0e-9,
           "sample rate follows the last prepareToPlay()");
    check (processor.getActiveChannelCount() == 1,
           "active channel count reflects the last processed block");

    // Processing the same buffer twice must stay a no-op (no accumulating state).
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillWithSine (buffer);
    const juce::AudioBuffer<float> reference (buffer);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    processor.processBlock (buffer, midi);

    check (buffersMatch (buffer, reference), "repeated blocks stay untouched");

    //==========================================================================
    // Parameters
    auto& apvts = processor.getValueTreeState();

    check (processor.getParameters().size() == 3, "exactly three parameters are published");

    auto* freqParam = apvts.getParameter (ParamIDs::frequency);
    auto* gainParam = apvts.getParameter (ParamIDs::gain);
    auto* qParam    = apvts.getParameter (ParamIDs::q);

    check (freqParam != nullptr && gainParam != nullptr && qParam != nullptr,
           "freq / gain / q parameters exist");

    if (freqParam == nullptr || gainParam == nullptr || qParam == nullptr)
    {
        std::printf ("\nCannot continue without parameters.\n");
        return 1;
    }

    check (approxEqual (freqParam->getNormalisableRange().start, 20.0f)
               && approxEqual (freqParam->getNormalisableRange().end, 20000.0f),
           "frequency range is 20..20000 Hz");
    check (approxEqual (gainParam->getNormalisableRange().start, -18.0f)
               && approxEqual (gainParam->getNormalisableRange().end, 18.0f),
           "gain range is -18..+18 dB");
    check (approxEqual (qParam->getNormalisableRange().start, 0.1f)
               && approxEqual (qParam->getNormalisableRange().end, 10.0f),
           "Q range is 0.1..10");
    // getDefaultValue() is normalised (0..1), so convert it back to the real unit.
    check (approxEqual (freqParam->convertFrom0to1 (freqParam->getDefaultValue()), 1000.0f, 0.5f),
           "frequency defaults to 1 kHz");
    check (approxEqual (gainParam->convertFrom0to1 (gainParam->getDefaultValue()), 0.0f, 1.0e-4f),
           "gain defaults to 0 dB");
    check (approxEqual (qParam->convertFrom0to1 (qParam->getDefaultValue()), 1.0f, 1.0e-3f),
           "Q defaults to 1");

    check (std::abs (freqParam->convertFrom0to1 (0.5f) - 1000.0f) < 1.0f,
           "frequency knob is logarithmic around 1 kHz");
    check (std::abs (qParam->convertFrom0to1 (0.5f) - 1.0f) < 1.0e-3f,
           "Q knob is logarithmic around 1");

    // Hosts (and the CLAP validator) require value -> text -> value -> text to
    // be stable, otherwise automation readouts drift between sessions.
    const auto textRoundTrips = [] (juce::RangedAudioParameter& param, float realValue)
    {
        const auto first = param.getText (param.convertTo0to1 (realValue), 0);
        const auto second = param.getText (param.getValueForText (first), 0);
        return first == second;
    };

    check (textRoundTrips (*freqParam, 1234.5f), "frequency text conversion round-trips");
    check (textRoundTrips (*gainParam, -3.25f), "gain text conversion round-trips");
    check (textRoundTrips (*qParam, 0.3131f), "Q text conversion round-trips");
    check (freqParam->getText (freqParam->convertTo0to1 (4000.0f), 0) == "4.00 kHz",
           "frequency is displayed in kHz above 1 kHz");
    check (gainParam->getText (gainParam->convertTo0to1 (6.0f), 0) == "6.00 dB",
           "gain is displayed in dB");

    //==========================================================================
    // State round-trip through the host's save/load hooks.
    freqParam->setValueNotifyingHost (freqParam->convertTo0to1 (5000.0f));
    gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (6.0f));
    qParam->setValueNotifyingHost (qParam->convertTo0to1 (4.0f));

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    check (state.getSize() > 0, "getStateInformation() produces data");

    MyFirstMasteringEQAudioProcessor restored;
    restored.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    auto& restoredState = restored.getValueTreeState();

    check (std::abs (restoredState.getRawParameterValue (ParamIDs::frequency)->load() - 5000.0f) < 1.0f,
           "state restores the frequency");
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain)->load() - 6.0f) < 1.0e-3f,
           "state restores the gain");
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::q)->load() - 4.0f) < 1.0e-3f,
           "state restores the Q");

    const char garbage[] = "this is not a plugin state";
    restored.setStateInformation (garbage, (int) sizeof (garbage));
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain)->load() - 6.0f) < 1.0e-3f,
           "garbage state is ignored");

    // The restored instance carries +6 dB, so flatten it before checking that it
    // processes audio correctly (prepareToPlay snaps the smoothing to the value).
    setParameterValue (restored, ParamIDs::gain, 0.0f);
    restored.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> restoredBuffer (reference);
    restored.processBlock (restoredBuffer, midi);
    check (buffersMatch (restoredBuffer, reference), "restored instance is transparent at 0 dB");

    //==========================================================================
    // Frequency response of the peaking filter, measured by running a sine
    // through processBlock() and comparing RMS in / out.
    const auto boostDb = measureGainDb (processor, 48000.0, 1000.0f, 12.0f, 1.0f, 1000.0);
    check (std::abs (boostDb - 12.0) < 0.3,
           "1 kHz sine gets +12 dB from a 1 kHz / +12 dB / Q1 peak");

    const auto cutDb = measureGainDb (processor, 48000.0, 1000.0f, -12.0f, 1.0f, 1000.0);
    check (std::abs (cutDb + 12.0) < 0.3,
           "1 kHz sine gets -12 dB from a 1 kHz / -12 dB / Q1 peak");

    const auto farDb = measureGainDb (processor, 48000.0, 1000.0f, 12.0f, 1.0f, 100.0);
    check (std::abs (farDb) < 0.5, "100 Hz is untouched by a 1 kHz peak");

    const auto flatDb = measureGainDb (processor, 48000.0, 1000.0f, 0.0f, 1.0f, 1000.0);
    check (std::abs (flatDb) < 0.05, "0 dB gain is transparent");

    const auto highRateDb = measureGainDb (processor, 96000.0, 1000.0f, 12.0f, 1.0f, 1000.0);
    check (std::abs (highRateDb - 12.0) < 0.3, "the same boost is measured at 96 kHz");

    const auto wideDb   = measureGainDb (processor, 48000.0, 1000.0f, 12.0f, 0.7f, 2000.0);
    const auto narrowDb = measureGainDb (processor, 48000.0, 1000.0f, 12.0f, 8.0f, 2000.0);
    check (narrowDb < wideDb - 3.0, "a higher Q makes the peak narrower");

    check (std::isfinite (boostDb) && std::isfinite (cutDb) && std::isfinite (narrowDb),
           "filter output stays finite");

    // The frequency-response graph uses this analytic |H(e^jw)| formula; it must
    // agree with what the audio path actually measures, or the graph lies.
    {
        const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
            48000.0, 1000.0f, 1.0f, juce::Decibels::decibelsToGain (12.0f));

        const auto a0 = raw[3];
        const auto b0 = raw[0] / a0, b1 = raw[1] / a0, b2 = raw[2] / a0;
        const auto a1 = raw[4] / a0, a2 = raw[5] / a0;

        const auto omega = 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0;
        const std::complex<double> z1 (std::cos (-omega), std::sin (-omega));
        const auto z2 = z1 * z1;

        const auto analyticDb = (float) juce::Decibels::gainToDecibels (
            std::abs (((double) b0 + (double) b1 * z1 + (double) b2 * z2)
                      / (1.0 + (double) a1 * z1 + (double) a2 * z2)));

        const auto measuredDb = measureGainDb (processor, 48000.0, 1000.0f, 12.0f, 1.0f, 1000.0);

        check (std::abs (analyticDb - measuredDb) < 0.2,
               "analytic curve matches the measured response");
    }

    processor.releaseResources();

    if (failures == 0)
        std::printf ("\nAll checks passed.\n");
    else
        std::printf ("\n%d check(s) failed.\n", failures);

    return failures == 0 ? 0 : 1;
}
