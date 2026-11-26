#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    // A range whose 0..1 midpoint lands on `centre`, i.e. a log-style control.
    juce::NormalisableRange<float> makeLogRange (float minValue, float maxValue, float centre)
    {
        juce::NormalisableRange<float> range (minValue, maxValue);
        range.setSkewForCentre (centre);
        return range;
    }

    // Hosts (and the CLAP validator) require value -> text -> value -> text to be
    // stable, so every parameter formats itself instead of using JUCE's default
    // seven decimals.
    juce::String formatDecibels (float db, int)    { return juce::String (db, 2) + " dB"; }
    juce::String formatPercent (float percent, int) { return juce::String (percent, 1) + " %"; }
    juce::String formatDrive (float drive, int)     { return juce::String (drive, 2); }

    juce::String formatFrequency (float hz, int)
    {
        if (std::round (hz) >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";

        return juce::String (hz, 0) + " Hz";
    }

    float parseFrequency (const juce::String& text)
    {
        const auto trimmed = text.trim();
        const auto numeric = trimmed.getFloatValue();

        return trimmed.containsIgnoreCase ("k") ? numeric * 1000.0f : numeric;
    }

    // Tape speed and head geometry are fixed: a gentle record-side treble boost
    // (pre-emphasis) that is undone on playback, and the low-frequency head bump
    // where the playback head picks up more flux.
    constexpr double preEmphasisHz     = 3000.0;
    constexpr double preEmphasisDb     = 4.0;
    constexpr double headBumpHz        = 65.0;
    constexpr double headBumpDb        = 2.5;
    constexpr double dcBlockerHz       = 12.0;
    constexpr double toneQ             = 0.6;

    // Pitch modulation depths at 100 %.
    constexpr double wowMilliseconds      = 3.0;
    constexpr double flutterMilliseconds  = 0.35;

    constexpr double wowRateHz            = 0.63;   // the slow drift of the capstan
    constexpr double flutterRateHz        = 6.3;    // reel eccentricity
    constexpr double flutterFastRateHz    = 9.7;    // a second partial, so it wobbles

    // The bias in the saturator is what produces the even harmonics; without it
    // a symmetric curve only adds odd ones and sounds like a fuzz box.
    constexpr float saturationBias = 0.08f;

    struct FactoryPreset
    {
        const char* name;
        float inputDb;
        float drive;
        float toneHz;
        float wowPercent;
        float flutterPercent;
        float hissDb;
        float mixPercent;
        float outputDb;
    };

    constexpr FactoryPreset factoryPresets[] = {
        { "Default",          0.0f, 2.0f, 12000.0f, 25.0f, 15.0f, -84.0f, 100.0f, 0.0f },
        { "Warm slap",        0.0f, 5.0f,  9000.0f, 30.0f, 20.0f, -78.0f, 100.0f, 0.0f },
        { "Lo-fi cassette",   1.0f, 7.0f,  4500.0f, 60.0f, 45.0f, -66.0f, 100.0f, -0.5f },
        { "Mastering glue",  -2.0f, 1.5f, 16000.0f,  8.0f,  4.0f, -96.0f, 100.0f, 0.5f },
        { "Parallel crush",   0.0f, 9.0f,  6000.0f, 20.0f, 10.0f, -72.0f,  40.0f, 0.0f },
    };

    constexpr int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
MyFirstTapeMachineAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    constexpr int versionHint = 1;

    const auto addDecibels = [&layout] (const char* id, const char* name,
                                        float minimum, float maximum, float defaultValue,
                                        const juce::String& suffix)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { minimum, maximum, 0.01f },
            defaultValue,
            juce::AudioParameterFloatAttributes().withLabel (suffix)
                .withStringFromValueFunction (formatDecibels)));
    };

    addDecibels (ParamIDs::input,  "Input",  -12.0f, 12.0f, 0.0f, "dB");
    addDecibels (ParamIDs::output, "Output", -12.0f, 12.0f, 0.0f, "dB");

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::drive, versionHint }, "Drive",
        juce::NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (formatDrive)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::tone, versionHint }, "Tone",
        makeLogRange (2000.0f, 18000.0f, 6000.0f), 12000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction (formatFrequency)
            .withValueFromStringFunction (parseFrequency)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::wow, versionHint }, "Wow",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 25.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction (formatPercent)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::flutter, versionHint }, "Flutter",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction (formatPercent)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::hiss, versionHint }, "Hiss",
        juce::NormalisableRange<float> { -96.0f, -24.0f, 0.1f }, -84.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction (formatDecibels)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::mix, versionHint }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction (formatPercent)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::bypass, versionHint }, "Bypass", false));

    return layout;
}

