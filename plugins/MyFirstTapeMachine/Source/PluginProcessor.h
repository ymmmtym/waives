#pragma once

#include "TapeElements.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

// Parameter IDs end up in saved sessions and automation lanes, so they must
// never change once released.
namespace ParamIDs
{
    inline constexpr const char* input   = "input";
    inline constexpr const char* drive   = "drive";
    inline constexpr const char* tone    = "tone";
    inline constexpr const char* wow     = "wow";
    inline constexpr const char* flutter = "flutter";
    inline constexpr const char* hiss    = "hiss";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* output  = "output";
    inline constexpr const char* bypass  = "bypass";
}

class MyFirstTapeMachineAudioProcessor final : public juce::AudioProcessor
{
public:
    MyFirstTapeMachineAudioProcessor();
    ~MyFirstTapeMachineAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;   // keep the double-precision overload visible
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

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

    /** Peak of the last block, per channel, in linear gain. The editor reads
        this to draw the meter and applies its own decay. */
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

    std::atomic<float>* inputParameter   = nullptr;
    std::atomic<float>* driveParameter   = nullptr;
    std::atomic<float>* toneParameter    = nullptr;
    std::atomic<float>* wowParameter     = nullptr;
    std::atomic<float>* flutterParameter = nullptr;
    std::atomic<float>* hissParameter    = nullptr;
    std::atomic<float>* mixParameter     = nullptr;
    std::atomic<float>* outputParameter  = nullptr;
    std::atomic<float>* bypassParameter  = nullptr;

    // Gains are smoothed as linear values and frequencies as log values, so a
    // knob move is a ramp rather than a step.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedInputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedOutputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedWet;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedMix;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedToneHz;

    // Depth of the pitch modulation, in samples of delay.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWowDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFlutterDepth;

    // LFO phases in cycles, advanced per sample.
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    double flutterPhaseFast = 0.0;

    waives::tape::DelayLine delayLine;

    // Pre-emphasis on the record side, head bump and de-emphasis on the
    // playback side, then the tone control and a DC blocker.
    std::array<waives::tape::Biquad, 2> preEmphasis;
    std::array<waives::tape::Biquad, 2> headBump;
    std::array<waives::tape::Biquad, 2> deEmphasis;
    std::array<waives::tape::Biquad, 2> toneFilter;
    std::array<waives::tape::Biquad, 2> dcBlocker;

    std::array<waives::tape::Noise, 2> noiseSources;

    // Saturation runs oversampled so the harmonics it generates do not fold
    // back below Nyquist. Built in prepareToPlay(); never touched by the audio
    // thread.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int oversamplingLatency = 0;

    // Average delay of the modulated read, in samples.
    float baseDelaySamples = 0.0f;

    // The unmodulated tap of the delay line: the dry side of the mix, aligned
    // with the oversampled wet path.
    juce::AudioBuffer<float> dryTapBuffer;

    // The host's signal before the plugin, kept for the bypass crossfade.
    juce::AudioBuffer<float> dryBuffer;

    std::atomic<int>    currentProgram { 0 };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<int>    activeChannels    { 0 };

    // Mutable because reading a peak consumes it: the editor's timer takes the
    // level and the next block starts from zero again.
    mutable std::array<std::atomic<float>, 2> meterLevels { { { 0.0f }, { 0.0f } } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstTapeMachineAudioProcessor)
};
