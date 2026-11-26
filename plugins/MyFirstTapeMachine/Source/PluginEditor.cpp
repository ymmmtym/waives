#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    const juce::Colour backgroundTop    { 0xff221f1a };
    const juce::Colour backgroundBottom { 0xff12100d };
    const juce::Colour accent           { 0xffe0a13a };
    const juce::Colour accentDim        { 0xff7a5a24 };
    const juce::Colour textPrimary      { 0xfff0eadd };
    const juce::Colour textSecondary    { 0xff9a9384 };

    void styleKnobLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, textSecondary);
        label.setJustificationType (juce::Justification::centred);
    }

    void styleRotary (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f,
                                    true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, textSecondary.withAlpha (0.3f));
        slider.setColour (juce::Slider::thumbColourId, textPrimary);
        slider.setColour (juce::Slider::textBoxTextColourId, textPrimary);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    struct KnobSpec
    {
        const char* parameterID;
        const char* label;
        const char* title;
        const char* description;
    };

    constexpr KnobSpec knobSpecs[] = {
        { ParamIDs::input,   "INPUT",   "Input",   "Level going onto the tape. Hotter input saturates sooner." },
        { ParamIDs::drive,   "DRIVE",   "Drive",   "How hard the tape is driven. At 0 the curve is a clean pass-through." },
        { ParamIDs::tone,    "TONE",    "Tone",    "Playback high-frequency loss. Lower values sound like a worn, slow tape." },
        { ParamIDs::wow,     "WOW",     "Wow",     "Slow pitch drift from the capstan, up to 3 ms of delay movement." },
        { ParamIDs::flutter, "FLUTTER", "Flutter", "Fast pitch wobble from the reel, up to 0.35 ms." },
        { ParamIDs::hiss,    "HISS",    "Hiss",    "Tape noise. -96 dB is off." },
        { ParamIDs::mix,     "MIX",     "Mix",     "Blend with the delayed dry signal. The tape sound alone is 100 %." },
        { ParamIDs::output,  "OUTPUT",  "Output",  "Output trim, after the tape stage." },
    };

    static_assert (sizeof (knobSpecs) / sizeof (knobSpecs[0]) == 8,
                   "the knob list and the attachments must stay the same length");

    // paint() and resized() both need these, so the reels and the meter cannot
    // drift away from the layout.
    juce::Rectangle<int> reelArea (juce::Rectangle<int> fullBounds)
    {
        auto area = fullBounds.reduced (16);
        return area.removeFromTop (34).removeFromRight (124);
    }

    juce::Rectangle<int> meterArea (juce::Rectangle<int> fullBounds)
    {
        auto area = fullBounds.reduced (16);
        return area.removeFromBottom (16);
    }
}

