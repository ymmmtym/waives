#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Hosts (and the CLAP validator) require value -> text -> value -> text to be
    // stable, so every parameter formats itself.
    juce::String formatDecibels (float db, int)    { return juce::String (db, 2) + " dB"; }
    juce::String formatPercent (float percent, int) { return juce::String (percent, 0) + " %"; }

    // The two valves. The bias is what puts even harmonics into the first stage;
    // the power stage runs a harder, symmetric curve.
    constexpr float preampBias = 0.15f;
    constexpr float powerKnee  = 1.6f;

    // Coupling capacitor between the stages.
    constexpr double interstageHz = 20.0;
    constexpr double dcBlockerHz  = 15.0;

    // Tone stack: a bass shelf, a mid bell, a treble shelf and the presence
    // control. A passive stack on a real amp interacts; independent filters are
    // the usual approximation and keep the knobs predictable.
    constexpr double bassHz      = 100.0;
    constexpr double midHz       = 700.0;
    constexpr double midQ        = 0.8;
    constexpr double trebleHz    = 3200.0;
    constexpr double presenceHz  = 2600.0;
    constexpr double presenceQ   = 0.9;

    // A closed 4x12: no bottom below ~75 Hz, nothing much above ~5 kHz, and the
    // mid honk around 900 Hz that makes it read as a guitar cabinet.
    constexpr double cabinetHighPassHz = 75.0;
    constexpr double cabinetLowPassHz  = 5000.0;
    constexpr double cabinetDipHz      = 900.0;
    constexpr double cabinetDipDb      = -3.0;
    constexpr double cabinetDipQ       = 1.1;

    // Supply sag: quick to duck, slow to recover.
    constexpr double sagAttackMs  = 5.0;
    constexpr double sagReleaseMs = 250.0;

    // With the sag at 100 % a sustained signal must not mute the amp.
    constexpr float minimumSagGain = 0.3f;

    struct FactoryPreset
    {
        const char* name;
        float gainDb;
        float bassDb;
        float midDb;
        float trebleDb;
        float presenceDb;
        float sagPercent;
        float outputDb;
        bool cabinet;
    };

    constexpr FactoryPreset factoryPresets[] = {
        // name             gain   bass   mid  treble pres  sag   out   cab
        { "Default",         6.0f,  2.0f,  0.0f,  2.0f,  3.0f, 30.0f,  0.0f, true },
        { "Clean headroom",  0.0f,  1.0f,  0.0f,  1.0f,  2.0f, 10.0f,  2.0f, true },
        { "Crunch",         16.0f,  2.0f,  2.0f,  2.0f,  4.0f, 40.0f, -3.0f, true },
        { "Lead",           24.0f,  3.0f,  4.0f,  3.0f,  6.0f, 55.0f, -6.0f, true },
        { "Head only",       6.0f,  2.0f,  0.0f,  2.0f,  3.0f, 30.0f,  2.0f, false },
    };

    constexpr int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
MyFirstTubeAmpAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    constexpr int versionHint = 1;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::gain, versionHint }, "Gain",
        juce::NormalisableRange<float> { 0.0f, 30.0f, 0.01f }, 6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction (formatDecibels)));

    const auto addTone = [&layout] (const char* id, const char* name,
                                    float minimum, float maximum, float defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, versionHint }, name,
            juce::NormalisableRange<float> { minimum, maximum, 0.01f },
            defaultValue,
            juce::AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction (formatDecibels)));
    };

    addTone (ParamIDs::bass,     "Bass",     -12.0f, 12.0f, 2.0f);
    addTone (ParamIDs::mid,      "Mid",      -12.0f, 12.0f, 0.0f);
    addTone (ParamIDs::treble,   "Treble",   -12.0f, 12.0f, 2.0f);

    // Presence is a boost: it is the top octave of the power stage, and real
    // amps do not have a "negative presence".
    addTone (ParamIDs::presence, "Presence",   0.0f, 12.0f, 3.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::sag, versionHint }, "Sag",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction (formatPercent)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::output, versionHint }, "Output",
        juce::NormalisableRange<float> { -24.0f, 12.0f, 0.01f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction (formatDecibels)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::cabinet, versionHint }, "Cabinet", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::bypass, versionHint }, "Bypass", false));

    return layout;
}

