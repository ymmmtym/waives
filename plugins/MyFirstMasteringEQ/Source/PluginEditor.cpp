#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    const juce::Colour backgroundTop    { 0xff1b1d29 };
    const juce::Colour backgroundBottom { 0xff11121a };
    const juce::Colour accent           { 0xff7c5cff };
    const juce::Colour textPrimary      { 0xffe9e9f2 };
    const juce::Colour textSecondary    { 0xff8b8fa3 };

    void styleKnobLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, textSecondary);
        label.setJustificationType (juce::Justification::centred);
    }

    void styleRotary (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 20);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f,
                                    true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, textSecondary.withAlpha (0.35f));
        slider.setColour (juce::Slider::thumbColourId, textPrimary);
        slider.setColour (juce::Slider::textBoxTextColourId, textPrimary);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
}

MyFirstMasteringEQAudioProcessorEditor::MyFirstMasteringEQAudioProcessorEditor (
        MyFirstMasteringEQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      apvts (p.getValueTreeState()),
      curve (p.getValueTreeState(), [&p] { return p.getCurrentSampleRate(); })
{
    setResizable (true, true);
    setResizeLimits (400, 420, 900, 760);

    titleLabel.setText ("MyFirstMasteringEQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, textPrimary);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("Single-band peaking EQ", juce::dontSendNotification);
    statusLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    statusLabel.setColour (juce::Label::textColourId, textSecondary);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    infoLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                      13.0f, juce::Font::plain)));
    infoLabel.setColour (juce::Label::textColourId, accent.brighter (0.4f));
    infoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (infoLabel);

    styleKnobLabel (frequencyLabel, "FREQ");
    styleKnobLabel (gainLabel, "GAIN");
    styleKnobLabel (qLabel, "Q");

    for (auto* label : { &frequencyLabel, &gainLabel, &qLabel })
        addAndMakeVisible (*label);

    // Units come from the parameter's own text conversion (see PluginProcessor),
    // so the sliders must not add a suffix of their own.
    styleRotary (frequencySlider);
    styleRotary (gainSlider);
    styleRotary (qSlider);

    for (auto* slider : { &frequencySlider, &gainSlider, &qSlider })
        addAndMakeVisible (*slider);

    addAndMakeVisible (curve);

    configureKnob (frequencySlider, ParamIDs::frequency, "Frequency",
                   "Boost or cut around this frequency. Logarithmic from 20 Hz to 20 kHz.");
    configureKnob (gainSlider, ParamIDs::gain, "Gain",
                   "How much to boost or cut, in dB. Mastering moves usually stay within +/- 3 dB.");
    configureKnob (qSlider, ParamIDs::q, "Q",
                   "Sharpness of the bell. Higher Q affects a narrower band.");

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setColour (juce::ToggleButton::textColourId, textSecondary);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);
    bypassButton.setColour (juce::ToggleButton::tickDisabledColourId, textSecondary);
    bypassButton.setTooltip ("Pass the audio through untouched, for an A/B comparison");
    bypassButton.setTitle ("Bypass");
    addAndMakeVisible (bypassButton);

    // The attachments copy the parameter's range (including its skew) into the
    // slider, so Frequency and Q are logarithmic without extra work here.
    frequencyAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::frequency, frequencySlider);
    gainAttachment      = std::make_unique<SliderAttachment> (apvts, ParamIDs::gain,      gainSlider);
    qAttachment         = std::make_unique<SliderAttachment> (apvts, ParamIDs::q,         qSlider);
    bypassAttachment    = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bypass,    bypassButton);

    setSize (480, 500);

    updateInfoLabel();
    startTimerHz (4);
}

MyFirstMasteringEQAudioProcessorEditor::~MyFirstMasteringEQAudioProcessorEditor()
{
    stopTimer();
}

void MyFirstMasteringEQAudioProcessorEditor::configureKnob (juce::Slider& slider,
                                                            const char* parameterID,
                                                            const juce::String& title,
                                                            const juce::String& description)
{
    auto* parameter = apvts.getParameter (parameterID);

    jassert (parameter != nullptr);

    if (parameter == nullptr)
        return;

    // Double click returns the knob to the parameter's own default, so the
    // shortcut and the session state can never disagree.
    slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    slider.setTooltip (description);
    slider.setTitle (title);
    slider.setDescription (description);
    slider.setWantsKeyboardFocus (true);
}

void MyFirstMasteringEQAudioProcessorEditor::timerCallback()
{
    updateInfoLabel();
}

void MyFirstMasteringEQAudioProcessorEditor::updateInfoLabel()
{
    const auto sampleRate = processorRef.getCurrentSampleRate();

    if (sampleRate <= 0.0)
    {
        infoLabel.setText ("waiting for audio device", juce::dontSendNotification);
        return;
    }

    const juce::String text = juce::String (sampleRate / 1000.0, 1) + " kHz   "
                            + juce::String (processorRef.getActiveChannelCount()) + " ch   "
                            + juce::String (processorRef.getCurrentBlockSize()) + " samples";

    infoLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
void MyFirstMasteringEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient gradient (backgroundTop, 0.0f, 0.0f,
                                   backgroundBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Accent bar between the title and the status line.
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (16.0f, 50.0f, 48.0f, 2.5f, 1.25f);

    g.setColour (textSecondary.withAlpha (0.35f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void MyFirstMasteringEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    titleLabel.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);

    auto statusRow = area.removeFromTop (24);
    bypassButton.setBounds (statusRow.removeFromRight (92));
    statusRow.removeFromRight (8);
    statusLabel.setBounds (statusRow);

    area.removeFromTop (8);

    infoLabel.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (8);

    const auto knobHeight = juce::jlimit (110, 160, area.getHeight() * 2 / 5);
    auto knobArea = area.removeFromTop (knobHeight);
    area.removeFromTop (10);

    curve.setBounds (area);

    const auto placeKnob = [] (juce::Rectangle<int> column, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (column.removeFromTop (18));
        slider.setBounds (column.reduced (4, 0));
    };

    const int columnWidth = knobArea.getWidth() / 3;

    placeKnob (knobArea.removeFromLeft (columnWidth), frequencyLabel, frequencySlider);
    placeKnob (knobArea.removeFromLeft (columnWidth), gainLabel,      gainSlider);
    placeKnob (knobArea,                               qLabel,         qSlider);
}