MyFirstTapeMachineAudioProcessor::MyFirstTapeMachineAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inputParameter   = apvts.getRawParameterValue (ParamIDs::input);
    driveParameter   = apvts.getRawParameterValue (ParamIDs::drive);
    toneParameter    = apvts.getRawParameterValue (ParamIDs::tone);
    wowParameter     = apvts.getRawParameterValue (ParamIDs::wow);
    flutterParameter = apvts.getRawParameterValue (ParamIDs::flutter);
    hissParameter    = apvts.getRawParameterValue (ParamIDs::hiss);
    mixParameter     = apvts.getRawParameterValue (ParamIDs::mix);
    outputParameter  = apvts.getRawParameterValue (ParamIDs::output);
    bypassParameter  = apvts.getRawParameterValue (ParamIDs::bypass);
}

MyFirstTapeMachineAudioProcessor::~MyFirstTapeMachineAudioProcessor() = default;

//==============================================================================
void MyFirstTapeMachineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBlockSize.store (samplesPerBlock, std::memory_order_relaxed);
    activeChannels.store (juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()),
                          std::memory_order_relaxed);

    const auto channels = juce::jmax (1, getTotalNumOutputChannels());
    const auto maximumBlockSize = juce::jmax (1, samplesPerBlock);

    // 2x is enough for a tanh curve this gentle, and it keeps the latency the
    // host has to compensate at a couple of samples.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) channels, 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true);
    oversampling->initProcessing ((size_t) maximumBlockSize);
    oversampling->reset();

    oversamplingLatency = oversampling->getLatencyInSamples();

    const auto maxWowSamples = wowMilliseconds * 0.001 * sampleRate;
    const auto maxFlutterSamples = flutterMilliseconds * 0.001 * sampleRate;

    // The modulated read moves between base - depth and base + depth, so the
    // base has to stay above the depth for the read never to reach the write
    // head. The extra two samples are the interpolation margin.
    baseDelaySamples = (float) (maxWowSamples + maxFlutterSamples + 2.0);

    const auto capacity = (int) std::ceil (baseDelaySamples) + oversamplingLatency + 8;
    delayLine.prepare (channels, capacity);

    dryTapBuffer.setSize (channels, maximumBlockSize, false, true, false);
    dryBuffer.setSize (channels, maximumBlockSize, false, true, false);

    constexpr double smoothingSeconds = 0.02;

    smoothedInputGain.reset (sampleRate, smoothingSeconds);
    smoothedOutputGain.reset (sampleRate, smoothingSeconds);
    smoothedDrive.reset (sampleRate, smoothingSeconds);
    smoothedToneHz.reset (sampleRate, smoothingSeconds);
    smoothedWowDepth.reset (sampleRate, smoothingSeconds);
    smoothedFlutterDepth.reset (sampleRate, smoothingSeconds);

    smoothedInputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputParameter->load()));
    smoothedOutputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputParameter->load()));
    smoothedDrive.setCurrentAndTargetValue (juce::jmax (0.01f, driveParameter->load()));
    smoothedToneHz.setCurrentAndTargetValue (toneParameter->load());
    smoothedWowDepth.setCurrentAndTargetValue (
        (float) (wowMilliseconds * 0.001 * sampleRate) * wowParameter->load() * 0.01f);
    smoothedFlutterDepth.setCurrentAndTargetValue (
        (float) (flutterMilliseconds * 0.001 * sampleRate) * flutterParameter->load() * 0.01f);

    // The dry tap lags the write head by the base delay plus the oversampler's
    // own latency; without this the mix knob would comb filter.
    smoothedMix.reset (sampleRate, smoothingSeconds);
    smoothedMix.setCurrentAndTargetValue (mixParameter->load() * 0.01f);

    smoothedWet.reset (sampleRate, 0.01);
    smoothedWet.setCurrentAndTargetValue (bypassParameter->load() > 0.5f ? 0.0f : 1.0f);

    for (auto* filter : { &preEmphasis, &headBump, &deEmphasis, &toneFilter, &dcBlocker })
        for (auto& biquad : *filter)
            biquad.reset();

    for (auto& source : noiseSources)
        source.seed (0x2545f491u);

    wowPhase = 0.0;
    flutterPhase = 0.0;
    flutterPhaseFast = 0.25;

    updateCoefficients();

    // The delay line is part of the sound, so the host has to know about it.
    setLatencySamples ((int) std::lround (baseDelaySamples) + oversamplingLatency);
}

