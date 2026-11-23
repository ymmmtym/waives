#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Draws the magnitude response of the peaking EQ.

    The curve is derived from exactly the same coefficients the audio thread
    uses (juce::dsp::IIR::ArrayCoefficients), so the graph can never drift away
    from what is actually heard.
*/
class FrequencyResponseCurve final : public juce::Component,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::AsyncUpdater
{
public:
    FrequencyResponseCurve (juce::AudioProcessorValueTreeState& state,
                            std::function<double()> sampleRateProvider);
    ~FrequencyResponseCurve() override;

    void paint (juce::Graphics& g) override;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState& apvts;
    std::function<double()> getSampleRate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrequencyResponseCurve)
};
