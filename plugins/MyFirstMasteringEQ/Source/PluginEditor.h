#pragma once

#include "FrequencyResponseCurve.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

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

    /** Points the three knobs at one band. The graph calls this when a node is
        clicked, and the band buttons call it when they are pressed. */
    void selectBand (int band);

    int getSelectedBand() const noexcept { return selectedBand; }

private:
    void timerCallback() override;
    void updateInfoLabel();
    void attachKnobsToBand (int band);
    void updateBandButtons();
    void setSolo (bool shouldSolo);

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

    std::array<std::unique_ptr<juce::TextButton>, waives::peak::maxBands> bandButtons;
    juce::TextButton soloButton;
    juce::TextButton bandBypassButton;
    juce::ComboBox presetBox;
    juce::ToggleButton bypassButton;

    FrequencyResponseCurve curve;

    // Declared after the sliders so they are destroyed first.
    std::unique_ptr<SliderAttachment> frequencyAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> qAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> bandBypassAttachment;

    // -1 until the constructor picks band 1, so selectBand() always runs once.
    int selectedBand = -1;

    // Must be destroyed before the components it watches, so it is last.
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyFirstMasteringEQAudioProcessorEditor)
};
