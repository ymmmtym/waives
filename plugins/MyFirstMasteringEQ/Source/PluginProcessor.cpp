#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PeakResponse.h"

#include <cmath>

namespace
{
    // A range whose 0..1 midpoint lands on `centre`, i.e. a log-style control.
    juce::NormalisableRange<float> makeLogRange (float minValue, float maxValue, float centre)
    {
        juce::NormalisableRange<float> range (minValue, maxValue);
        range.setSkewForCentre (centre);
        return range;
    }

    // Hosts (and the CLAP validator) require value -> text -> value -> text to be
    // stable. JUCE's default formatting prints 7 decimals for continuous ranges,
    // which does not survive that round trip, so each parameter formats itself.
    juce::String formatFrequency (float hz, int)
    {
        // Rounding first keeps 999.99 Hz (a float round-trip of 1 kHz) on the kHz side.
        if (std::round (hz) >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";

        return juce::String (hz, 1) + " Hz";
    }

    float parseFrequency (const juce::String& text)
    {
        const auto trimmed = text.trim();
        const auto numeric = trimmed.getFloatValue();

        return trimmed.containsIgnoreCase ("k") ? numeric * 1000.0f : numeric;
    }

    juce::String formatDecibels (float db, int) { return juce::String (db, 2) + " dB"; }
    juce::String formatQ (float q, int)         { return juce::String (q, 2); }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
MyFirstMasteringEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The version hint (1) must stay stable per parameter ID: VST3/AU hosts use
    // it to match automation data across sessions.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::frequency, 1 },
        "Frequency",
        makeLogRange (20.0f, 20000.0f, 1000.0f),
        1000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction (formatFrequency)
            .withValueFromStringFunction (parseFrequency)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::gain, 1 },
        "Gain",
        juce::NormalisableRange<float> { -18.0f, 18.0f, 0.01f },
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction (formatDecibels)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::q, 1 },
        "Q",
        makeLogRange (0.1f, 10.0f, 1.0f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Q")
            .withStringFromValueFunction (formatQ)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::bypass, 1 },
        "Bypass",
        false));

    return layout;
}

MyFirstMasteringEQAudioProcessor::MyFirstMasteringEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    frequencyParameter = apvts.getRawParameterValue (ParamIDs::frequency);
    gainParameter      = apvts.getRawParameterValue (ParamIDs::gain);
    qParameter         = apvts.getRawParameterValue (ParamIDs::q);
    bypassParameter    = apvts.getRawParameterValue (ParamIDs::bypass);
}

MyFirstMasteringEQAudioProcessor::~MyFirstMasteringEQAudioProcessor() = default;

//==============================================================================
void MyFirstMasteringEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBlockSize.store (samplesPerBlock, std::memory_order_relaxed);
    activeChannels.store (juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels()),
                          std::memory_order_relaxed);

    // A peaking EQ is a zero-latency filter; the host needs to know that so it
    // does not compensate the track by mistake.
    setLatencySamples (0);

    constexpr double smoothingSeconds = 0.02;

    smoothedFrequency.reset (sampleRate, smoothingSeconds);
    smoothedGainDb.reset (sampleRate, smoothingSeconds);
    smoothedQ.reset (sampleRate, smoothingSeconds);

    // Start on the current parameter values so the first block is not a ramp
    // from a stale value.
    smoothedFrequency.setCurrentAndTargetValue (frequencyParameter->load());
    smoothedGainDb.setCurrentAndTargetValue (gainParameter->load());
    smoothedQ.setCurrentAndTargetValue (qParameter->load());

    for (auto& filter : filters)
        filter.reset();

    // 10 ms is long enough to be inaudible and short enough to feel instant.
    constexpr double bypassFadeSeconds = 0.01;

    smoothedWet.reset (sampleRate, bypassFadeSeconds);
    smoothedWet.setCurrentAndTargetValue (bypassParameter->load() > 0.5f ? 0.0f : 1.0f);

    dryBuffer.setSize (juce::jmax (1, getTotalNumOutputChannels()),
                       juce::jmax (1, samplesPerBlock),
                       false, true, false);

    updateFilterCoefficients();
}

void MyFirstMasteringEQAudioProcessor::updateFilterCoefficients() noexcept
{
    const auto sampleRate = getCurrentSampleRate();

    if (sampleRate <= 0.0)
        return;

    // Never let the centre reach Nyquist: the biquad math degenerates there.
    // At unity gain the biquad is only mathematically transparent: on hardware
    // with fused multiply-add (Apple silicon) the residual is not bit-exact, so
    // snap to an exact pass-through instead. The audible difference at this
    // threshold is below 0.001 dB.
    constexpr float transparentThresholdDb = 1.0e-3f;

    if (std::abs (smoothedGainDb.getCurrentValue()) < transparentThresholdDb)
    {
        const auto identity = waives::peak::identity();

        for (auto& filter : filters)
        {
            // A leftover z1/z2 from the previous coefficients would leak the old
            // tail into the first samples, so clear the state on the transition.
            filter.reset();
            filter.setCoefficients (identity);
        }

        return;
    }

    const waives::peak::BandSettings band { smoothedFrequency.getCurrentValue(),
                                            smoothedGainDb.getCurrentValue(),
                                            smoothedQ.getCurrentValue() };

    // Everything returns by value, so this never allocates and is safe to call
    // from the audio thread.
    const auto normalised = waives::peak::normalise (
        waives::peak::makePeakCoefficients (sampleRate, band));

    for (auto& filter : filters)
        filter.setCoefficients (normalised);
}