void MyFirstTapeMachineAudioProcessor::releaseResources()
{
}

bool MyFirstTapeMachineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    return mainOutput == layouts.getMainInputChannelSet();
}

void MyFirstTapeMachineAudioProcessor::updateCoefficients() noexcept
{
    const auto sampleRate = getCurrentSampleRate();

    if (sampleRate <= 0.0)
        return;

    const auto preEmphasisCoefficients = waives::tape::highShelf (sampleRate, preEmphasisHz, preEmphasisDb);
    const auto headBumpCoefficients    = waives::tape::lowShelf  (sampleRate, headBumpHz, headBumpDb);
    const auto deEmphasisCoefficients  = waives::tape::highShelf (sampleRate, preEmphasisHz, -preEmphasisDb);
    const auto toneCoefficients        = waives::tape::lowPass   (sampleRate,
                                                                  smoothedToneHz.getCurrentValue(),
                                                                  toneQ);
    const auto dcCoefficients          = waives::tape::highPass  (sampleRate, dcBlockerHz, 0.707);

    for (size_t channel = 0; channel < preEmphasis.size(); ++channel)
    {
        preEmphasis[channel].setCoefficients (preEmphasisCoefficients);
        headBump[channel].setCoefficients (headBumpCoefficients);
        deEmphasis[channel].setCoefficients (deEmphasisCoefficients);
        toneFilter[channel].setCoefficients (toneCoefficients);
        dcBlocker[channel].setCoefficients (dcCoefficients);
    }
}

//==============================================================================
void MyFirstTapeMachineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    // Decaying feedback in the filters and the delay line can fall into
    // denormal range; this sets the FTZ/DAZ flags for the block.
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    for (int channel = getTotalNumInputChannels(); channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    const int channelsToProcess = juce::jmin (numOutputChannels, 2);

    smoothedInputGain.setTargetValue (juce::Decibels::decibelsToGain (inputParameter->load()));
    smoothedOutputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParameter->load()));
    smoothedDrive.setTargetValue (juce::jmax (0.01f, driveParameter->load()));
    smoothedToneHz.setTargetValue (toneParameter->load());
    smoothedMix.setTargetValue (mixParameter->load() * 0.01f);

    const auto sampleRate = getCurrentSampleRate();
    smoothedWowDepth.setTargetValue ((float) (wowMilliseconds * 0.001 * sampleRate)
                                     * wowParameter->load() * 0.01f);
    smoothedFlutterDepth.setTargetValue ((float) (flutterMilliseconds * 0.001 * sampleRate)
                                         * flutterParameter->load() * 0.01f);

    smoothedWet.setTargetValue (bypassParameter->load() > 0.5f ? 0.0f : 1.0f);

    // A host may hand over a block longer than the one announced in
    // prepareToPlay(), so the buffers are filled in chunks of their own size
    // instead of being reallocated on the audio thread.
    const int chunkSize = juce::jmax (1, dryBuffer.getNumSamples());

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int length = juce::jmin (chunkSize, numSamples - start);
        processChunk (buffer, start, length, channelsToProcess);
    }

    activeChannels.store (numOutputChannels, std::memory_order_relaxed);
}

void MyFirstTapeMachineAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer,
                                                     int start,
                                                     int length,
                                                     int channelsToProcess)
{
    // Dry audio is only needed while the wet signal is faded in or out: a fully
    // processed block works in place, and a fully bypassed block is left alone.
    // The chain still runs while bypassed so its state tracks the input.
    const bool needsDry = smoothedWet.isSmoothing() || smoothedWet.getCurrentValue() < 1.0f;

    if (needsDry)
        for (int channel = 0; channel < channelsToProcess; ++channel)
            dryBuffer.copyFrom (channel, 0, buffer, channel, start, length);

    // Retuning in bounded steps keeps a tone-knob move smooth without rebuilding
    // coefficients for every sample.
    constexpr int maximumRetuneInterval = 64;

    for (int offset = 0; offset < length; offset += maximumRetuneInterval)
    {
        const int step = juce::jmin (maximumRetuneInterval, length - offset);
        const int segmentStart = start + offset;

        if (smoothedToneHz.isSmoothing())
        {
            smoothedToneHz.skip (step);
            updateCoefficients();
        }

        const auto sampleRate = getCurrentSampleRate();
        const auto wowPhaseIncrement = wowRateHz / sampleRate;
        const auto flutterPhaseIncrement = flutterRateHz / sampleRate;
        const auto flutterFastPhaseIncrement = flutterFastRateHz / sampleRate;

        // Pass 1: into the delay line, out with the wow/flutter modulation, plus
        // the aligned dry tap the mix knob crossfades with.
        for (int i = 0; i < step; ++i)
        {
            const auto wowDepth = smoothedWowDepth.getNextValue();
            const auto flutterDepth = smoothedFlutterDepth.getNextValue();

            const auto modulation = wowDepth * (float) std::sin (twoPi * wowPhase)
                                  + flutterDepth * (float) (0.7 * std::sin (twoPi * flutterPhase)
                                                            + 0.3 * std::sin (twoPi * flutterPhaseFast));

            wowPhase += wowPhaseIncrement;
            flutterPhase += flutterPhaseIncrement;
            flutterPhaseFast += flutterFastPhaseIncrement;

            wowPhase -= std::floor (wowPhase);
            flutterPhase -= std::floor (flutterPhase);
            flutterPhaseFast -= std::floor (flutterPhaseFast);

            const auto inputGain = smoothedInputGain.getNextValue();

            for (int channel = 0; channel < channelsToProcess; ++channel)
                delayLine.write (channel, buffer.getSample (channel, segmentStart + i) * inputGain);

            delayLine.advance();

            for (int channel = 0; channel < channelsToProcess; ++channel)
            {
                buffer.setSample (channel, segmentStart + i,
                                  delayLine.read (channel, baseDelaySamples + modulation));
                dryTapBuffer.setSample (channel, i,
                                        delayLine.read (channel, baseDelaySamples
                                                                    + (float) oversamplingLatency));
            }
        }

        // Pass 2: the saturator, oversampled so its harmonics do not alias.
        if (oversampling != nullptr)
        {
            auto block = juce::dsp::AudioBlock<float> (buffer)
                             .getSubsetChannelBlock (0, (size_t) channelsToProcess)
                             .getSubBlock ((size_t) segmentStart, (size_t) step);

            auto oversampled = oversampling->processSamplesUp (block);

            // 0.05 is a straight line for all practical purposes; 4 compresses a
            // -6 dBFS sine by about 6 dB. The bias grows with drive as well, so
            // the even harmonics arrive with the crunch instead of at rest.
            const auto drive = smoothedDrive.getCurrentValue() * 0.1f;
            smoothedDrive.skip (step);
            const auto curveAmount = 0.05f + 3.95f * drive;
            const auto bias = saturationBias * drive;

            for (size_t channel = 0; channel < oversampled.getNumChannels(); ++channel)
            {
                auto* samples = oversampled.getChannelPointer (channel);

                for (size_t i = 0; i < oversampled.getNumSamples(); ++i)
                    samples[i] = waives::tape::saturate (samples[i], curveAmount, bias);
            }

            oversampling->processSamplesDown (block);
        }

        // Pass 3: playback EQ, tone, DC block, hiss, mix and the output trim.

        // A uniform generator between -1 and 1 has an RMS of 1/sqrt(3), so the
        // hiss parameter can mean an RMS level.
        const auto hissGain = juce::Decibels::decibelsToGain (hissParameter->load()) * 1.7320508f;

        for (int i = 0; i < step; ++i)
        {
            const auto mix = smoothedMix.getNextValue();
            const auto outputGain = smoothedOutputGain.getNextValue();

            for (int channel = 0; channel < channelsToProcess; ++channel)
            {
                auto value = headBump[(size_t) channel].processSample (
                                 buffer.getSample (channel, segmentStart + i));
                value = deEmphasis[(size_t) channel].processSample (value);
                value = toneFilter[(size_t) channel].processSample (value);
                value = dcBlocker[(size_t) channel].processSample (value);

                value += noiseSources[(size_t) channel].next() * hissGain;

                const auto dryTap = dryTapBuffer.getSample (channel, i);
                const auto wet = dryTap + (value - dryTap) * mix;

                buffer.setSample (channel, segmentStart + i, wet * outputGain);
            }
        }
    }

    if (! needsDry)
    {
        for (int i = 0; i < length; ++i)
            for (int channel = 0; channel < channelsToProcess; ++channel)
                meterLevels[(size_t) channel].store (
                    std::max (meterLevels[(size_t) channel].load (std::memory_order_relaxed),
                              std::abs (buffer.getSample (channel, start + i))),
                    std::memory_order_relaxed);

        return;
    }

    for (int i = 0; i < length; ++i)
    {
        const auto wetAmount = smoothedWet.getNextValue();

        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* samples = buffer.getWritePointer (channel) + start;
            const auto dry = dryBuffer.getSample (channel, i);

            samples[i] = dry + (samples[i] - dry) * wetAmount;

            meterLevels[(size_t) channel].store (
                std::max (meterLevels[(size_t) channel].load (std::memory_order_relaxed),
                          std::abs (samples[i])),
                std::memory_order_relaxed);
        }
    }
}

