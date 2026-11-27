#pragma once

#include "PeakResponse.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

// Parameter IDs are written into saved sessions and automation data, so they
// must never change once a version has been released.
namespace ParamIDs
{
    inline constexpr int numBands = waives::peak::maxBands;

    // Band 1 keeps the unsuffixed IDs released in v0.1.0, so a session saved by
    // the one-band build restores into band 1 with no migration; bands 2..8
    // append their number.
    inline constexpr std::array<const char*, numBands> frequency {
        "freq", "freq2", "freq3", "freq4", "freq5", "freq6", "freq7", "freq8"
    };
    inline constexpr std::array<const char*, numBands> gain {
        "gain", "gain2", "gain3", "gain4", "gain5", "gain6", "gain7", "gain8"
    };
    inline constexpr std::array<const char*, numBands> q {
        "q", "q2", "q3", "q4", "q5", "q6", "q7", "q8"
    };

    // Added after the first release, so it goes last: hosts match automation by
    // parameter index within the same version hint.
    inline constexpr const char* bypass       = "bypass";
    inline constexpr std::array<const char*, numBands> bandBypass
    {
        "bypass1", "bypass2", "bypass3", "bypass4",
        "bypass5", "bypass6", "bypass7", "bypass8"
    };
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

    /** Applies one of the factory presets (see getProgramName()). */
    void applyFactoryPreset (int index);

    /** Solos one band: the others fade out, and fade back in when the solo ends.
        -1 clears it. Deliberately not saved in the session - it is a listening
        aid, not part of the sound. */
    void setSoloBand (int band) noexcept;
    int getSoloBand() const noexcept { return soloBand.load (std::memory_order_relaxed); }

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

    // One band of the serial chain: its three parameters, the ramp that keeps
    // knob moves click-free, and the biquad per channel.
    struct BandState
    {
        std::atomic<float>* frequencyParameter = nullptr;
        std::atomic<float>* gainParameter      = nullptr;
        std::atomic<float>* qParameter         = nullptr;
        std::atomic<float>* bypassParameter    = nullptr;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedFrequency;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedGainDb;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedQ;

        // 1 = the band is heard, 0 = it is bypassed or muted by a solo. Fading
        // between the two keeps a band switch out of the output.
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;

        std::array<Biquad, 2> filters;

        bool isSmoothing() const noexcept
        {
            return smoothedFrequency.isSmoothing()
                || smoothedGainDb.isSmoothing()
                || smoothedQ.isSmoothing();
        }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** 1 when the band should be heard, 0 when it is bypassed or muted by a solo. */
    float bandMixTarget (int band) const noexcept;
    void updateFilterCoefficients() noexcept;
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int length, int channelsToProcess);
    void processWet (juce::AudioBuffer<float>& buffer, int start, int length, int channelsToProcess);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* bypassParameter = nullptr;
    std::atomic<int>    soloBand { -1 };

    // The bands run in series: band 0 feeds band 1, and so on.
    std::array<BandState, ParamIDs::numBands> bands;

    // 1 = fully processed, 0 = fully bypassed. Fading between the two instead of
    // switching keeps a bypass click out of the master bus.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWet;

    // Holds the input while the wet signal is faded in or out. Sized once in
    // prepareToPlay(); processBlock never allocates.
    juce::AudioBuffer<float> dryBuffer;

    std::atomic<int>    currentProgram { 0 };

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<int>    activeChannels    { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstMasteringEQAudioProcessor)
};
