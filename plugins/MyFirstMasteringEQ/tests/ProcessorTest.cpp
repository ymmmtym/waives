// Headless test driver: instantiates the real AudioProcessor and runs the same
// processBlock() the plugin uses, without a host or an audio device.

#include "FrequencyResponseCurve.h"
#include "PeakResponse.h"
#include "PluginEditor.h"
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
    // Measures the chain with whatever the parameters are set to right now.
        double measureCurrentSettingsDb (MyFirstMasteringEQAudioProcessor& processor,
                                     double sampleRate,
                                     double probeHz);

    double measureGainDb (MyFirstMasteringEQAudioProcessor& processor,
                          double sampleRate,
                          float centreHz, float gainDb, float q,
                          double probeHz)
    {
        setParameterValue (processor, ParamIDs::frequency[0], centreHz);
        setParameterValue (processor, ParamIDs::gain[0], gainDb);
        setParameterValue (processor, ParamIDs::q[0], q);
        setParameterValue (processor, ParamIDs::bypass, 0.0f);

        return measureCurrentSettingsDb (processor, sampleRate, probeHz);
    }

    double measureCurrentSettingsDb (MyFirstMasteringEQAudioProcessor& processor,
                                     double sampleRate,
                                     double probeHz)
    {
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

    // The analytic curve for a chain whose first entries are `active` and whose
    // remaining bands are flat. Built by hand so the expected values do not
    // depend on the band count.
    std::array<waives::peak::BandSettings, ParamIDs::numBands>
    flatChain (std::initializer_list<waives::peak::BandSettings> active)
    {
        std::array<waives::peak::BandSettings, ParamIDs::numBands> chain {};

        for (auto& band : chain)
            band = { 1000.0f, 0.0f, 1.0f };

        int index = 0;

        for (const auto& band : active)
            chain[(size_t) index++] = band;

        return chain;
    }

    // Every band flat: the state a fresh instance starts from.
    void flattenBands (MyFirstMasteringEQAudioProcessor& processor)
    {
        for (int band = 0; band < ParamIDs::numBands; ++band)
        {
            setParameterValue (processor, ParamIDs::frequency[(size_t) band], 1000.0f);
            setParameterValue (processor, ParamIDs::gain[(size_t) band], 0.0f);
            setParameterValue (processor, ParamIDs::q[(size_t) band], 1.0f);
        }

        setParameterValue (processor, ParamIDs::bypass, 0.0f);
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

    check (processor.getParameters().size() == 4 * ParamIDs::numBands + 1,
           "each band publishes freq/gain/q and its own bypass, plus the global bypass");

    // The AU wrapper sorts by version hint and hosts address automation by that
    // position, so every release that added bands had to use a hint of its own.
    {
        const auto hintOf = [&apvts] (const char* parameterID)
        {
            auto* parameter = apvts.getParameter (parameterID);
            return parameter == nullptr ? -1 : parameter->getVersionHint();
        };

        check (hintOf (ParamIDs::frequency[0]) == 1 && hintOf (ParamIDs::gain[0]) == 1
                   && hintOf (ParamIDs::q[0]) == 1 && hintOf (ParamIDs::bypass) == 1,
               "band 1 and the bypass switch keep the v0.1.0 version hint");

        const auto hintsAreExpected = [&hintOf] (int firstBand, int endBand, int expected,
                                                 bool includeBypass)
        {
            for (int band = firstBand; band < endBand; ++band)
            {
                const auto index = (size_t) band;

                if (hintOf (ParamIDs::frequency[index]) != expected
                    || hintOf (ParamIDs::gain[index]) != expected
                    || hintOf (ParamIDs::q[index]) != expected
                    || (includeBypass && hintOf (ParamIDs::bandBypass[index]) != expected))
                    return false;
            }

            return true;
        };

        check (hintsAreExpected (1, 4, 2, true), "bands 2..4 keep the v0.3.0 version hint");
        check (hintsAreExpected (4, ParamIDs::numBands, 3, true),
               "bands 5..8 use a new version hint so older automation does not move");

        // Band 1's bypass shipped in v0.3.0, not with the band itself.
        check (hintOf (ParamIDs::bandBypass[0]) == 2,
               "band 1's bypass keeps the v0.3.0 version hint");
        bool fourBandBypassesOnHint2 = true;

        for (int band = 0; band < 4; ++band)
            fourBandBypassesOnHint2 = fourBandBypassesOnHint2
                                   && hintOf (ParamIDs::bandBypass[(size_t) band]) == 2;

        check (fourBandBypassesOnHint2, "the four-band bypasses stay in the v0.3.0 group");
    }

    auto* freqParam = apvts.getParameter (ParamIDs::frequency[0]);
    auto* gainParam = apvts.getParameter (ParamIDs::gain[0]);
    auto* qParam    = apvts.getParameter (ParamIDs::q[0]);

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

    check (std::abs (restoredState.getRawParameterValue (ParamIDs::frequency[0])->load() - 5000.0f) < 1.0f,
           "state restores the frequency");
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain[0])->load() - 6.0f) < 1.0e-3f,
           "state restores the gain");
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::q[0])->load() - 4.0f) < 1.0e-3f,
           "state restores the Q");
    check (restoredState.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f,
           "state restores the bypass switch");

    const char garbage[] = "this is not a plugin state";
    restored.setStateInformation (garbage, (int) sizeof (garbage));
    check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain[0])->load() - 6.0f) < 1.0e-3f,
           "garbage state is ignored");

    // The restored instance carries +6 dB and bypass on, so flatten it before
    // checking that it processes audio correctly (prepareToPlay snaps the
    // smoothing to the value).
    setParameterValue (restored, ParamIDs::gain[0], 0.0f);
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

        setParameterValue (processor, ParamIDs::frequency[0], 1000.0f);
        setParameterValue (processor, ParamIDs::q[0], 1.0f);
        setParameterValue (processor, ParamIDs::gain[0], 0.0f);

        processor.setPlayConfigDetails (1, 1, rate, longBlock);
        processor.prepareToPlay (rate, longBlock);

        setParameterValue (processor, ParamIDs::gain[0], 18.0f);   // knob jump

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
        setParameterValue (processor, ParamIDs::gain[0], 0.0f);
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

        setParameterValue (processor, ParamIDs::frequency[0], 1000.0f);
        setParameterValue (processor, ParamIDs::gain[0], 18.0f);
        setParameterValue (processor, ParamIDs::q[0], 1.0f);
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
    // Four bands in series: each one is independent, and the combined response is
    // the sum of the individual decibel curves.
    {
        flattenBands (processor);

        // Only band 2 is active: band 1 must not colour the result.
        setParameterValue (processor, ParamIDs::frequency[1], 2000.0f);
        setParameterValue (processor, ParamIDs::gain[1], 12.0f);
        setParameterValue (processor, ParamIDs::q[1], 1.0f);

        const auto atBand2 = measureCurrentSettingsDb (processor, sampleRate, 2000.0);
        check (std::abs (atBand2 - 12.0) < 0.5, "band 2 boosts its own centre frequency");

        const auto awayFromBand2 = measureCurrentSettingsDb (processor, sampleRate, 100.0);
        check (std::abs (awayFromBand2) < 0.5, "band 2 leaves the rest of the spectrum alone");

        // Band 1 keeps working while band 2 is active.
        setParameterValue (processor, ParamIDs::frequency[0], 300.0f);
        setParameterValue (processor, ParamIDs::gain[0], -9.0f);
        setParameterValue (processor, ParamIDs::q[0], 1.0f);

        const auto atBand1 = measureCurrentSettingsDb (processor, sampleRate, 300.0);
        check (std::abs (atBand1 - (-9.0)) < 0.5, "band 1 still works with band 2 active");

        const auto atBand2Again = measureCurrentSettingsDb (processor, sampleRate, 2000.0);
        check (std::abs (atBand2Again - atBand2) < 0.5,
               "moving band 1 does not change band 2's response");

        // Two bands overlapping: the measured result must be the sum of the
        // analytic curves, which is what the graph draws.
        flattenBands (processor);
        setParameterValue (processor, ParamIDs::frequency[0], 500.0f);
        setParameterValue (processor, ParamIDs::gain[0], 9.0f);
        setParameterValue (processor, ParamIDs::q[0], 1.0f);
        setParameterValue (processor, ParamIDs::frequency[1], 4000.0f);
        setParameterValue (processor, ParamIDs::gain[1], 6.0f);
        setParameterValue (processor, ParamIDs::q[1], 1.0f);

        const auto chain = flatChain ({ { 500.0f, 9.0f, 1.0f }, { 4000.0f, 6.0f, 1.0f } });

        for (const double probeHz : { 500.0, 1000.0, 4000.0 })
        {
            const auto measured = measureCurrentSettingsDb (processor, sampleRate, probeHz);
            const auto analytic = waives::peak::combinedResponseDb (sampleRate, chain, probeHz);

            const auto label = juce::String ("two bands add up at ")
                             + juce::String (probeHz, 0) + " Hz";

            check (std::abs (measured - analytic) < 0.5, label.toRawUTF8());
        }

        // The same boost in a different band must sound the same: the chain is
        // ordered but the response is not. The last band is included because it
        // was added after the first release.
        for (const int band : { 3, ParamIDs::numBands - 1 })
        {
            flattenBands (processor);
            setParameterValue (processor, ParamIDs::frequency[(size_t) band], 1000.0f);
            setParameterValue (processor, ParamIDs::gain[(size_t) band], 12.0f);
            setParameterValue (processor, ParamIDs::q[(size_t) band], 1.0f);

            const auto measured = measureCurrentSettingsDb (processor, sampleRate, 1000.0);
            const auto label = "band " + juce::String (band + 1) + " boosts like any other band";

            check (std::abs (measured - 12.0) < 0.5, label.toRawUTF8());
        }

        flattenBands (processor);
    }

    //==========================================================================
    // Band bypass and solo: switching a band off must leave the other bands
    // alone, and a solo must mute everything except the band being listened to.
    {
        flattenBands (processor);
        setParameterValue (processor, ParamIDs::frequency[0], 1000.0f);
        setParameterValue (processor, ParamIDs::gain[0], 12.0f);
        setParameterValue (processor, ParamIDs::q[0], 1.0f);

        check (std::abs (measureCurrentSettingsDb (processor, sampleRate, 1000.0) - 12.0) < 0.5,
               "the band is heard before it is switched off");

        setParameterValue (processor, ParamIDs::bandBypass[0], 1.0f);
        check (std::abs (measureCurrentSettingsDb (processor, sampleRate, 1000.0)) < 0.5,
               "a bypassed band stops colouring its frequency");

        setParameterValue (processor, ParamIDs::bandBypass[0], 0.0f);
        check (std::abs (measureCurrentSettingsDb (processor, sampleRate, 1000.0) - 12.0) < 0.5,
               "the band comes back when its bypass is released");

        // Band 2 must keep working while band 1 is switched off.
        setParameterValue (processor, ParamIDs::frequency[1], 2000.0f);
        setParameterValue (processor, ParamIDs::gain[1], -6.0f);
        setParameterValue (processor, ParamIDs::q[1], 1.0f);
        setParameterValue (processor, ParamIDs::bandBypass[0], 1.0f);

        check (std::abs (measureCurrentSettingsDb (processor, sampleRate, 2000.0) - (-6.0)) < 0.5,
               "bypassing one band leaves the others working");

        setParameterValue (processor, ParamIDs::bandBypass[0], 0.0f);

        // Solo: only the chosen band is heard.
        flattenBands (processor);
        setParameterValue (processor, ParamIDs::frequency[0], 1000.0f);
        setParameterValue (processor, ParamIDs::gain[0], 9.0f);
        setParameterValue (processor, ParamIDs::frequency[1], 4000.0f);
        setParameterValue (processor, ParamIDs::gain[1], -9.0f);

        const auto bothBands = flatChain ({ { 1000.0f, 9.0f, 1.0f }, { 4000.0f, -9.0f, 1.0f } });

        const auto band1Only = flatChain ({ { 1000.0f, 9.0f, 1.0f } });

        check (std::abs (measureCurrentSettingsDb (processor, sampleRate, 4000.0)
                         - waives::peak::combinedResponseDb (sampleRate, bothBands, 4000.0)) < 0.5,
               "band 2 is heard before the solo starts");

        processor.setSoloBand (0);
        check (processor.getSoloBand() == 0, "the solo can be read back");

        for (const double probeHz : { 1000.0, 4000.0 })
        {
            const auto measured = measureCurrentSettingsDb (processor, sampleRate, probeHz);
            const auto expected = waives::peak::combinedResponseDb (sampleRate, band1Only, probeHz);

            const auto label = juce::String ("a solo leaves band 1 alone at ")
                             + juce::String (probeHz, 0) + " Hz";

            check (std::abs (measured - expected) < 0.5, label.toRawUTF8());
        }

        processor.setSoloBand (-1);
        check (std::abs (measureCurrentSettingsDb (processor, sampleRate, 4000.0)
                         - waives::peak::combinedResponseDb (sampleRate, bothBands, 4000.0)) < 0.5,
               "clearing the solo brings the other bands back");

        processor.setSoloBand (ParamIDs::numBands);
        check (processor.getSoloBand() == -1, "an out-of-range solo is ignored");

        // A solo is a listening aid, so it must not travel with the session.
        processor.setSoloBand (1);

        juce::MemoryBlock soloedState;
        processor.getStateInformation (soloedState);

        MyFirstMasteringEQAudioProcessor reloaded;
        reloaded.setStateInformation (soloedState.getData(), (int) soloedState.getSize());

        check (reloaded.getSoloBand() == -1, "a reloaded session is not left in solo");

        processor.setSoloBand (-1);

        // Switching a band must fade, exactly like the global bypass does.
        flattenBands (processor);
        setParameterValue (processor, ParamIDs::frequency[0], 1000.0f);
        setParameterValue (processor, ParamIDs::gain[0], 18.0f);
        setParameterValue (processor, ParamIDs::q[0], 1.0f);

        processor.setPlayConfigDetails (1, 1, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> bandWarmUp (1, blockSize);

        for (int block = 0; block < 20; ++block)
        {
            fillWithSine (bandWarmUp, 1000.0, sampleRate);
            processor.processBlock (bandWarmUp, midi);
        }

        setParameterValue (processor, ParamIDs::bandBypass[0], 1.0f);

        constexpr int fadeLength = 2048;   // ~43 ms, several times the 10 ms fade
        juce::AudioBuffer<float> bandFade (1, fadeLength);
        fillWithSine (bandFade, 1000.0, sampleRate);
        const juce::AudioBuffer<float> bandFadeInput (bandFade);

        processor.processBlock (bandFade, midi);

        const auto rmsOf = [] (const juce::AudioBuffer<float>& audio, int start, int length)
        {
            double sum = 0.0;

            for (int i = 0; i < length; ++i)
            {
                const auto value = (double) audio.getSample (0, start + i);
                sum += value * value;
            }

            return std::sqrt (sum / length);
        };

        const auto earlyDb = juce::Decibels::gainToDecibels (rmsOf (bandFade, 0, 96)
                                                             / rmsOf (bandFadeInput, 0, 96));
        const auto settledDb = juce::Decibels::gainToDecibels (
            rmsOf (bandFade, fadeLength - 480, 480) / rmsOf (bandFadeInput, fadeLength - 480, 480));

        check (earlyDb > 6.0, "the band fade starts from the boosted signal");
        check (std::abs (settledDb) < 0.1, "the band fade reaches the dry signal");

        double maxStep = 0.0;

        for (int i = 1; i < fadeLength; ++i)
            maxStep = juce::jmax (maxStep,
                                  (double) std::abs (bandFade.getSample (0, i)
                                                     - bandFade.getSample (0, i - 1)));

        const auto boostedAmplitude = (double) juce::Decibels::decibelsToGain (18.0f);
        const auto naturalStep = 2.0 * boostedAmplitude
                               * std::sin (juce::MathConstants<double>::pi * 1000.0 / sampleRate);

        check (maxStep < naturalStep * 1.15, "switching a band does not step the output");

        setParameterValue (processor, ParamIDs::bandBypass[0], 0.0f);
        flattenBands (processor);
    }

    //==========================================================================
    // A session saved by the one-band build only carries band 1's parameters;
    // the rest must stay at 0 dB so the sound does not change on reload.
    {
        // Build a real state, then drop everything the one-band build did not
        // know about: that is exactly what an old session looks like.
        MyFirstMasteringEQAudioProcessor legacy;
        setParameterValue (legacy, ParamIDs::frequency[0], 5000.0f);
        setParameterValue (legacy, ParamIDs::gain[0], 6.0f);
        setParameterValue (legacy, ParamIDs::q[0], 4.0f);

        auto legacyState = legacy.getValueTreeState().copyState();

        for (int band = 1; band < ParamIDs::numBands; ++band)
        {
            const auto index = (size_t) band;

            for (const auto* parameterID : { ParamIDs::frequency[index],
                                             ParamIDs::gain[index],
                                             ParamIDs::q[index],
                                             ParamIDs::bandBypass[index] })
            {
                // APVTS children are PARAM nodes; the parameter ID is a property.
                auto child = legacyState.getChildWithProperty ("id", parameterID);

                if (child.isValid())
                    legacyState.removeChild (child, nullptr);
            }
        }

        // Band 1's own bypass is newer than v0.1.0 as well.
        if (auto child = legacyState.getChildWithProperty ("id", ParamIDs::bandBypass[0]); child.isValid())
            legacyState.removeChild (child, nullptr);

        // freq, gain, q and bypass are all a v0.1.0 session could hold.
        check (legacyState.getNumChildren() == 4,
               "the legacy fixture only carries the four v0.1.0 parameters");

        juce::MemoryBlock legacyBlock;
        juce::AudioProcessor::copyXmlToBinary (*legacyState.createXml(), legacyBlock);

        MyFirstMasteringEQAudioProcessor migrated;
        migrated.setStateInformation (legacyBlock.getData(), (int) legacyBlock.getSize());

        auto& migratedState = migrated.getValueTreeState();

        check (std::abs (migratedState.getRawParameterValue (ParamIDs::frequency[0])->load() - 5000.0f) < 1.0f,
               "a one-band session restores band 1's frequency");
        check (std::abs (migratedState.getRawParameterValue (ParamIDs::gain[0])->load() - 6.0f) < 1.0e-3f,
               "a one-band session restores band 1's gain");

        bool otherBandsFlat = true;

        for (int band = 1; band < ParamIDs::numBands; ++band)
            otherBandsFlat = otherBandsFlat
                          && std::abs (migratedState.getRawParameterValue (ParamIDs::gain[(size_t) band])->load()) < 1.0e-3f;

        check (otherBandsFlat, "the new bands stay flat when an old session is opened");
    }

    //==========================================================================
    // The same for a session saved by the four-band build: bands 5..8 must come
    // up flat, and the four bands it did carry must survive.
    {
        MyFirstMasteringEQAudioProcessor fourBand;
        setParameterValue (fourBand, ParamIDs::frequency[3], 3000.0f);
        setParameterValue (fourBand, ParamIDs::gain[3], -4.0f);
        setParameterValue (fourBand, ParamIDs::q[3], 2.0f);

        auto fourBandState = fourBand.getValueTreeState().copyState();

        for (int band = 4; band < ParamIDs::numBands; ++band)
        {
            const auto index = (size_t) band;

            for (const auto* parameterID : { ParamIDs::frequency[index],
                                             ParamIDs::gain[index],
                                             ParamIDs::q[index],
                                             ParamIDs::bandBypass[index] })
            {
                if (auto child = fourBandState.getChildWithProperty ("id", parameterID); child.isValid())
                    fourBandState.removeChild (child, nullptr);
            }
        }

        check (fourBandState.getNumChildren() == 4 * 4 + 1,
               "the v0.3.0 fixture carries four bands plus the global bypass");

        juce::MemoryBlock fourBandBlock;
        juce::AudioProcessor::copyXmlToBinary (*fourBandState.createXml(), fourBandBlock);

        MyFirstMasteringEQAudioProcessor migrated;
        migrated.setStateInformation (fourBandBlock.getData(), (int) fourBandBlock.getSize());

        auto& migratedState = migrated.getValueTreeState();

        check (std::abs (migratedState.getRawParameterValue (ParamIDs::frequency[3])->load() - 3000.0f) < 1.0f,
               "a four-band session restores band 4's frequency");
        check (std::abs (migratedState.getRawParameterValue (ParamIDs::gain[3])->load() - (-4.0f)) < 1.0e-3f,
               "a four-band session restores band 4's gain");

        bool addedBandsFlat = true;

        for (int band = 4; band < ParamIDs::numBands; ++band)
            addedBandsFlat = addedBandsFlat
                          && std::abs (migratedState.getRawParameterValue (ParamIDs::gain[(size_t) band])->load()) < 1.0e-3f;

        check (addedBandsFlat, "bands 5..8 stay flat when a four-band session is opened");
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
        check (std::abs (apvts.getRawParameterValue (ParamIDs::gain[0])->load()) < 1.0e-3f,
               "the first preset is a 0 dB pass-through");

        processor.setCurrentProgram (1);
        check (processor.getCurrentProgram() == 1, "setCurrentProgram() selects the program");
        check (std::abs (apvts.getRawParameterValue (ParamIDs::gain[0])->load()) > 0.1f
                   || std::abs (apvts.getRawParameterValue (ParamIDs::frequency[0])->load() - 1000.0f) > 1.0f,
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
    // The GUI tests in pluginval are skipped in CI, so build the editor here and
    // exercise the parts that carry logic: band selection, node dragging and the
    // layout at the smallest allowed size.
    {
        MyFirstMasteringEQAudioProcessor editorProcessor;
        editorProcessor.setPlayConfigDetails (2, 2, 48000.0, 512);
        editorProcessor.prepareToPlay (48000.0, 512);

        std::unique_ptr<MyFirstMasteringEQAudioProcessorEditor> editor (
            static_cast<MyFirstMasteringEQAudioProcessorEditor*> (editorProcessor.createEditor()));

        check (editor != nullptr, "createEditor() returns an editor");

        if (editor != nullptr)
        {
            editor->setSize (480, 500);

            auto* curve = dynamic_cast<FrequencyResponseCurve*> (editor->findChildWithID ("frequencyResponseCurve"));
            auto* frequencySlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("frequencySlider"));
            auto* gainSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("gainSlider"));
            auto* band3Button = dynamic_cast<juce::Button*> (editor->findChildWithID ("band3"));

            check (curve != nullptr && frequencySlider != nullptr
                       && gainSlider != nullptr && band3Button != nullptr,
                   "the graph, the knobs and the band buttons are there");

            auto* constrainer = editor->getConstrainer();
            check (constrainer != nullptr
                       && constrainer->getMinimumWidth() == 480
                       && constrainer->getMinimumHeight() == 500,
                   "480x500 is the smallest size the layout allows");

            if (curve != nullptr && frequencySlider != nullptr
                && gainSlider != nullptr && band3Button != nullptr)
            {
                // Selecting a band must point every knob at that band's values.
                setParameterValue (editorProcessor, ParamIDs::gain[2], -7.0f);
                setParameterValue (editorProcessor, ParamIDs::frequency[2], 4321.0f);

                editor->selectBand (2);

                check (band3Button->getToggleState(), "the band 3 button shows the selection");
                check (std::abs (gainSlider->getValue() - (-7.0)) < 0.01,
                       "band 3's gain reaches the gain knob");
                check (std::abs (frequencySlider->getValue() - 4321.0) < 1.0,
                       "band 3's frequency reaches the frequency knob");

                editor->selectBand (0);

                check (! band3Button->getToggleState(), "the band 3 button clears again");
                check (std::abs (gainSlider->getValue()) < 0.01,
                       "band 1's gain comes back when band 1 is selected");

                // Dragging a node writes through the APVTS, so the knobs follow.
                const auto plot = FrequencyResponseCurve::Geometry::plotFor (curve->getLocalBounds().toFloat());
                setParameterValue (editorProcessor, ParamIDs::frequency[1], 1000.0f);
                setParameterValue (editorProcessor, ParamIDs::gain[1], 0.0f);

                // The axis spans 20 Hz to 20 kHz, so a third of the width is one decade.
                curve->beginNodeDrag (1);
                curve->dragNodeBy (1, { plot.getWidth() / 3.0f, -plot.getHeight() * 0.15f });
                curve->endNodeDrag();
                curve->refreshNow();

                auto& editorState = editorProcessor.getValueTreeState();

                check (std::abs (editorState.getRawParameterValue (ParamIDs::gain[1])->load() - 6.0f) < 0.2f,
                       "dragging a node moves that band's gain");
                check (std::abs (editorState.getRawParameterValue (ParamIDs::frequency[1])->load() - 10000.0f) < 120.0f,
                       "dragging a node moves that band's frequency");
                check (std::abs (editorState.getRawParameterValue (ParamIDs::gain[2])->load() - (-7.0f)) < 0.01f,
                       "dragging one node leaves the other bands alone");

                // The band row carries the solo and band-off switches.
                auto* soloButton = dynamic_cast<juce::Button*> (editor->findChildWithID ("soloButton"));
                auto* bandOffButton = dynamic_cast<juce::Button*> (editor->findChildWithID ("bandBypassButton"));

                check (soloButton != nullptr && bandOffButton != nullptr,
                       "the editor exposes the solo and band-off buttons");

                if (soloButton != nullptr && bandOffButton != nullptr)
                {
                    setParameterValue (editorProcessor, ParamIDs::frequency[1], 3000.0f);
                    setParameterValue (editorProcessor, ParamIDs::gain[1], 12.0f);
                    setParameterValue (editorProcessor, ParamIDs::q[1], 1.0f);

                    // triggerClick() only posts a message, so drive the click path
                    // synchronously the way a real press would.
                    const auto press = [] (juce::Button& button)
                    {
                        button.setToggleState (! button.getToggleState(), juce::sendNotificationSync);
                    };

                    editor->selectBand (1);
                    press (*soloButton);

                    check (soloButton->getToggleState(), "the solo button latches");
                    check (editorProcessor.getSoloBand() == 1, "solo points at the selected band");

                    // The graph has to agree with the sound: only the soloed band
                    // feeds the combined curve.
                    curve->refreshNow();
                    const auto soloDrawn = curve->combinedResponseDbAt (3000.0);
                    const auto bandAlone = waives::peak::responseDb (
                        editorProcessor.getCurrentSampleRate(), { 3000.0f, 12.0f, 1.0f }, 3000.0);

                    check (std::abs (soloDrawn - bandAlone) < 0.1f,
                           "the graph drops the other bands during a solo");

                    editor->selectBand (3);
                    check (editorProcessor.getSoloBand() == 3, "the solo follows the band selection");

                    press (*soloButton);
                    check (editorProcessor.getSoloBand() == -1, "clicking solo again clears it");

                    editor->selectBand (1);
                    curve->refreshNow();
                    const auto withBand = curve->combinedResponseDbAt (3000.0);

                    press (*bandOffButton);
                    check (editorState.getRawParameterValue (ParamIDs::bandBypass[1])->load() > 0.5f,
                           "band off writes the selected band's bypass");

                    curve->refreshNow();
                    const auto withoutBand = curve->combinedResponseDbAt (3000.0);

                    check (std::abs ((withBand - withoutBand) - 12.0f) < 0.2f,
                           "a band that is switched off leaves the combined curve");

                    press (*bandOffButton);
                    check (editorState.getRawParameterValue (ParamIDs::bandBypass[1])->load() < 0.5f,
                           "band off releases again");
                }

                // The thick curve is the sum of the individual bands.
                setParameterValue (editorProcessor, ParamIDs::frequency[0], 500.0f);
                setParameterValue (editorProcessor, ParamIDs::gain[0], 9.0f);
                setParameterValue (editorProcessor, ParamIDs::q[0], 1.0f);
                curve->refreshNow();

                std::array<waives::peak::BandSettings, ParamIDs::numBands> chain {};

                for (int band = 0; band < ParamIDs::numBands; ++band)
                {
                    const auto index = (size_t) band;

                    chain[index] = { editorState.getRawParameterValue (ParamIDs::frequency[index])->load(),
                                     editorState.getRawParameterValue (ParamIDs::gain[index])->load(),
                                     editorState.getRawParameterValue (ParamIDs::q[index])->load() };
                }

                for (const double probeHz : { 500.0, 1000.0, 2000.0 })
                {
                    const auto drawn = curve->combinedResponseDbAt (probeHz);
                    const auto expected = waives::peak::combinedResponseDb (48000.0, chain, probeHz);

                    const auto label = juce::String ("the drawn curve is the sum of the bands at ")
                                     + juce::String (probeHz, 0) + " Hz";

                    check (std::abs (drawn - expected) < 0.05f, label.toRawUTF8());
                }

                // The three sections must not collide at the minimum size.
                check (! curve->getBounds().intersects (frequencySlider->getBounds()),
                       "the graph does not overlap the frequency knob");
                check (! curve->getBounds().intersects (gainSlider->getBounds()),
                       "the graph does not overlap the gain knob");
                check (! curve->getBounds().intersects (band3Button->getBounds()),
                       "the graph does not overlap the band buttons");

                // All eight bands have to sit in the row beside the two switches at
                // the minimum width, so the row is checked as a whole.
                {
                    auto* soloSwitch = dynamic_cast<juce::Button*> (editor->findChildWithID ("soloButton"));
                    auto* lastBandButton = dynamic_cast<juce::Button*> (
                        editor->findChildWithID ("band" + juce::String (ParamIDs::numBands)));

                    bool everyButtonIsThere = true;
                    bool buttonsDoNotOverlap = true;

                    for (int band = 0; band < ParamIDs::numBands; ++band)
                    {
                        auto* button = dynamic_cast<juce::Button*> (
                            editor->findChildWithID ("band" + juce::String (band + 1)));

                        everyButtonIsThere = everyButtonIsThere && button != nullptr;

                        if (button == nullptr || band == 0)
                            continue;

                        auto* previous = dynamic_cast<juce::Button*> (
                            editor->findChildWithID ("band" + juce::String (band)));

                        buttonsDoNotOverlap = buttonsDoNotOverlap
                            && editor->getLocalBounds().contains (button->getBounds())
                            && previous != nullptr
                            && ! button->getBounds().intersects (previous->getBounds());
                    }

                    check (everyButtonIsThere, "every band has its own button");
                    check (buttonsDoNotOverlap, "the band buttons stay inside the editor and do not overlap");
                    check (soloSwitch != nullptr && lastBandButton != nullptr
                               && lastBandButton->getRight() < soloSwitch->getX(),
                           "the last band button still fits left of the solo switch");

                    // The knobs have to reach the bands added after the first release.
                    setParameterValue (editorProcessor, ParamIDs::gain[ParamIDs::numBands - 1], 4.5f);
                    editor->selectBand (ParamIDs::numBands - 1);

                    check (std::abs (gainSlider->getValue() - 4.5) < 0.01,
                           "the last band's gain reaches the gain knob");

                    editor->selectBand (0);
                }

                editor->setSize (900, 760);
                check (curve->getBounds().getHeight() > 0,
                       "the layout survives the largest size");

                // The node is drawn at the Nyquist-clamped frequency, which is below
                // the parameter at low sample rates. A gain-only drag must not carry
                // that drawn value back into the frequency parameter.
                editorProcessor.setPlayConfigDetails (2, 2, 32000.0, 512);
                editorProcessor.prepareToPlay (32000.0, 512);
                setParameterValue (editorProcessor, ParamIDs::frequency[1], 16000.0f);
                setParameterValue (editorProcessor, ParamIDs::gain[1], 0.0f);
                curve->refreshNow();

                curve->beginNodeDrag (1);
                curve->dragNodeBy (1, { 0.0f, -20.0f });
                curve->endNodeDrag();
                curve->refreshNow();

                check (std::abs (editorState.getRawParameterValue (ParamIDs::frequency[1])->load() - 16000.0f) < 1.0f,
                       "a gain-only drag keeps a frequency above the Nyquist clamp");
                check (editorState.getRawParameterValue (ParamIDs::gain[1])->load() > 1.0f,
                       "the same drag still moves the gain");

                // Destroying the editor takes the curve with it, so this has to be
                // the last thing that touches either of them.
                editorProcessor.setSoloBand (2);
                editor.reset();

                check (editorProcessor.getSoloBand() == -1, "closing the editor clears the solo");

                editorProcessor.releaseResources();
            }
        }
    }

    processor.releaseResources();

    return waives::test::report();
}
