#pragma once

#include "FrequencyResponseCurve.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MyFirstMasteringEQAudioProcessor;

class MyFirstMasteringEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                    private juce::Timer
{
public:
    explicit MyFirstMasteringEQAudioProcessorEditor (MyFirstMasteringEQAudioProcessor&);
    ~MyFirstMasteringEQAudioProcessorEditor() override;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateInfoLabel();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void configureKnob (juce::Slider& slider, const char* parameterID,
                        const juce::String& title, const juce::String& description);

    MyFirstMasteringEQAudioProcessor& processorRef;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label infoLabel;

    juce::Label frequencyLabel;
    juce::Label gainLabel;
    juce::Label qLabel;

    juce::Slider frequencySlider;
    juce::Slider gainSlider;
    juce::Slider qSlider;

    juce::ToggleButton bypassButton;

    FrequencyResponseCurve curve;

    // Declared after the sliders so they are destroyed first.
    std::unique_ptr<SliderAttachment> frequencyAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> qAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    // Must be destroyed before the components it watches, so it is last.
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstMasteringEQAudioProcessorEditor)
};