void MyFirstMasteringEQAudioProcessor::releaseResources()
{
}

bool MyFirstMasteringEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    return mainOutput == layouts.getMainInputChannelSet();
}

void MyFirstMasteringEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    // 1) Denormals: once a filter with feedback is running, decaying tails can
    //    fall into denormal range and cost huge CPU. This sets the FTZ/DAZ flags.
    juce::ScopedNoDenormals noDenormals;

    // 2) The buffer belongs to the host and already contains the input audio.
    //    Output channels without a matching input must be silent.
    const int numSamples = buffer.getNumSamples();
    const int numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    for (int channel = getTotalNumInputChannels(); channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    const int channelsToProcess = juce::jmin (numOutputChannels, (int) filters.size());

    // 3) Retuning happens inside processWet(), which only recomputes while a
    //    parameter is moving.
    smoothedFrequency.setTargetValue (frequencyParameter->load());
    smoothedGainDb.setTargetValue (gainParameter->load());
    smoothedQ.setTargetValue (qParameter->load());
    smoothedWet.setTargetValue (bypassParameter->load() > 0.5f ? 0.0f : 1.0f);

    // 4) The host may pass a block longer than the one announced in
    //    prepareToPlay(), so the dry copy is filled in chunks of its own size
    //    rather than reallocating on the audio thread.
    const int chunkSize = juce::jmax (1, dryBuffer.getNumSamples());

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int length = juce::jmin (chunkSize, numSamples - start);
        processChunk (buffer, start, length, channelsToProcess);
    }

    activeChannels.store (numOutputChannels, std::memory_order_relaxed);
}

void MyFirstMasteringEQAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer,
                                                     int start,
                                                     int length,
                                                     int channelsToProcess)
{
    // Dry audio is only needed while the wet signal is being faded in or out: a
    // fully processed block works in place, and a fully bypassed block is left
    // untouched. The filter still runs while bypassed so that its state tracks
    // the input and the fade-in starts from a settled filter.
    const bool needsDry = smoothedWet.isSmoothing() || smoothedWet.getCurrentValue() < 1.0f;

    if (needsDry)
    {
        for (int channel = 0; channel < channelsToProcess; ++channel)
            dryBuffer.copyFrom (channel, 0, buffer, channel, start, length);
    }

    processWet (buffer, start, length, channelsToProcess);

    if (! needsDry)
        return;

    // One ramp step per sample, applied to every channel, so the fade lands on
    // the same point in all of them.
    for (int i = 0; i < length; ++i)
    {
        const auto wetAmount = smoothedWet.getNextValue();

        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* samples = buffer.getWritePointer (channel) + start;
            const auto dry = dryBuffer.getSample (channel, i);

            samples[i] = dry + (samples[i] - dry) * wetAmount;
        }
    }
}

void MyFirstMasteringEQAudioProcessor::processWet (juce::AudioBuffer<float>& buffer,
                                                   int start,
                                                   int length,
                                                   int channelsToProcess)
{
    // One independent biquad per channel; the state must not be shared.
    const auto processRange = [this, &buffer, channelsToProcess] (int rangeStart, int rangeLength)
    {
        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            auto* samples = buffer.getWritePointer (channel) + rangeStart;
            auto& filter = filters[(size_t) channel];

            for (int i = 0; i < rangeLength; ++i)
                samples[i] = filter.processSample (samples[i]);
        }
    };

    const bool isSmoothing = smoothedFrequency.isSmoothing()
                          || smoothedGainDb.isSmoothing()
                          || smoothedQ.isSmoothing();

    if (! isSmoothing)
    {
        processRange (start, length);
        return;
    }

    // Advance the ramp in bounded steps instead of once per host block: a block
    // longer than the ramp would otherwise jump straight to the target and the
    // smoothing would never be heard.
    constexpr int maximumRetuneInterval = 32;

    for (int offset = 0; offset < length; offset += maximumRetuneInterval)
    {
        const int step = juce::jmin (maximumRetuneInterval, length - offset);

        smoothedFrequency.skip (step);
        smoothedGainDb.skip (step);
        smoothedQ.skip (step);
        updateFilterCoefficients();

        processRange (start + offset, step);
    }
}

//==============================================================================
juce::AudioProcessorEditor* MyFirstMasteringEQAudioProcessor::createEditor()
{
    return new MyFirstMasteringEQAudioProcessorEditor (*this);
}

bool MyFirstMasteringEQAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String MyFirstMasteringEQAudioProcessor::getName() const
{
    return "MyFirstMasteringEQ";
}

bool MyFirstMasteringEQAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MyFirstMasteringEQAudioProcessor::producesMidi() const
{
    return false;
}

bool MyFirstMasteringEQAudioProcessor::isMidiEffect() const
{
    return false;
}

double MyFirstMasteringEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
int MyFirstMasteringEQAudioProcessor::getNumPrograms()
{
    return 1;
}

int MyFirstMasteringEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MyFirstMasteringEQAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String MyFirstMasteringEQAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void MyFirstMasteringEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void MyFirstMasteringEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void MyFirstMasteringEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    // Unknown or corrupt state: keep the current parameter values.
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// Entry point every JUCE plugin wrapper (VST3, AU, CLAP, standalone) calls to
// create the processor instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MyFirstMasteringEQAudioProcessor();
}
