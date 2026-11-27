// Headless test driver: instantiates the real AudioProcessor and runs the same
// processBlock() the plugin uses, without a host or an audio device.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestUtilities.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace waives::test;

namespace
{
    void setParameterValue (MyFirstTubeAmpAudioProcessor& processor,
                            const char* parameterID,
                            float realValue)
    {
        auto* parameter = processor.getValueTreeState().getParameter (parameterID);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    }

    void processAll (MyFirstTubeAmpAudioProcessor& processor,
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

    juce::AudioBuffer<float> makeSine (double sampleRate, double frequency, double seconds,
                                       float amplitude, int numChannels = 2)
    {
        juce::AudioBuffer<float> buffer (numChannels, (int) (sampleRate * seconds));
        fillWithSine (buffer, frequency, sampleRate);
        buffer.applyGain (amplitude);
        return buffer;
    }

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

    // Harmonic distortion against the fundamental, steady state only. The amp is
    // noiseless, so this is a clean measure of how hard it is driven.
    double distortionPercent (const juce::AudioBuffer<float>& buffer, double fundamental,
                              double sampleRate)
    {
        // A whole number of periods of the fundamental: a window that ends mid
        // period leaks the fundamental into the harmonic bins.
        const auto settle = (int) (sampleRate * 0.3);
        const auto periodSamples = juce::jmax (1, (int) std::lround (sampleRate / fundamental));
        const auto periods = (buffer.getNumSamples() - settle) / periodSamples - 2;
        const auto length = periods * periodSamples;

        if (length <= 0)
            return 0.0;

        const auto fundamentalLevel = magnitudeAt (buffer, fundamental, sampleRate, 0, settle, length);

        if (fundamentalLevel <= 0.0)
            return 0.0;

        double harmonicEnergy = 0.0;

        for (int harmonic = 2; harmonic <= 6; ++harmonic)
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

    RenderResult render (MyFirstTubeAmpAudioProcessor& processor,
                         double sampleRate,
                         double frequency = 1000.0,
                         float amplitude = 0.5f,
                         int blockSize = 512)
    {
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        auto buffer = makeSine (sampleRate, frequency, 1.0, amplitude);
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

    // A tone stack that is flat and a cabinet that is out of the way, both at
    // 0 dB gain: what comes out should be what went in.
    void makeClean (MyFirstTubeAmpAudioProcessor& processor)
    {
        setParameterValue (processor, ParamIDs::bypass, 0.0f);
        setParameterValue (processor, ParamIDs::gain, 0.0f);
        setParameterValue (processor, ParamIDs::bass, 0.0f);
        setParameterValue (processor, ParamIDs::mid, 0.0f);
        setParameterValue (processor, ParamIDs::treble, 0.0f);
        setParameterValue (processor, ParamIDs::presence, 0.0f);
        setParameterValue (processor, ParamIDs::sag, 0.0f);
        setParameterValue (processor, ParamIDs::output, 0.0f);
        setParameterValue (processor, ParamIDs::cabinet, 0.0f);
    }

    // Bypassed, the plugin has to hand the input back unprocessed but still
    // delayed by the latency it reports: the host compensates for that delay, so
    // a bypassed signal that arrives early would slide against the dry tracks.
    void runBypassCheck (MyFirstTubeAmpAudioProcessor& processor,
                         double sampleRate, int numChannels, int blockSize,
                         const char* what, bool hostBypass = false)
    {
        // The two bypass routes are different code, so each check starts from a
        // known state: the parameter on only for the parameter route, or the
        // host route would pass without processBlockBypassed() doing anything.
        setParameterValue (processor, ParamIDs::bypass, hostBypass ? 0.0f : 1.0f);

        processor.setPlayConfigDetails (numChannels, numChannels, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        const auto latency = processor.getReportedLatency();
        const int numSamples = 8192;

        juce::AudioBuffer<float> input (numChannels, numSamples);
        fillWithSine (input);

        juce::AudioBuffer<float> output (input);

        const auto runBypassed = [&processor, blockSize] (juce::AudioBuffer<float>& target,
                                                          bool viaHost)
        {
            juce::MidiBuffer midi;

            for (int start = 0; start < target.getNumSamples(); start += blockSize)
            {
                const auto length = juce::jmin (blockSize, target.getNumSamples() - start);
                juce::AudioBuffer<float> view (target.getArrayOfWritePointers(),
                                               target.getNumChannels(), start, length);

                if (viaHost)
                    processor.processBlockBypassed (view, midi);
                else
                    processor.processBlock (view, midi);
            }
        };

        // Once to settle the bypass fade and fill the dry delay, then again on a
        // fresh copy for the comparison.
        runBypassed (output, hostBypass);

        output.makeCopyOf (input);
        runBypassed (output, hostBypass);

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

    MyFirstTubeAmpAudioProcessor processor;

    check (processor.getName() == "MyFirstTubeAmp", "getName() returns the plugin name");
    check (processor.getTailLengthSeconds() >= 0.0, "the tail length is reported");
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
    check (processor.getLatencySamples() >= 0, "the oversampler latency is reported");

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
           "the plugin publishes gain/bass/mid/treble/presence/sag/output, cabinet and bypass");

    auto* gainParam     = apvts.getParameter (ParamIDs::gain);
    auto* bassParam     = apvts.getParameter (ParamIDs::bass);
    auto* midParam      = apvts.getParameter (ParamIDs::mid);
    auto* trebleParam   = apvts.getParameter (ParamIDs::treble);
    auto* presenceParam = apvts.getParameter (ParamIDs::presence);
    auto* sagParam      = apvts.getParameter (ParamIDs::sag);
    auto* outputParam   = apvts.getParameter (ParamIDs::output);

    check (gainParam != nullptr && bassParam != nullptr && midParam != nullptr
               && trebleParam != nullptr && presenceParam != nullptr && sagParam != nullptr
               && outputParam != nullptr,
           "every knob has a parameter behind it");

    if (gainParam == nullptr || bassParam == nullptr || presenceParam == nullptr)
    {
        std::printf ("\nCannot continue without parameters.\n");
        return 1;
    }

    check (approxEqual (gainParam->getNormalisableRange().start, 0.0f)
               && approxEqual (gainParam->getNormalisableRange().end, 30.0f),
           "gain range is 0 .. +30 dB");
    check (approxEqual (bassParam->getNormalisableRange().start, -12.0f)
               && approxEqual (bassParam->getNormalisableRange().end, 12.0f),
           "bass range is -12 .. +12 dB");
    check (approxEqual (presenceParam->getNormalisableRange().start, 0.0f),
           "presence is boost only, like a real amp");
    check (approxEqual (gainParam->convertFrom0to1 (gainParam->getDefaultValue()), 6.0f, 1.0e-3f),
           "gain defaults to 6 dB");
    check (approxEqual (sagParam->convertFrom0to1 (sagParam->getDefaultValue()), 30.0f, 1.0e-2f),
           "sag defaults to 30 %");

    const auto textRoundTrips = [] (juce::RangedAudioParameter& param, float realValue)
    {
        const auto first = param.getText (param.convertTo0to1 (realValue), 0);
        const auto second = param.getText (param.getValueForText (first), 0);
        return first == second;
    };

    check (textRoundTrips (*gainParam, 17.25f), "gain text conversion round-trips");
    check (textRoundTrips (*bassParam, -3.5f), "bass text conversion round-trips");
    check (textRoundTrips (*presenceParam, 7.25f), "presence text conversion round-trips");
    check (gainParam->getText (gainParam->convertTo0to1 (12.0f), 0) == "12.00 dB",
           "gain is displayed in dB");

    //==========================================================================
    // The default preset has to be usable straight away: within a few dB of the
    // input level, and warm rather than clean.
    {
        setParameterValue (processor, ParamIDs::bypass, 0.0f);
        processor.setCurrentProgram (0);

        const auto byDefault = render (processor, sampleRate, 400.0, 0.3f);


        check (std::abs (byDefault.rms - (-13.5)) < 6.0, "the default preset is close to the input level");
        check (byDefault.distortion > 0.3 && byDefault.distortion < 8.0,
               "the default preset is warm, not clean and not filthy");
    }

    //==========================================================================
    // The clean path has to be level accurate before anything is driven.
    {
        makeClean (processor);
        const auto clean = render (processor, sampleRate, 200.0, 0.1f);

        // A 0.1 amplitude sine is -26 dB RMS; the amp must not change that.
        std::printf ("      clean rms=%.2f dB thd=%.3f%%\n", clean.rms, clean.distortion);
        check (std::abs (clean.rms - (-23.0)) < 1.0, "the clean amp is level-accurate");
        check (clean.distortion < 1.0, "the clean amp stays clean");
        check (clean.finite, "the clean amp stays finite");
    }

    //==========================================================================
    // Gain: more drive means more harmonics, and the level stays usable.
    {
        makeClean (processor);
        const auto clean = render (processor, sampleRate, 1000.0, 0.3f);

        setParameterValue (processor, ParamIDs::gain, 18.0f);
        const auto crunch = render (processor, sampleRate, 1000.0, 0.3f);

        setParameterValue (processor, ParamIDs::gain, 30.0f);
        const auto lead = render (processor, sampleRate, 1000.0, 0.3f);

        check (crunch.distortion > clean.distortion * 5.0 && crunch.distortion > 15.0,
               "18 dB of gain is audibly driven");
        check (lead.distortion > crunch.distortion, "30 dB is dirtier than 18 dB");
        check (lead.distortion > 15.0, "full gain is heavily saturated");
        check (clean.finite && crunch.finite && lead.finite, "the saturators stay finite");

        // The valves compress, they do not blow up: a 0.3 input through 30 dB of
        // gain must stay below full scale.
        check (lead.peak < 1.0, "a hot signal is compressed, not amplified past full scale");

        // The first stage is biased, so its harmonics are not all odd. A
        // symmetric curve would leave the second harmonic at zero.
        const auto settle = (int) (sampleRate * 0.3);
        const auto periodSamples = (int) std::lround (sampleRate / 1000.0);
        const auto periods = (lead.buffer.getNumSamples() - settle) / periodSamples - 2;
        const auto length = periods * periodSamples;

        const auto fundamental = magnitudeAt (lead.buffer, 1000.0, sampleRate, 0, settle, length);
        const auto second = magnitudeAt (lead.buffer, 2000.0, sampleRate, 0, settle, length);
        const auto third = magnitudeAt (lead.buffer, 3000.0, sampleRate, 0, settle, length);

        check (fundamental > 0.0
                   && second > 0.01 * fundamental
                   && third > 0.01 * fundamental,
               "the valves produce both even and odd harmonics");
    }

    //==========================================================================
    // Tone stack: every control has to move its own band.
    {
        const auto measureBand = [&processor] (double frequency, float bass, float mid,
                                               float treble, float presence)
        {
            makeClean (processor);
            setParameterValue (processor, ParamIDs::bass, bass);
            setParameterValue (processor, ParamIDs::mid, mid);
            setParameterValue (processor, ParamIDs::treble, treble);
            setParameterValue (processor, ParamIDs::presence, presence);

            return render (processor, sampleRate, frequency, 0.1f).rms;
        };

        const auto bassUp   = measureBand (80.0, 12.0f, 0.0f, 0.0f, 0.0f);
        const auto bassDown = measureBand (80.0, -12.0f, 0.0f, 0.0f, 0.0f);
        check (bassUp - bassDown > 9.0, "bass +12 lifts 80 Hz above bass -12");

        const auto midUp   = measureBand (700.0, 0.0f, 12.0f, 0.0f, 0.0f);
        const auto midDown = measureBand (700.0, 0.0f, -12.0f, 0.0f, 0.0f);
        check (midUp - midDown > 9.0, "mid +12 lifts 700 Hz above mid -12");

        const auto trebleUp   = measureBand (5000.0, 0.0f, 0.0f, 12.0f, 0.0f);
        const auto trebleDown = measureBand (5000.0, 0.0f, 0.0f, -12.0f, 0.0f);
        check (trebleUp - trebleDown > 9.0, "treble +12 lifts 5 kHz above treble -12");

        const auto presenceUp   = measureBand (2600.0, 0.0f, 0.0f, 0.0f, 12.0f);
        const auto presenceDown = measureBand (2600.0, 0.0f, 0.0f, 0.0f, 0.0f);
        check (presenceUp - presenceDown > 6.0, "presence +12 lifts 2.6 kHz");

        // The controls must not drag each other around too much: a bass-only
        // move has to leave 5 kHz roughly where it was.
        const auto trebleWithBassDown = measureBand (5000.0, -12.0f, 0.0f, 0.0f, 0.0f);
        const auto trebleWithBassUp   = measureBand (5000.0, 12.0f, 0.0f, 0.0f, 0.0f);
        std::printf ("      bass up=%.2f down=%.2f | mid up=%.2f down=%.2f | treble up=%.2f down=%.2f | presence up=%.2f down=%.2f | 5k bassdown=%.2f bassup=%.2f\n",
                     bassUp, bassDown, midUp, midDown, trebleUp, trebleDown, presenceUp, presenceDown, trebleWithBassDown, trebleWithBassUp);
        check (std::abs (trebleWithBassUp - trebleWithBassDown) < 1.0,
               "the bass control leaves the top end alone");
    }

    //==========================================================================
    // Cabinet: a closed 4x12 has no bottom and no top.
    {
        makeClean (processor);
        setParameterValue (processor, ParamIDs::cabinet, 1.0f);

        const auto withCabHigh = render (processor, sampleRate, 8000.0, 0.1f).rms;
        const auto withCabLow  = render (processor, sampleRate, 40.0, 0.1f).rms;

        setParameterValue (processor, ParamIDs::cabinet, 0.0f);
        const auto withoutCabHigh = render (processor, sampleRate, 8000.0, 0.1f).rms;
        const auto withoutCabLow  = render (processor, sampleRate, 40.0, 0.1f).rms;

        check (withoutCabHigh - withCabHigh > 6.0, "the cabinet takes more than 6 dB off 8 kHz");
        check (withoutCabLow - withCabLow > 6.0, "the cabinet takes more than 6 dB off 40 Hz");

        // Midrange is what a cabinet keeps.
        const auto withCabMid = render (processor, sampleRate, 1500.0, 0.1f).rms;
        setParameterValue (processor, ParamIDs::cabinet, 1.0f);
        const auto withCabMidOn = render (processor, sampleRate, 1500.0, 0.1f).rms;
        std::printf ("      cab 8k on=%.2f off=%.2f | 40Hz on=%.2f off=%.2f | 1.5k on=%.2f off=%.2f\n",
                     withCabHigh, withoutCabHigh, withCabLow, withoutCabLow, withCabMidOn, withCabMid);
        check (std::abs (withCabMidOn - withCabMid) < 4.0, "the cabinet keeps the midrange");
    }

    //==========================================================================
    // Sag: the supply has to duck a sustained signal.
    {
        makeClean (processor);
        setParameterValue (processor, ParamIDs::gain, 18.0f);

        const auto noSag = render (processor, sampleRate, 200.0, 0.5f);

        setParameterValue (processor, ParamIDs::sag, 100.0f);
        const auto withSag = render (processor, sampleRate, 200.0, 0.5f);

        check (noSag.rms - withSag.rms > 1.0, "sag at 100 % ducks a sustained signal");
        check (withSag.finite, "the sag stays finite");
    }

    //==========================================================================
    // Extreme settings must not produce NaN, infinity or a runaway.
    {
        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            setParameterValue (processor, ParamIDs::bypass, 0.0f);
            setParameterValue (processor, ParamIDs::gain, 30.0f);
            setParameterValue (processor, ParamIDs::bass, 12.0f);
            setParameterValue (processor, ParamIDs::mid, 12.0f);
            setParameterValue (processor, ParamIDs::treble, 12.0f);
            setParameterValue (processor, ParamIDs::presence, 12.0f);
            setParameterValue (processor, ParamIDs::sag, 100.0f);
            setParameterValue (processor, ParamIDs::output, 12.0f);
            setParameterValue (processor, ParamIDs::cabinet, 1.0f);

            const auto hot = render (processor, rate, 100.0, 0.95f);

            const auto label = juce::String ("extremes stay finite at ")
                             + juce::String (rate / 1000.0, 1) + " kHz";

            check (hot.finite, label.toRawUTF8());
            // Twleve dB of output trim on top of four boosted bands is allowed
            // to leave full scale; what must not happen is a runaway.
            check (hot.peak < 40.0, "the output trim caps the level, no runaway");
        }
    }

    //==========================================================================
    // A knob move has to reach the sound without a prepareToPlay() in between:
    // a host only calls that on sample-rate or buffer changes.
    {
        makeClean (processor);

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> quiet = makeSine (sampleRate, 1000.0, 1.0, 0.3f);
        processAll (processor, quiet, blockSize);
        const auto quietDistortion = distortionPercent (quiet, 1000.0, sampleRate);

        setParameterValue (processor, ParamIDs::gain, 24.0f);

        juce::AudioBuffer<float> driven = makeSine (sampleRate, 1000.0, 1.0, 0.3f);
        processAll (processor, driven, blockSize);
        const auto drivenDistortion = distortionPercent (driven, 1000.0, sampleRate);

        check (drivenDistortion > quietDistortion * 5.0,
               "a gain move reaches the sound without a fresh prepare");

        // The tone controls rebuild their coefficients from a smoothed ramp, so
        // they need the same check.
        setParameterValue (processor, ParamIDs::gain, 0.0f);
        setParameterValue (processor, ParamIDs::treble, 12.0f);

        juce::AudioBuffer<float> bright = makeSine (sampleRate, 6000.0, 1.0, 0.1f);
        processAll (processor, bright, blockSize);
        const auto brightLevel = rmsDb (bright, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        setParameterValue (processor, ParamIDs::treble, -12.0f);

        juce::AudioBuffer<float> dark = makeSine (sampleRate, 6000.0, 1.0, 0.1f);
        processAll (processor, dark, blockSize);
        const auto darkLevel = rmsDb (dark, 0, (int) (sampleRate * 0.3), (int) (sampleRate * 0.6));

        check (darkLevel < brightLevel - 6.0,
               "a treble move reaches the sound without a fresh prepare");
    }

    //==========================================================================
    // Smoothing: a knob jump inside a long block still ramps.
    {
        makeClean (processor);
        setParameterValue (processor, ParamIDs::output, -12.0f);

        constexpr int longBlock = 4096;
        processor.setPlayConfigDetails (1, 1, sampleRate, longBlock);
        processor.prepareToPlay (sampleRate, longBlock);

        setParameterValue (processor, ParamIDs::output, 12.0f);   // knob jump

        juce::AudioBuffer<float> input (1, longBlock);
        juce::AudioBuffer<float> output (1, longBlock);
        fillWithSine (input, 1000.0, sampleRate);
        input.applyGain (0.05f);
        output = input;

        juce::MidiBuffer midi;
        processor.processBlock (output, midi);

        const auto early = rmsDb (output, 0, 0, 96) - rmsDb (input, 0, 0, 96);
        const auto late  = rmsDb (output, 0, longBlock - 480, 480)
                         - rmsDb (input, 0, longBlock - 480, 480);

        check (early < 6.0, "a 24 dB output jump is ramped, not applied instantly");
        check (late > 9.0, "the ramp reaches the new output level by the end of the block");

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
    }

    //==========================================================================
    // State round-trip through the host's save/load hooks.
    {
        makeClean (processor);
        setParameterValue (processor, ParamIDs::gain, 21.5f);
        setParameterValue (processor, ParamIDs::mid, -4.25f);
        setParameterValue (processor, ParamIDs::sag, 77.0f);
        setParameterValue (processor, ParamIDs::cabinet, 1.0f);

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        check (state.getSize() > 0, "getStateInformation() produces data");

        MyFirstTubeAmpAudioProcessor restored;
        restored.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        restored.prepareToPlay (sampleRate, blockSize);
        restored.setStateInformation (state.getData(), (int) state.getSize());

        auto& restoredState = restored.getValueTreeState();

        check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain)->load() - 21.5f) < 1.0e-3f,
               "state restores the gain");
        check (std::abs (restoredState.getRawParameterValue (ParamIDs::mid)->load() - (-4.25f)) < 1.0e-3f,
               "state restores the mid");
        check (std::abs (restoredState.getRawParameterValue (ParamIDs::sag)->load() - 77.0f) < 1.0e-2f,
               "state restores the sag");
        check (restoredState.getRawParameterValue (ParamIDs::cabinet)->load() > 0.5f,
               "state restores the cabinet switch");

        const char garbage[] = "this is not a plugin state";
        restored.setStateInformation (garbage, (int) sizeof (garbage));
        check (std::abs (restoredState.getRawParameterValue (ParamIDs::gain)->load() - 21.5f) < 1.0e-3f,
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

        const auto gainAtPresetOne = apvts.getRawParameterValue (ParamIDs::gain)->load();
        processor.setCurrentProgram (3);
        check (std::abs (apvts.getRawParameterValue (ParamIDs::gain)->load() - gainAtPresetOne) > 0.5f,
               "presets change the parameters");

        // "Head only" is the preset that switches the cabinet off.
        const auto cabinetPreset = processor.getNumPrograms() - 1;
        processor.setCurrentProgram (cabinetPreset);
        check (apvts.getRawParameterValue (ParamIDs::cabinet)->load() < 0.5f,
               "the last preset leaves the cabinet out");
    }

    //==========================================================================
    // Editor smoke test: the knobs exist, point at their parameters and do not
    // collide with each other at the smallest allowed size.
    {
        MyFirstTubeAmpAudioProcessor editorProcessor;
        editorProcessor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        editorProcessor.prepareToPlay (sampleRate, blockSize);

        std::unique_ptr<MyFirstTubeAmpAudioProcessorEditor> editor (
            static_cast<MyFirstTubeAmpAudioProcessorEditor*> (editorProcessor.createEditor()));

        check (editor != nullptr, "createEditor() returns an editor");

        if (editor != nullptr)
        {
            editor->setSize (560, 470);

            auto* gainSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("gainSlider"));
            auto* bassSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("bassSlider"));
            auto* sagSlider = dynamic_cast<juce::Slider*> (editor->findChildWithID ("sagSlider"));
            auto* cabinetButton = dynamic_cast<juce::Button*> (editor->findChildWithID ("cabinetButton"));

            check (gainSlider != nullptr && bassSlider != nullptr && sagSlider != nullptr,
                   "the gain, bass and sag knobs are there");
            check (cabinetButton != nullptr, "the cabinet switch is there");

            auto* constrainer = editor->getConstrainer();
            check (constrainer != nullptr
                       && constrainer->getMinimumWidth() == 560
                       && constrainer->getMinimumHeight() == 470,
                   "560x470 is the smallest size the layout allows");

            if (gainSlider != nullptr && bassSlider != nullptr && sagSlider != nullptr
                && cabinetButton != nullptr)
            {
                setParameterValue (editorProcessor, ParamIDs::gain, 24.5f);
                setParameterValue (editorProcessor, ParamIDs::bass, -6.5f);

                check (std::abs (gainSlider->getValue() - 24.5) < 0.01,
                       "the gain parameter reaches its knob");
                check (std::abs (bassSlider->getValue() - (-6.5)) < 0.01,
                       "the bass parameter reaches its knob");

                check (! bassSlider->getBounds().intersects (gainSlider->getBounds()),
                       "the knobs do not overlap at the minimum size");
                check (! sagSlider->getBounds().intersects (cabinetButton->getBounds()),
                       "the cabinet switch does not overlap the sag knob");

                editor->setSize (1000, 760);
                check (sagSlider->getBounds().getHeight() > 0,
                       "the layout survives the largest size");
            }

            editor.reset();
            editorProcessor.releaseResources();
        }
    }

    processor.releaseResources();

    return waives::test::report();
}