float MyFirstTapeMachineAudioProcessor::getMeterLevel (int channel) const noexcept
{
    if (! juce::isPositiveAndBelow (channel, (int) meterLevels.size()))
        return 0.0f;

    return meterLevels[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* MyFirstTapeMachineAudioProcessor::createEditor()
{
    return new MyFirstTapeMachineAudioProcessorEditor (*this);
}

bool MyFirstTapeMachineAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String MyFirstTapeMachineAudioProcessor::getName() const
{
    return "MyFirstTapeMachine";
}

bool MyFirstTapeMachineAudioProcessor::acceptsMidi() const       { return false; }
bool MyFirstTapeMachineAudioProcessor::producesMidi() const      { return false; }
bool MyFirstTapeMachineAudioProcessor::isMidiEffect() const      { return false; }

double MyFirstTapeMachineAudioProcessor::getTailLengthSeconds() const
{
    // Long enough for the delay line and the filter tails to die away.
    return 0.2;
}

//==============================================================================
int MyFirstTapeMachineAudioProcessor::getNumPrograms()      { return numFactoryPresets; }
int MyFirstTapeMachineAudioProcessor::getCurrentProgram()   { return currentProgram.load (std::memory_order_relaxed); }

void MyFirstTapeMachineAudioProcessor::setCurrentProgram (int index)
{
    applyFactoryPreset (index);
}

void MyFirstTapeMachineAudioProcessor::applyFactoryPreset (int index)
{
    if (index < 0 || index >= numFactoryPresets)
        return;

    const auto& preset = factoryPresets[index];

    // Notifying the host keeps its preset display and automation in step.
    const auto apply = [this] (const char* parameterID, float realValue)
    {
        auto* parameter = apvts.getParameter (parameterID);

        jassert (parameter != nullptr);

        if (parameter != nullptr)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    };

    apply (ParamIDs::input,   preset.inputDb);
    apply (ParamIDs::drive,   preset.drive);
    apply (ParamIDs::tone,    preset.toneHz);
    apply (ParamIDs::wow,     preset.wowPercent);
    apply (ParamIDs::flutter, preset.flutterPercent);
    apply (ParamIDs::hiss,    preset.hissDb);
    apply (ParamIDs::mix,     preset.mixPercent);
    apply (ParamIDs::output,  preset.outputDb);

    currentProgram.store (index, std::memory_order_relaxed);
}

const juce::String MyFirstTapeMachineAudioProcessor::getProgramName (int index)
{
    if (index < 0 || index >= numFactoryPresets)
        return {};

    return factoryPresets[index].name;
}

void MyFirstTapeMachineAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void MyFirstTapeMachineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("programIndex", getCurrentProgram(), nullptr);

    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void MyFirstTapeMachineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    const auto index = (int) apvts.state.getProperty ("programIndex", 0);
    currentProgram.store (juce::jlimit (0, numFactoryPresets - 1, index), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyFirstTapeMachineAudioProcessor();
}