MyFirstTapeMachineAudioProcessorEditor::MyFirstTapeMachineAudioProcessorEditor (
        MyFirstTapeMachineAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      apvts (p.getValueTreeState())
{
    setResizable (true, true);

    titleLabel.setText ("MyFirstTapeMachine", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, textPrimary);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    statusLabel.setColour (juce::Label::textColourId, textSecondary);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    infoLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                      12.0f, juce::Font::plain)));
    infoLabel.setColour (juce::Label::textColourId, accentDim.brighter (0.5f));
    infoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (infoLabel);

    for (size_t index = 0; index < knobs.size(); ++index)
    {
        auto& knob = knobs[index];
        const auto& spec = knobSpecs[index];

        styleKnobLabel (knob.label, spec.label);
        styleRotary (knob.slider);

        knob.slider.setComponentID (juce::String (spec.parameterID) + "Slider");
        knob.slider.setTitle (spec.title);
        knob.slider.setDescription (spec.description);
        knob.slider.setTooltip (spec.description);
        knob.slider.setWantsKeyboardFocus (true);

        // Double click returns the knob to the parameter's own default, so the
        // shortcut and the session state can never disagree.
        if (auto* parameter = apvts.getParameter (spec.parameterID))
            knob.slider.setDoubleClickReturnValue (
                true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

        addAndMakeVisible (knob.label);
        addAndMakeVisible (knob.slider);

        attachments[index] = std::make_unique<SliderAttachment> (apvts, spec.parameterID,
                                                                 knob.slider);
    }

    presetButton.setTooltip ("Factory presets. Picking the current one again reloads it.");
    presetButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    presetButton.setColour (juce::TextButton::textColourOffId, textPrimary);
    presetButton.onClick = [this]
    {
        juce::PopupMenu menu;

        for (int program = 0; program < processorRef.getNumPrograms(); ++program)
            menu.addItem (program + 1, processorRef.getProgramName (program), true,
                          program == processorRef.getCurrentProgram());

        // A menu rather than a combo box: a combo box does not report a click on
        // the item that is already selected, so re-picking a preset after
        // turning a knob could never restore it.
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                            [this] (int result)
                            {
                                if (result > 0)
                                    processorRef.applyFactoryPreset (result - 1);
                            });
    };
    addAndMakeVisible (presetButton);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setColour (juce::ToggleButton::textColourId, textSecondary);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);
    bypassButton.setColour (juce::ToggleButton::tickDisabledColourId, textSecondary);
    bypassButton.setTooltip ("Pass the audio through untouched, for an A/B comparison");
    bypassButton.setTitle ("Bypass");
    addAndMakeVisible (bypassButton);

    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bypass, bypassButton);

    setSize (560, 470);

    // 560x470 is the size the two knob rows are designed for; below it the
    // meters and the knobs would start to overlap.
    setResizeLimits (560, 470, 1000, 760);

    updateInfoLabel();
    startTimerHz (15);
}

MyFirstTapeMachineAudioProcessorEditor::~MyFirstTapeMachineAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void MyFirstTapeMachineAudioProcessorEditor::timerCallback()
{
    updateInfoLabel();

    presetButton.setButtonText (processorRef.getProgramName (processorRef.getCurrentProgram()));

    // Meter: jump to a new peak, fall back smoothly, so a transient stays
    // readable without the bar flickering.
    bool needsRepaint = false;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto peak = processorRef.getMeterLevel (channel);
        const auto decayed = juce::jmax (peak, meterDisplay[(size_t) channel] * 0.86f);

        if (std::abs (decayed - meterDisplay[(size_t) channel]) > 1.0e-4f)
            needsRepaint = true;

        meterDisplay[(size_t) channel] = decayed;
    }

    if (needsRepaint)
        repaint();
}

void MyFirstTapeMachineAudioProcessorEditor::updateInfoLabel()
{
    const auto sampleRate = processorRef.getCurrentSampleRate();

    if (sampleRate <= 0.0)
    {
        infoLabel.setText ("waiting for audio device", juce::dontSendNotification);
        statusLabel.setText ("TAPE - 15 ips", juce::dontSendNotification);
        return;
    }

    const juce::String text = juce::String (sampleRate / 1000.0, 1) + " kHz   "
                            + juce::String (processorRef.getActiveChannelCount()) + " ch   "
                            + juce::String (processorRef.getCurrentBlockSize()) + " samples   "
                            + juce::String (processorRef.getReportedLatency()) + " smp latency";

    infoLabel.setText (text, juce::dontSendNotification);

    const auto wow = apvts.getRawParameterValue (ParamIDs::wow)->load();
    const auto flutter = apvts.getRawParameterValue (ParamIDs::flutter)->load();

    statusLabel.setText ("TAPE - wow " + juce::String (wow, 0) + " %, flutter "
                             + juce::String (flutter, 0) + " %",
                         juce::dontSendNotification);
}

