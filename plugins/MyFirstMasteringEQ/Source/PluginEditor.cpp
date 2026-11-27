#include "PluginEditor.h"
#include "BandPalette.h"
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

    titleLabel.setText ("MyFirstMasteringEQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, textPrimary);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
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
    for (auto* slider : { &frequencySlider, &gainSlider, &qSlider })
    {
        styleRotary (*slider);
        addAndMakeVisible (*slider);
    }

    frequencySlider.setComponentID ("frequencySlider");
    gainSlider.setComponentID ("gainSlider");
    qSlider.setComponentID ("qSlider");

    // One button per band, each in its own colour, behaving as a radio group.
    for (int band = 0; band < ParamIDs::numBands; ++band)
    {
        auto button = std::make_unique<juce::TextButton> (juce::String (band + 1));

        button->setComponentID ("band" + juce::String (band + 1));
        button->setClickingTogglesState (true);
        button->setRadioGroupId (0x62616e64);   // "band"
        button->setTooltip ("Edit band " + juce::String (band + 1));
        button->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button->setColour (juce::TextButton::buttonOnColourId, waives::ui::bandColour (band));
        button->setColour (juce::TextButton::textColourOffId, textSecondary);
        button->setColour (juce::TextButton::textColourOnId, backgroundTop);
        button->onClick = [this, band] { selectBand (band); };

        addAndMakeVisible (*button);
        bandButtons[(size_t) band] = std::move (button);
    }

    soloButton.setComponentID ("soloButton");
    soloButton.setButtonText ("SOLO");
    soloButton.setClickingTogglesState (true);
    soloButton.setTooltip ("Listen to the selected band only. Not saved with the session.");
    soloButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    soloButton.setColour (juce::TextButton::buttonOnColourId, accent);
    soloButton.setColour (juce::TextButton::textColourOffId, textSecondary);
    soloButton.setColour (juce::TextButton::textColourOnId, backgroundTop);
    soloButton.onClick = [this] { setSolo (soloButton.getToggleState()); };
    addAndMakeVisible (soloButton);

    bandBypassButton.setComponentID ("bandBypassButton");
    bandBypassButton.setButtonText ("BAND OFF");
    bandBypassButton.setClickingTogglesState (true);
    bandBypassButton.setTooltip ("Pass the selected band through untouched; the other bands keep working");
    bandBypassButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    bandBypassButton.setColour (juce::TextButton::buttonOnColourId, textSecondary.withAlpha (0.35f));
    bandBypassButton.setColour (juce::TextButton::textColourOffId, textSecondary);
    bandBypassButton.setColour (juce::TextButton::textColourOnId, textPrimary);
    addAndMakeVisible (bandBypassButton);

    curve.setComponentID ("frequencyResponseCurve");
    curve.onBandSelected = [this] (int band) { selectBand (band); };
    addAndMakeVisible (curve);

    for (int program = 0; program < processorRef.getNumPrograms(); ++program)
        presetBox.addItem (processorRef.getProgramName (program), program + 1);

    presetBox.setSelectedItemIndex (processorRef.getCurrentProgram(), juce::dontSendNotification);
    presetBox.setTooltip ("Factory presets. Any knob move keeps the sound, the name stays as a hint.");
    presetBox.setTitle ("Preset");
    presetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    presetBox.setColour (juce::ComboBox::textColourId, textPrimary);
    presetBox.setColour (juce::ComboBox::outlineColourId, textSecondary.withAlpha (0.35f));
    presetBox.setColour (juce::ComboBox::arrowColourId, accent);
    presetBox.onChange = [this]
    {
        processorRef.applyFactoryPreset (presetBox.getSelectedItemIndex());
    };
    addAndMakeVisible (presetBox);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setColour (juce::ToggleButton::textColourId, textSecondary);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);
    bypassButton.setColour (juce::ToggleButton::tickDisabledColourId, textSecondary);
    bypassButton.setTooltip ("Pass the audio through untouched, for an A/B comparison");
    bypassButton.setTitle ("Bypass");
    addAndMakeVisible (bypassButton);

    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bypass, bypassButton);

    selectBand (0);
    setSize (480, 500);

    // Last: the call applies the constraint straight away, which runs resized()
    // while the components above do not exist yet.
    // 480x500 is the size the layout is designed for; below it the graph and the
    // knobs would start to overlap, so it is also the minimum.
    setResizeLimits (480, 500, 900, 760);

    updateInfoLabel();
    startTimerHz (4);
}

