#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Draws the magnitude response of the peaking EQ.

    The curve is derived from exactly the same coefficients the audio thread
    uses (juce::dsp::IIR::ArrayCoefficients), so the graph can never drift away
    from what is actually heard.
*/
class FrequencyResponseCurve final : public juce::Component,
                                     private juce::Timer
{
public:
    FrequencyResponseCurve (juce::AudioProcessorValueTreeState& state,
                            std::function<double()> sampleRateProvider);
    ~FrequencyResponseCurve() override;

    void paint (juce::Graphics& g) override;

private:
    struct Snapshot
    {
        float frequency = 0.0f;
        float gainDb = 0.0f;
        float q = 0.0f;
        double sampleRate = 0.0;
    };

    void timerCallback() override;
    Snapshot readSnapshot() const;
    static bool isDifferent (const Snapshot& a, const Snapshot& b) noexcept;

    juce::AudioProcessorValueTreeState& apvts;
    std::function<double()> getSampleRate;
    Snapshot lastSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrequencyResponseCurve)
};
