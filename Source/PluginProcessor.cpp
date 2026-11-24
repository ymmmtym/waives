#include "PluginProcessor.h"
#include "PluginEditor.h"

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
        const std::array<float, 5> identity { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        for (auto& filter : filters)
            filter.setCoefficients (identity);

        return;
    }

    const auto frequency = juce::jlimit (20.0f, (float) (sampleRate * 0.45),
                                         smoothedFrequency.getCurrentValue());
    const auto q = juce::jmax (0.05f, smoothedQ.getCurrentValue());
    const auto gain = juce::Decibels::decibelsToGain (smoothedGainDb.getCurrentValue());

    // ArrayCoefficients returns { b0, b1, b2, a0, a1, a2 } by value, so this
    // never allocates and is safe to call from the audio thread.
    const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate,
                                                                              frequency, q, gain);
    const auto a0 = raw[3];

    const std::array<float, 5> normalised { raw[0] / a0, raw[1] / a0, raw[2] / a0,
                                            raw[4] / a0, raw[5] / a0 };

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

    // 3) Retune the filter only while a parameter is moving, using block-rate
    //    smoothing. A steady knob costs one isSmoothing() check per block.
    smoothedFrequency.setTargetValue (frequencyParameter->load());
    smoothedGainDb.setTargetValue (gainParameter->load());
    smoothedQ.setTargetValue (qParameter->load());

    if (smoothedFrequency.isSmoothing() || smoothedGainDb.isSmoothing() || smoothedQ.isSmoothing())
    {
        smoothedFrequency.skip (numSamples);
        smoothedGainDb.skip (numSamples);
        smoothedQ.skip (numSamples);

        updateFilterCoefficients();
    }

    // 4) One independent biquad per channel; the state must not be shared.
    const int channelsToProcess = juce::jmin (numOutputChannels, (int) filters.size());

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        auto& filter = filters[(size_t) channel];

        for (int i = 0; i < numSamples; ++i)
            samples[i] = filter.processSample (samples[i]);
    }

    activeChannels.store (numOutputChannels, std::memory_order_relaxed);
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