MyFirstMasteringEQAudioProcessorEditor::~MyFirstMasteringEQAudioProcessorEditor()
{
    stopTimer();

    // The solo button owns the state, so closing the window must not leave the
    // processor soloing a band.
    processorRef.setSoloBand (-1);
}

//==============================================================================
void MyFirstMasteringEQAudioProcessorEditor::selectBand (int band)
{
    if (! juce::isPositiveAndBelow (band, ParamIDs::numBands))
        return;

    if (band != selectedBand)
    {
        selectedBand = band;
        attachKnobsToBand (band);
    }

    curve.setSelectedBand (band);
    updateBandButtons();

    // Solo is a mode: it follows the band being edited rather than staying behind
    // on the one that was selected before.
    if (soloButton.getToggleState())
        setSolo (true);

    statusLabel.setText ("Band " + juce::String (band + 1) + " of " + juce::String (ParamIDs::numBands),
                         juce::dontSendNotification);
}

void MyFirstMasteringEQAudioProcessorEditor::attachKnobsToBand (int band)
{
    const auto index = (size_t) band;
    const auto name = "Band " + juce::String (band + 1);

    // Drop the old listeners first: a surviving attachment would keep writing the
    // previous band's value into the slider after the switch.
    frequencyAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();

    // The attachments copy the parameter's range (including its skew) into the
    // slider, so Frequency and Q are logarithmic without extra work here.
    bandBypassAttachment.reset();
    bandBypassAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bandBypass[index],
                                                               bandBypassButton);

    frequencyAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::frequency[index], frequencySlider);
    gainAttachment      = std::make_unique<SliderAttachment> (apvts, ParamIDs::gain[index],      gainSlider);
    qAttachment         = std::make_unique<SliderAttachment> (apvts, ParamIDs::q[index],         qSlider);

    configureKnob (frequencySlider, ParamIDs::frequency[index], name + " frequency",
                   "Boost or cut around this frequency. Logarithmic from 20 Hz to 20 kHz.");
    configureKnob (gainSlider, ParamIDs::gain[index], name + " gain",
                   "How much to boost or cut, in dB. Mastering moves usually stay within +/- 3 dB.");
    configureKnob (qSlider, ParamIDs::q[index], name + " Q",
                   "Sharpness of the bell. Higher Q affects a narrower band; the mouse wheel over "
                   "the graph node does the same thing.");
}

void MyFirstMasteringEQAudioProcessorEditor::setSolo (bool shouldSolo)
{
    const auto band = shouldSolo ? selectedBand : -1;

    processorRef.setSoloBand (band);
    curve.setSoloBand (band);
    soloButton.setToggleState (shouldSolo, juce::dontSendNotification);
}

void MyFirstMasteringEQAudioProcessorEditor::updateBandButtons()
{
    for (int band = 0; band < ParamIDs::numBands; ++band)
        bandButtons[(size_t) band]->setToggleState (band == selectedBand, juce::dontSendNotification);
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

    // The host can change the program behind the editor's back (automation,
    // session load), so the box follows the processor instead of only the click.
    const auto program = processorRef.getCurrentProgram();

    if (presetBox.getSelectedItemIndex() != program)
        presetBox.setSelectedItemIndex (program, juce::dontSendNotification);
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

    auto statusRow = area.removeFromTop (26);
    bypassButton.setBounds (statusRow.removeFromRight (92));
    statusRow.removeFromRight (8);
    presetBox.setBounds (statusRow.removeFromLeft (juce::jmin (170, statusRow.getWidth())));
    statusRow.removeFromLeft (10);
    statusLabel.setBounds (statusRow);

    area.removeFromTop (6);

    // Eight bands fit beside the two switches at the minimum width, so the row
    // is laid out left to right and the spare space stays at the end.
    auto bandRow = area.removeFromTop (24);

    for (auto& button : bandButtons)
        button->setBounds (bandRow.removeFromLeft (30).reduced (1));

    bandRow.removeFromLeft (10);
    soloButton.setBounds (bandRow.removeFromLeft (56).reduced (1));
    bandRow.removeFromLeft (4);
    bandBypassButton.setBounds (bandRow.removeFromLeft (80).reduced (1));

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