//==============================================================================
void MyFirstTapeMachineAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient gradient (backgroundTop, 0.0f, 0.0f,
                                   backgroundBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Accent bar between the title and the status line.
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (16.0f, 50.0f, 48.0f, 2.5f, 1.25f);

    drawReels (g, reelArea (getLocalBounds()));
    drawMeter (g, meterArea (getLocalBounds()));

    g.setColour (textSecondary.withAlpha (0.3f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void MyFirstTapeMachineAudioProcessorEditor::drawReels (juce::Graphics& g,
                                                        juce::Rectangle<int> area) const
{
    if (area.isEmpty())
        return;

    const auto diameter = (float) juce::jmin (area.getHeight(), 46);
    const auto radius = diameter * 0.5f;

    for (int reel = 0; reel < 2; ++reel)
    {
        const auto centreX = (float) area.getRight() - radius - (float) reel * (diameter + 12.0f);
        const auto centreY = (float) area.getCentreY();

        const juce::Rectangle<float> outer (centreX - radius, centreY - radius, diameter, diameter);

        g.setColour (accentDim.withAlpha (0.55f));
        g.drawEllipse (outer, 1.4f);

        g.setColour (accent.withAlpha (0.75f));
        g.drawEllipse (outer.reduced (radius * 0.45f), 1.2f);

        // Three spokes, so the reel reads as a reel and not as a dot.
        for (int spoke = 0; spoke < 3; ++spoke)
        {
            const auto angle = juce::MathConstants<float>::twoPi * (float) spoke / 3.0f
                             + juce::MathConstants<float>::pi * 0.25f;

            g.drawLine (centreX + std::cos (angle) * radius * 0.45f,
                        centreY + std::sin (angle) * radius * 0.45f,
                        centreX + std::cos (angle) * radius * 0.9f,
                        centreY + std::sin (angle) * radius * 0.9f,
                        1.2f);
        }
    }

    // The tape running between the two reels.
    g.setColour (accentDim.withAlpha (0.4f));
    g.drawLine ((float) area.getRight() - diameter * 2.0f - 12.0f, (float) area.getCentreY() - radius * 0.55f,
                (float) area.getRight() - diameter - 12.0f, (float) area.getCentreY() - radius * 0.55f,
                1.6f);
}

void MyFirstTapeMachineAudioProcessorEditor::drawMeter (juce::Graphics& g,
                                                        juce::Rectangle<int> area) const
{
    if (area.isEmpty())
        return;

    const auto barHeight = 6;
    const auto gap = 4;

    for (int channel = 0; channel < 2; ++channel)
    {
        auto bar = area.withHeight (barHeight).translated (0, channel * (barHeight + gap));

        g.setColour (textSecondary.withAlpha (0.15f));
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);

        // -60 dB to 0 dB across the width.
        const auto levelDb = juce::Decibels::gainToDecibels (meterDisplay[(size_t) channel], -60.0f);
        const auto amount = juce::jlimit (0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);

        if (amount <= 0.0f)
            continue;

        const auto filled = bar.withWidth (juce::roundToInt ((float) bar.getWidth() * amount));

        g.setColour (levelDb > -3.0f ? juce::Colour (0xffe2603a) : accent);
        g.fillRoundedRectangle (filled.toFloat(), 3.0f);
    }
}

//==============================================================================
void MyFirstTapeMachineAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    area.removeFromBottom (16);   // the meter is painted, not laid out

    auto titleRow = area.removeFromTop (34);
    titleRow.removeFromRight (124);   // the reels are painted, not laid out
    titleLabel.setBounds (titleRow);

    area.removeFromTop (8);

    auto statusRow = area.removeFromTop (26);
    bypassButton.setBounds (statusRow.removeFromRight (92));
    statusRow.removeFromRight (8);
    presetButton.setBounds (statusRow.removeFromLeft (juce::jmin (170, statusRow.getWidth())));
    statusRow.removeFromLeft (10);
    statusLabel.setBounds (statusRow);

    area.removeFromTop (10);

    infoLabel.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (10);

    // Two rows of four knobs, so the panel stays legible at the minimum size.
    // The gap keeps a row's readout away from the next row's label.
    constexpr int rowGap = 10;
    const auto rowHeight = (area.getHeight() - rowGap) / 2;
    const auto columns = 4;

    for (int row = 0; row < 2; ++row)
    {
        if (row > 0)
            area.removeFromTop (rowGap);

        auto rowArea = area.removeFromTop (rowHeight);

        for (int column = 0; column < columns; ++column)
        {
            auto cell = rowArea.removeFromLeft (rowArea.getWidth() / (columns - column));
            auto& knob = knobs[(size_t) (row * columns + column)];

            knob.label.setBounds (cell.removeFromTop (16));
            knob.slider.setBounds (cell.reduced (4, 0));
        }
    }
}
