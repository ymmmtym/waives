#pragma once

#include "AmpElements.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

// Parameter IDs end up in saved sessions and automation lanes, so they must
// never change once released.
namespace ParamIDs
{
    inline constexpr const char* gain     = "gain";
    inline constexpr const char* bass     = "bass";
    inline constexpr const char* mid      = "mid";
    inline constexpr const char* treble   = "treble";
    inline constexpr const char* presence = "presence";
    inline constexpr const char* sag      = "sag";
    inline constexpr const char* output   = "output";
    inline constexpr const char* cabinet  = "cabinet";
    inline constexpr const char* bypass   = "bypass";
}

class MyFirstTubeAmpAudioProcessor final : public juce::AudioProcessor
{
public:
    MyFirstTubeAmpAudioProcessor();
    ~MyFirstTubeAmpAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;   // keep the double-precision overload visible
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Written by the audio thread, read by the editor's timer.
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load (std::memory_order_relaxed); }
    int    getCurrentBlockSize()  const noexcept { return currentBlockSize.load (std::memory_order_relaxed); }
    int    getActiveChannelCount() const noexcept { return activeChannels.load (std::memory_order_relaxed); }

    /** The latency this plugin adds, safe to read from the editor while the
        audio thread re-prepares. */
    int    getReportedLatency()   const noexcept { return reportedLatencySamples.load (std::memory_order_relaxed); }

    /** Peak of the last block, per channel, in linear gain. */
    float getMeterLevel (int channel) const noexcept;

    /** Applies one of the factory presets (see getProgramName()). */
    void applyFactoryPreset (int index);

    //==========================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateCoefficients() noexcept;
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int length, int channelsToProcess);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* gainParameter     = nullptr;
    std::atomic<float>* bassParameter     = nullptr;
    std::atomic<float>* midParameter      = nullptr;
    std::atomic<float>* trebleParameter   = nullptr;
    std::atomic<float>* presenceParameter = nullptr;
    std::atomic<float>* sagParameter      = nullptr;
    std::atomic<float>* outputParameter   = nullptr;
    std::atomic<float>* cabinetParameter  = nullptr;
    std::atomic<float>* bypassParameter   = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedInputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedCabinet;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWet;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSag;

    // The tone controls are smoothed in dB and the coefficients are rebuilt from
    // those ramps, so turning a knob sweeps instead of stepping.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBassDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMidDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTrebleDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPresenceDb;

    // The tone stack and the cabinet are plain biquads either side of the
    // non-linear part of the chain.
    std::array<waives::dsp::Biquad, 2> bassFilter;
    std::array<waives::dsp::Biquad, 2> midFilter;
    std::array<waives::dsp::Biquad, 2> trebleFilter;
    std::array<waives::dsp::Biquad, 2> presenceFilter;
    std::array<waives::dsp::Biquad, 2> cabinetHighPass;
    std::array<waives::dsp::Biquad, 2> cabinetLowPass;
    std::array<waives::dsp::Biquad, 2> cabinetDip;

    std::array<waives::amp::OnePoleHighPass, 2> interstage;
    waives::amp::EnvelopeFollower sagEnvelope;

    // The two stages run oversampled: 4x keeps the harmonics they generate from
    // folding back below Nyquist, which is what a hard-clipped guitar signal
    // would otherwise do very audibly.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int oversamplingLatency = 0;

    std::array<waives::dsp::Biquad, 2> dcBlocker;

    // The host's signal before the plugin, kept for the bypass crossfade.
    juce::AudioBuffer<float> dryBuffer;

    std::atomic<int>    currentProgram { 0 };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    reportedLatencySamples { 0 };
    std::atomic<bool>   hostBypassRequested { false };

    // The dry side of the bypass, delayed by the oversampler latency so the
    // plugin keeps its place in time even when it is bypassed.
    waives::dsp::DelayLine dryDelayLine;
    int dryDelaySamples = 1;
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<int>    activeChannels    { 0 };

    // Mutable because reading a peak consumes it: the editor's timer takes the
    // level and the next block starts from zero again.
    mutable std::array<std::atomic<float>, 2> meterLevels { { { 0.0f }, { 0.0f } } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstTubeAmpAudioProcessor)
};