MyFirstTubeAmpAudioProcessor::MyFirstTubeAmpAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    gainParameter     = apvts.getRawParameterValue (ParamIDs::gain);
    bassParameter     = apvts.getRawParameterValue (ParamIDs::bass);
    midParameter      = apvts.getRawParameterValue (ParamIDs::mid);
    trebleParameter   = apvts.getRawParameterValue (ParamIDs::treble);
    presenceParameter = apvts.getRawParameterValue (ParamIDs::presence);
    sagParameter      = apvts.getRawParameterValue (ParamIDs::sag);
    outputParameter   = apvts.getRawParameterValue (ParamIDs::output);
    cabinetParameter  = apvts.getRawParameterValue (ParamIDs::cabinet);
    bypassParameter   = apvts.getRawParameterValue (ParamIDs::bypass);
}

MyFirstTubeAmpAudioProcessor::~MyFirstTubeAmpAudioProcessor() = default;

//==============================================================================
void MyFirstTubeAmpAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBlockSize.store (samplesPerBlock, std::memory_order_relaxed);
    activeChannels.store (juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()),
                          std::memory_order_relaxed);

    const auto channels = juce::jmax (1, getTotalNumOutputChannels());
    const auto maximumBlockSize = juce::jmax (1, samplesPerBlock);

    // 4x: a hard-clipped guitar signal is the worst case for aliasing.
    // useIntegerLatency: the oversampler pads its own latency to whole samples,
    // which is what lets the dry path be delayed by exactly that much and keep
    // the bypass latency matched.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) channels, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    oversampling->initProcessing ((size_t) maximumBlockSize);
    oversampling->reset();

    oversamplingLatency = oversampling->getLatencyInSamples();
    dryDelaySamples = juce::jmax (1, oversamplingLatency);
    dryDelayLine.prepare (channels, dryDelaySamples + 4);

    constexpr double smoothingSeconds = 0.02;

    smoothedInputGain.reset (sampleRate, smoothingSeconds);
    smoothedOutputGain.reset (sampleRate, smoothingSeconds);
    smoothedSag.reset (sampleRate, smoothingSeconds);
    smoothedBassDb.reset (sampleRate, smoothingSeconds);
    smoothedMidDb.reset (sampleRate, smoothingSeconds);
    smoothedTrebleDb.reset (sampleRate, smoothingSeconds);
    smoothedPresenceDb.reset (sampleRate, smoothingSeconds);

    smoothedInputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (gainParameter->load()));
    smoothedOutputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputParameter->load()));
    smoothedSag.setCurrentAndTargetValue (sagParameter->load() * 0.01f);
    smoothedBassDb.setCurrentAndTargetValue (bassParameter->load());
    smoothedMidDb.setCurrentAndTargetValue (midParameter->load());
    smoothedTrebleDb.setCurrentAndTargetValue (trebleParameter->load());
    smoothedPresenceDb.setCurrentAndTargetValue (presenceParameter->load());

    // Switching the cabinet in or out crossfades, so the filters always run.
    smoothedCabinet.reset (sampleRate, 0.01);
    smoothedCabinet.setCurrentAndTargetValue (cabinetParameter->load() > 0.5f ? 1.0f : 0.0f);

    smoothedWet.reset (sampleRate, 0.01);
    smoothedWet.setCurrentAndTargetValue (bypassParameter->load() > 0.5f ? 0.0f : 1.0f);

    for (auto* filters : { &bassFilter, &midFilter, &trebleFilter, &presenceFilter,
                            &cabinetHighPass, &cabinetLowPass, &cabinetDip, &dcBlocker })
        for (auto& biquad : *filters)
            biquad.reset();

    for (auto& stage : interstage)
    {
        stage.setCutoff (sampleRate * 4.0, interstageHz);   // runs on the oversampled rate
        stage.reset();
    }

    sagEnvelope.prepare (sampleRate, sagAttackMs, sagReleaseMs);

    dryBuffer.setSize (channels, maximumBlockSize, false, true, false);

    updateCoefficients();

    setLatencySamples (oversamplingLatency);
    reportedLatencySamples.store (oversamplingLatency, std::memory_order_relaxed);
}

