#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

// Parameter IDs are written into saved sessions and automation data, so they
// must never change once a version has been released.
namespace ParamIDs
{
    inline constexpr const char* frequency = "freq";
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* q         = "q";
}

class MyFirstMasteringEQAudioProcessor final : public juce::AudioProcessor
{
public:
    MyFirstMasteringEQAudioProcessor();
    ~MyFirstMasteringEQAudioProcessor() override;

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

    //==========================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

private:
    // Biquad in transposed direct form II. Plain floats only, so retuning and
    // processing it never allocates or touches a reference count.
    struct Biquad
    {
        void setCoefficients (const std::array<float, 5>& normalised) noexcept
        {
            b0 = normalised[0];
            b1 = normalised[1];
            b2 = normalised[2];
            a1 = normalised[3];
            a2 = normalised[4];
        }

        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }

        float processSample (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateFilterCoefficients() noexcept;

    juce::AudioProcessorValueTreeState apvts;

    // Read-only views of the APVTS atomics: safe to load from the audio thread.
    std::atomic<float>* frequencyParameter = nullptr;
    std::atomic<float>* gainParameter = nullptr;
    std::atomic<float>* qParameter = nullptr;

    std::array<Biquad, 2> filters;

    // Smoothing keeps a knob move from producing a click: the filter is retuned
    // once per block with a slowly moving value instead of jumping.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedFrequency;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedGainDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedQ;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<int>    activeChannels    { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstMasteringEQAudioProcessor)
};
