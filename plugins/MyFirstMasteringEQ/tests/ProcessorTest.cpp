// Headless test driver: instantiates the real AudioProcessor and runs the same
// processBlock() the plugin uses, without a host or an audio device.

#include "PeakResponse.h"
#include "PluginProcessor.h"
#include "TestUtilities.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

using namespace waives::test;

namespace
{
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
        setParameterValue (processor, ParamIDs::bypass, 0.0f);

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
    // The editor smoke test at the end needs a message manager.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

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

    check (processor.getParameters().size() == 4,
           "freq, gain, q and bypass are the published parameters");

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
    setParameterValue (processor, ParamIDs::bypass, 1.0f);

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
    check (restoredState.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f,
           "state restores the bypass switch");

    const char garbage[] = "this is not a plugin state";
    restored.setStateInformation (garbage, (int) sizeof (garbage));
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain)->load() - 6.0f) < 1.0e-3f,
           "garbage state is ignored");

    // The restored instance carries +6 dB and bypass on, so flatten it before
    // checking that it processes audio correctly (prepareToPlay snaps the
    // smoothing to the value).
    setParameterValue (restored, ParamIDs::gain, 0.0f);
    setParameterValue (restored, ParamIDs::bypass, 0.0f);
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

    // A 20 ms ramp at 48 kHz is 960 samples. A host block longer than that must
    // still be smoothed, otherwise the knob jump is applied instantly.
    {
        constexpr double rate = 48000.0;
        constexpr int longBlock = 4096;

        setParameterValue (processor, ParamIDs::frequency, 1000.0f);
        setParameterValue (processor, ParamIDs::q, 1.0f);
        setParameterValue (processor, ParamIDs::gain, 0.0f);

        processor.setPlayConfigDetails (1, 1, rate, longBlock);
        processor.prepareToPlay (rate, longBlock);

        setParameterValue (processor, ParamIDs::gain, 18.0f);   // knob jump

        juce::AudioBuffer<float> jumpBuffer (1, longBlock);
        juce::AudioBuffer<float> jumpInput (1, longBlock);

        fillWithSine (jumpInput, 1000.0, rate);
        fillWithSine (jumpBuffer, 1000.0, rate);

        processor.processBlock (jumpBuffer, midi);

        const auto rmsOver = [] (const juce::AudioBuffer<float>& audio, int start, int length)
        {
            double sum = 0.0;

            for (int i = 0; i < length; ++i)
            {
                const auto value = (double) audio.getSample (0, start + i);
                sum += value * value;
            }

            return std::sqrt (sum / length);
        };

        const auto earlyDb = juce::Decibels::gainToDecibels (
            rmsOver (jumpBuffer, 0, 240) / rmsOver (jumpInput, 0, 240));
        const auto lateDb = juce::Decibels::gainToDecibels (
            rmsOver (jumpBuffer, longBlock - 480, 480)
            / rmsOver (jumpInput, longBlock - 480, 480));

        check (earlyDb < 12.0, "a knob jump is still ramped inside a long block");
        check (lateDb > 17.0, "the ramp reaches the target by the end of the block");
    }

    // A biquad at unity gain is only mathematically transparent; on FMA hardware
    // the rounding is visible. The processor must bypass it bit-exactly.
    {
        setParameterValue (processor, ParamIDs::gain, 0.0f);
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> bypassBuffer (2, blockSize);
        fillWithSine (bypassBuffer);
        const juce::AudioBuffer<float> bypassReference (bypassBuffer);

        processor.processBlock (bypassBuffer, midi);

        check (buffersMatch (bypassBuffer, bypassReference, 0.0f),
               "0 dB is a bit-exact pass-through");
    }

    // The frequency-response graph uses this analytic |H(e^jw)| formula; it must
    // agree with what the audio path actually measures, or the graph lies.
    {
        struct Probe
        {
            float centreHz;
            float gainDb;
            float q;
            double probeHz;
            const char* what;
        };

        const Probe probes[] = {
            { 1000.0f,  12.0f, 1.0f, 1000.0, "on the peak" },
            { 1000.0f,  12.0f, 1.0f,  100.0, "far below the peak" },
            { 1000.0f,  12.0f, 1.0f, 2000.0, "on the skirt" },
            { 1000.0f,  -9.0f, 4.0f, 1000.0, "on a narrow cut" },
        };

        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            for (const auto& probe : probes)
            {
                const waives::peak::BandSettings band { probe.centreHz, probe.gainDb, probe.q };

                const auto analytic = waives::peak::responseDb (rate, band, probe.probeHz);
                const auto measured = measureGainDb (processor, rate, probe.centreHz,
                                                     probe.gainDb, probe.q, probe.probeHz);

                const auto label = juce::String ("graph matches the audio path, ")
                                 + juce::String (rate / 1000.0, 1) + " kHz, "
                                 + probe.what;

                check (std::abs (analytic - measured) < 0.5, label.toRawUTF8());
            }
        }
    }

    //==========================================================================
    // Bypass: the dry signal has to survive bit-exactly, and switching must fade
    // rather than jump.
    {
        auto* bypassParam = apvts.getParameter (ParamIDs::bypass);
        check (bypassParam != nullptr, "bypass parameter exists");

        if (bypassParam != nullptr)
            check (bypassParam->getDefaultValue() < 0.5f, "bypass defaults to off");

        setParameterValue (processor, ParamIDs::frequency, 1000.0f);
        setParameterValue (processor, ParamIDs::gain, 18.0f);
        setParameterValue (processor, ParamIDs::q, 1.0f);
        setParameterValue (processor, ParamIDs::bypass, 1.0f);

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> bypassed (2, blockSize);
        fillWithSine (bypassed);
        const juce::AudioBuffer<float> bypassReference (bypassed);

        processor.processBlock (bypassed, midi);
        check (buffersMatch (bypassed, bypassReference, 0.0f),
               "bypass is a bit-exact pass-through");

        // Switch bypass on mid-stream, with the filter carrying a +18 dB boost,
        // and watch a single continuous block so sample-to-sample steps are real.
        setParameterValue (processor, ParamIDs::bypass, 0.0f);
        processor.setPlayConfigDetails (1, 1, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> warmUp (1, blockSize);

        for (int block = 0; block < 20; ++block)
        {
            fillWithSine (warmUp, 1000.0, sampleRate);
            processor.processBlock (warmUp, midi);
        }

        setParameterValue (processor, ParamIDs::bypass, 1.0f);

        constexpr int fadeLength = 2048;   // ~43 ms, several times the 10 ms fade
        juce::AudioBuffer<float> fade (1, fadeLength);
        fillWithSine (fade, 1000.0, sampleRate);
        const juce::AudioBuffer<float> fadeInput (fade);

        processor.processBlock (fade, midi);

        const auto rmsOver = [] (const juce::AudioBuffer<float>& audio, int start, int length)
        {
            double sum = 0.0;

            for (int i = 0; i < length; ++i)
            {
                const auto value = (double) audio.getSample (0, start + i);
                sum += value * value;
            }

            return std::sqrt (sum / length);
        };

        const auto earlyDb = juce::Decibels::gainToDecibels (
            rmsOver (fade, 0, 96) / rmsOver (fadeInput, 0, 96));
        const auto settledDb = juce::Decibels::gainToDecibels (
            rmsOver (fade, fadeLength - 480, 480) / rmsOver (fadeInput, fadeLength - 480, 480));

        check (earlyDb > 6.0, "the fade starts from the processed signal");
        check (std::abs (settledDb) < 0.1, "the fade reaches the dry signal");

        double maxStep = 0.0;

        for (int i = 1; i < fadeLength; ++i)
            maxStep = juce::jmax (maxStep,
                                  (double) std::abs (fade.getSample (0, i)
                                                     - fade.getSample (0, i - 1)));

        // The boosted sine itself steps by 2 * A * sin(w / 2) per sample; a fade
        // adds nothing beyond that if it is smooth.
        const auto boostedAmplitude = (double) juce::Decibels::decibelsToGain (18.0f);
        const auto naturalStep = 2.0 * boostedAmplitude
                               * std::sin (juce::MathConstants<double>::pi * 1000.0 / sampleRate);

        check (maxStep < naturalStep * 1.15, "switching bypass does not step the output");

        setParameterValue (processor, ParamIDs::bypass, 0.0f);
    }

    //==========================================================================
    // Factory presets: the host's program menu has to offer them, and choosing
    // one must move the parameters.
    {
        check (processor.getNumPrograms() >= 3, "factory presets are published");

        bool namesAreFilled = true;

        for (int program = 0; program < processor.getNumPrograms(); ++program)
            namesAreFilled = namesAreFilled && processor.getProgramName (program).isNotEmpty();

        check (namesAreFilled, "every preset has a name");
        check (processor.getProgramName (processor.getNumPrograms()).isEmpty(),
               "an out-of-range preset name is empty");

        processor.setCurrentProgram (0);
        check (std::abs (apvts.getRawParameterValue (ParamIDs::gain)->load()) < 1.0e-3f,
               "the first preset is a 0 dB pass-through");

        processor.setCurrentProgram (1);
        check (processor.getCurrentProgram() == 1, "setCurrentProgram() selects the program");
        check (std::abs (apvts.getRawParameterValue (ParamIDs::gain)->load()) > 0.1f
                   || std::abs (apvts.getRawParameterValue (ParamIDs::frequency)->load() - 1000.0f) > 1.0f,
               "selecting another preset changes the parameters");

        processor.setCurrentProgram (processor.getNumPrograms());
        check (processor.getCurrentProgram() == 1, "an out-of-range program is ignored");

        juce::MemoryBlock presetState;
        processor.getStateInformation (presetState);

        MyFirstMasteringEQAudioProcessor presetRestored;
        presetRestored.setStateInformation (presetState.getData(), (int) presetState.getSize());

        check (presetRestored.getCurrentProgram() == 1, "state restores the selected preset");
        check (presetRestored.getProgramName (presetRestored.getCurrentProgram())
                   == processor.getProgramName (1),
               "state restores the preset name");
    }

    //==========================================================================
    // The GUI tests in pluginval are skipped in CI, so at least build the editor
    // and run its layout once; an assert or a crash in either fails here.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        check (editor != nullptr, "createEditor() returns an editor");

        if (editor != nullptr)
        {
            check (editor->getWidth() > 0 && editor->getHeight() > 0,
                   "the editor starts with a size");

            editor->setSize (400, 420);
            editor->setSize (900, 760);

            check (editor->getWidth() == 900 && editor->getHeight() == 760,
                   "the editor follows its resize limits");
        }
    }

    processor.releaseResources();

    return waives::test::report();
}