void MyFirstTubeAmpAudioProcessor::releaseResources()
{
}

bool MyFirstTubeAmpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    return mainOutput == layouts.getMainInputChannelSet();
}

void MyFirstTubeAmpAudioProcessor::updateCoefficients() noexcept
{
    const auto sampleRate = getCurrentSampleRate();

    if (sampleRate <= 0.0)
        return;

    const auto bass     = waives::dsp::lowShelf  (sampleRate, bassHz, smoothedBassDb.getCurrentValue());
    const auto mid      = waives::dsp::bell      (sampleRate, midHz, smoothedMidDb.getCurrentValue(), midQ);
    const auto treble   = waives::dsp::highShelf (sampleRate, trebleHz, smoothedTrebleDb.getCurrentValue());
    const auto presence = waives::dsp::bell      (sampleRate, presenceHz,
                                                  smoothedPresenceDb.getCurrentValue(), presenceQ);

    const auto cabinetHP = waives::dsp::highPass (sampleRate, cabinetHighPassHz, 0.7);
    const auto cabinetLP = waives::dsp::lowPass  (sampleRate, cabinetLowPassHz, 0.7);
    const auto cabinetNotch = waives::dsp::bell  (sampleRate, cabinetDipHz, cabinetDipDb, cabinetDipQ);

    const auto dc = waives::dsp::highPass (sampleRate, dcBlockerHz, 0.707);

    for (size_t channel = 0; channel < bassFilter.size(); ++channel)
    {
        bassFilter[channel].setCoefficients (bass);
        midFilter[channel].setCoefficients (mid);
        trebleFilter[channel].setCoefficients (treble);
        presenceFilter[channel].setCoefficients (presence);
        cabinetHighPass[channel].setCoefficients (cabinetHP);
        cabinetLowPass[channel].setCoefficients (cabinetLP);
        cabinetDip[channel].setCoefficients (cabinetNotch);
        dcBlocker[channel].setCoefficients (dc);
    }
}

//==============================================================================
void MyFirstTubeAmpAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                         juce::MidiBuffer& midiMessages)
{
    // The host's bypass must not skip the latency the host compensates for, so
    // the chain still runs and the output is the delayed, unprocessed dry signal.
    hostBypassRequested.store (true, std::memory_order_relaxed);
    processBlock (buffer, midiMessages);
    hostBypassRequested.store (false, std::memory_order_relaxed);
}

void MyFirstTubeAmpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                 juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    // The clip stages and the filters can produce tails that fall into
    // denormal range; this sets the FTZ/DAZ flags for the block.
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    for (int channel = getTotalNumInputChannels(); channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    const int channelsToProcess = juce::jmin (numOutputChannels, 2);

    smoothedInputGain.setTargetValue (juce::Decibels::decibelsToGain (gainParameter->load()));
    smoothedOutputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParameter->load()));
    smoothedSag.setTargetValue (sagParameter->load() * 0.01f);
    smoothedBassDb.setTargetValue (bassParameter->load());
    smoothedMidDb.setTargetValue (midParameter->load());
    smoothedTrebleDb.setTargetValue (trebleParameter->load());
    smoothedPresenceDb.setTargetValue (presenceParameter->load());

    smoothedCabinet.setTargetValue (cabinetParameter->load() > 0.5f ? 1.0f : 0.0f);
    // The parameter and the host's bypass do the same thing, and both fade, so
    // the bypassed signal stays latency matched instead of jumping back in time.
    const bool bypassed = hostBypassRequested.load (std::memory_order_relaxed)
                       || bypassParameter->load() > 0.5f;

    smoothedWet.setTargetValue (bypassed ? 0.0f : 1.0f);

    // A host may hand over a block longer than the one announced in
    // prepareToPlay(), so the buffers are filled in chunks of their own size.
    const int chunkSize = juce::jmax (1, dryBuffer.getNumSamples());

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int length = juce::jmin (chunkSize, numSamples - start);
        processChunk (buffer, start, length, channelsToProcess);
    }

    activeChannels.store (numOutputChannels, std::memory_order_relaxed);
}

void MyFirstTubeAmpAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer,
                                                  int start,
                                                  int length,
                                                  int channelsToProcess)
{
    // The dry copy is always needed: it is what the bypass fades to, and it is
    // delayed by the oversampler latency so a bypassed signal leaves the plugin
    // at the same time as a processed one.
    for (int channel = 0; channel < channelsToProcess; ++channel)
        dryBuffer.copyFrom (channel, 0, buffer, channel, start, length);

    // Rebuilding coefficients 64 samples at a time keeps a tone-knob move smooth
    // without doing it per sample.
    constexpr int maximumRetuneInterval = 64;

    for (int offset = 0; offset < length; offset += maximumRetuneInterval)
    {
        const int step = juce::jmin (maximumRetuneInterval, length - offset);
        const int segmentStart = start + offset;

        const bool toneIsSmoothing = smoothedBassDb.isSmoothing() || smoothedMidDb.isSmoothing()
                                  || smoothedTrebleDb.isSmoothing() || smoothedPresenceDb.isSmoothing();

        if (toneIsSmoothing)
        {
            smoothedBassDb.skip (step);
            smoothedMidDb.skip (step);
            smoothedTrebleDb.skip (step);
            smoothedPresenceDb.skip (step);

            updateCoefficients();
        }

        // Pass 1: into the first valve's grid. The sag ducks the whole amp, and
        // it follows the mono sum because both channels share one supply.
        for (int i = 0; i < step; ++i)
        {
            const auto inputGain = smoothedInputGain.getNextValue();
            const auto sagAmount = smoothedSag.getNextValue();

            float mono = buffer.getSample (0, segmentStart + i);

            if (channelsToProcess > 1)
                mono = 0.5f * (mono + buffer.getSample (1, segmentStart + i));

            const auto envelope = sagEnvelope.processSample (mono * inputGain);
            const auto sagGain = juce::jmax (minimumSagGain, 1.0f - sagAmount * envelope);

            for (int channel = 0; channel < channelsToProcess; ++channel)
                buffer.setSample (channel, segmentStart + i,
                                  buffer.getSample (channel, segmentStart + i) * inputGain * sagGain);
        }

        // Pass 2: the two valves, oversampled.
        if (oversampling != nullptr)
        {
            auto block = juce::dsp::AudioBlock<float> (buffer)
                             .getSubsetChannelBlock (0, (size_t) channelsToProcess)
                             .getSubBlock ((size_t) segmentStart, (size_t) step);

            auto oversampled = oversampling->processSamplesUp (block);

            for (size_t channel = 0; channel < oversampled.getNumChannels(); ++channel)
            {
                auto* samples = oversampled.getChannelPointer (channel);
                auto& coupling = interstage[channel];

                for (size_t i = 0; i < oversampled.getNumSamples(); ++i)
                {
                    const auto fromPreamp = waives::amp::preampCurve (samples[i], preampBias);
                    const auto coupled = coupling.processSample (fromPreamp);

                    samples[i] = waives::amp::powerCurve (coupled, powerKnee);
                }
            }

            oversampling->processSamplesDown (block);
        }

        // Pass 3: tone stack, cabinet, DC block and the output trim.
        for (int i = 0; i < step; ++i)
        {
            const auto cabinetAmount = smoothedCabinet.getNextValue();
            const auto outputGain = smoothedOutputGain.getNextValue();

            for (int channel = 0; channel < channelsToProcess; ++channel)
            {
                const auto index = (size_t) channel;
                auto value = buffer.getSample (channel, segmentStart + i);

                value = bassFilter[index].processSample (value);
                value = midFilter[index].processSample (value);
                value = trebleFilter[index].processSample (value);
                value = presenceFilter[index].processSample (value);

                // The cabinet always runs so its filters hold live state; the
                // switch only moves the mix between them and the dry path.
                auto cabinet = cabinetHighPass[index].processSample (value);
                cabinet = cabinetLowPass[index].processSample (cabinet);
                cabinet = cabinetDip[index].processSample (cabinet);

                value += (cabinet - value) * cabinetAmount;
                value = dcBlocker[index].processSample (value);

                buffer.setSample (channel, segmentStart + i, value * outputGain);
            }
        }
    }

    for (int i = 0; i < length; ++i)
    {
        const auto wetAmount = smoothedWet.getNextValue();

        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* samples = buffer.getWritePointer (channel) + start;

            dryDelayLine.write (channel, dryBuffer.getSample (channel, i));
            const auto dry = dryDelayLine.read (channel, (float) dryDelaySamples);

            samples[i] = dry + (samples[i] - dry) * wetAmount;

            waives::dsp::updatePeak (meterLevels[(size_t) channel], std::abs (samples[i]));
        }

        dryDelayLine.advance();
    }
}

