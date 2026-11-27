#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

class MyFirstTubeAmpAudioProcessor;

class MyFirstTubeAmpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit MyFirstTubeAmpAudioProcessorEditor (MyFirstTubeAmpAudioProcessor&);
    ~MyFirstTubeAmpAudioProcessorEditor() override;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateInfoLabel();
    void drawValves (juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawMeter (juce::Graphics& g, juce::Rectangle<int> area) const;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Label  label;
        juce::Slider slider;
    };

    MyFirstTubeAmpAudioProcessor& processorRef;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label infoLabel;

    // Gain, bass, mid, treble / presence, sag, output.
    std::array<Knob, 7> knobs;

    juce::TextButton presetButton { "Preset" };
    juce::TextButton cabinetButton;
    juce::ToggleButton bypassButton;

    // Declared after the sliders so they are destroyed first.
    std::array<std::unique_ptr<SliderAttachment>, 7> attachments;
    std::unique_ptr<ButtonAttachment> cabinetAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    // Meter ballistics, updated on the timer rather than from paint().
    std::array<float, 2> meterDisplay { { 0.0f, 0.0f } };

    // Must be destroyed before the components it watches, so it is last.
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstTubeAmpAudioProcessorEditor)
};