float MyFirstTubeAmpAudioProcessor::getMeterLevel (int channel) const noexcept
{
    if (! juce::isPositiveAndBelow (channel, (int) meterLevels.size()))
        return 0.0f;

    return meterLevels[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* MyFirstTubeAmpAudioProcessor::createEditor()
{
    return new MyFirstTubeAmpAudioProcessorEditor (*this);
}

bool MyFirstTubeAmpAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String MyFirstTubeAmpAudioProcessor::getName() const
{
    return "MyFirstTubeAmp";
}

bool MyFirstTubeAmpAudioProcessor::acceptsMidi() const       { return false; }
bool MyFirstTubeAmpAudioProcessor::producesMidi() const      { return false; }
bool MyFirstTubeAmpAudioProcessor::isMidiEffect() const      { return false; }

double MyFirstTubeAmpAudioProcessor::getTailLengthSeconds() const
{
    return 0.05;
}

//==============================================================================
int MyFirstTubeAmpAudioProcessor::getNumPrograms()      { return numFactoryPresets; }
int MyFirstTubeAmpAudioProcessor::getCurrentProgram()   { return currentProgram.load (std::memory_order_relaxed); }

void MyFirstTubeAmpAudioProcessor::setCurrentProgram (int index)
{
    applyFactoryPreset (index);
}

void MyFirstTubeAmpAudioProcessor::applyFactoryPreset (int index)
{
    if (index < 0 || index >= numFactoryPresets)
        return;

    const auto& preset = factoryPresets[index];

    const auto apply = [this] (const char* parameterID, float realValue)
    {
        auto* parameter = apvts.getParameter (parameterID);

        jassert (parameter != nullptr);

        if (parameter != nullptr)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    };

    apply (ParamIDs::gain,     preset.gainDb);
    apply (ParamIDs::bass,     preset.bassDb);
    apply (ParamIDs::mid,      preset.midDb);
    apply (ParamIDs::treble,   preset.trebleDb);
    apply (ParamIDs::presence, preset.presenceDb);
    apply (ParamIDs::sag,      preset.sagPercent);
    apply (ParamIDs::output,   preset.outputDb);
    apply (ParamIDs::cabinet,  preset.cabinet ? 1.0f : 0.0f);

    currentProgram.store (index, std::memory_order_relaxed);
}

const juce::String MyFirstTubeAmpAudioProcessor::getProgramName (int index)
{
    if (index < 0 || index >= numFactoryPresets)
        return {};

    return factoryPresets[index].name;
}

void MyFirstTubeAmpAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void MyFirstTubeAmpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("programIndex", getCurrentProgram(), nullptr);

    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void MyFirstTubeAmpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new MyFirstTubeAmpAudioProcessor();
}
