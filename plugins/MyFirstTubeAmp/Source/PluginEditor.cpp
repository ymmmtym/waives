#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    const juce::Colour backgroundTop    { 0xff241a17 };
    const juce::Colour backgroundBottom { 0xff120d0b };
    const juce::Colour accent           { 0xffe2603a };
    const juce::Colour accentDim        { 0xff7d3a25 };
    const juce::Colour textPrimary      { 0xfff2e6df };
    const juce::Colour textSecondary    { 0xffa08f86 };

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
        { ParamIDs::gain,     "GAIN",     "Gain",     "How hard the first valve is driven. This is the amp's input drive." },
        { ParamIDs::bass,     "BASS",     "Bass",     "Low shelf at 100 Hz." },
        { ParamIDs::mid,      "MID",      "Mid",      "Bell at 700 Hz." },
        { ParamIDs::treble,   "TREBLE",   "Treble",   "High shelf at 3.2 kHz." },
        { ParamIDs::presence, "PRESENCE", "Presence", "Bell at 2.6 kHz. Boost only, like the control on a real head." },
        { ParamIDs::sag,      "SAG",      "Sag",      "Supply sag: the amp ducks on a transient and recovers slowly." },
        { ParamIDs::output,   "OUTPUT",   "Output",   "Output trim, after the cabinet." },
    };

    static_assert (sizeof (knobSpecs) / sizeof (knobSpecs[0]) == 7,
                   "the knob list and the attachments must stay the same length");

    // paint() and resized() both need these, so the valves and the meter cannot
    // drift away from the layout.
    juce::Rectangle<int> valveArea (juce::Rectangle<int> fullBounds)
    {
        auto area = fullBounds.reduced (16);
        return area.removeFromTop (34).removeFromRight (110);
    }

    juce::Rectangle<int> meterArea (juce::Rectangle<int> fullBounds)
    {
        auto area = fullBounds.reduced (16);
        return area.removeFromBottom (16);
    }
}

MyFirstTubeAmpAudioProcessorEditor::MyFirstTubeAmpAudioProcessorEditor (MyFirstTubeAmpAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      apvts (p.getValueTreeState())
{
    setResizable (true, true);

    titleLabel.setText ("MyFirstTubeAmp", juce::dontSendNotification);
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
    infoLabel.setColour (juce::Label::textColourId, accentDim.brighter (0.55f));
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

    cabinetButton.setComponentID ("cabinetButton");
    cabinetButton.setButtonText ("CAB");
    cabinetButton.setClickingTogglesState (true);
    cabinetButton.setTooltip ("Speaker cabinet: a closed 4x12. Switch it off to use your own IR.");
    cabinetButton.setTitle ("Cabinet");
    cabinetButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    cabinetButton.setColour (juce::TextButton::buttonOnColourId, accent);
    cabinetButton.setColour (juce::TextButton::textColourOffId, textSecondary);
    cabinetButton.setColour (juce::TextButton::textColourOnId, backgroundTop);
    addAndMakeVisible (cabinetButton);

    cabinetAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::cabinet, cabinetButton);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setColour (juce::ToggleButton::textColourId, textSecondary);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);
    bypassButton.setColour (juce::ToggleButton::tickDisabledColourId, textSecondary);
    bypassButton.setTooltip ("Pass the audio through untouched, for an A/B comparison");
    bypassButton.setTitle ("Bypass");
    addAndMakeVisible (bypassButton);

    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bypass, bypassButton);

    setSize (560, 470);

    // 560x470 is the size the two knob rows are designed for.
    setResizeLimits (560, 470, 1000, 760);

    updateInfoLabel();
    startTimerHz (15);
}

MyFirstTubeAmpAudioProcessorEditor::~MyFirstTubeAmpAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void MyFirstTubeAmpAudioProcessorEditor::timerCallback()
{
    updateInfoLabel();

    presetButton.setButtonText (processorRef.getProgramName (processorRef.getCurrentProgram()));

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

void MyFirstTubeAmpAudioProcessorEditor::updateInfoLabel()
{
    const auto sampleRate = processorRef.getCurrentSampleRate();

    if (sampleRate <= 0.0)
    {
        infoLabel.setText ("waiting for audio device", juce::dontSendNotification);
        statusLabel.setText ("VALVE - 4x oversampled", juce::dontSendNotification);
        return;
    }

    const juce::String text = juce::String (sampleRate / 1000.0, 1) + " kHz   "
                            + juce::String (processorRef.getActiveChannelCount()) + " ch   "
                            + juce::String (processorRef.getCurrentBlockSize()) + " samples   "
                            + juce::String (processorRef.getReportedLatency()) + " smp latency";

    infoLabel.setText (text, juce::dontSendNotification);

    const auto gain = apvts.getRawParameterValue (ParamIDs::gain)->load();
    const auto sag = apvts.getRawParameterValue (ParamIDs::sag)->load();

    statusLabel.setText ("VALVE - drive " + juce::String (gain, 0) + " dB, sag "
                             + juce::String (sag, 0) + " %",
                         juce::dontSendNotification);
}

//==============================================================================
void MyFirstTubeAmpAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient gradient (backgroundTop, 0.0f, 0.0f,
                                   backgroundBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (16.0f, 50.0f, 48.0f, 2.5f, 1.25f);

    drawValves (g, valveArea (getLocalBounds()));
    drawMeter (g, meterArea (getLocalBounds()));

    g.setColour (textSecondary.withAlpha (0.3f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void MyFirstTubeAmpAudioProcessorEditor::drawValves (juce::Graphics& g,
                                                     juce::Rectangle<int> area) const
{
    if (area.isEmpty())
        return;

    const auto height = (float) juce::jmin (area.getHeight(), 40);
    const auto width = height * 0.62f;

    for (int valve = 0; valve < 2; ++valve)
    {
        const auto x = (float) area.getRight() - width - (float) valve * (width + 14.0f);
        const auto centreY = (float) area.getCentreY();

        const juce::Rectangle<float> glass (x, centreY - height * 0.5f, width, height);

        // The glow is the filament: a warm radial gradient behind the glass.
        juce::ColourGradient glow (accent.withAlpha (0.35f), glass.getCentreX(),
                                   glass.getBottom() - glass.getHeight() * 0.25f,
                                   juce::Colours::transparentBlack, glass.getCentreX(),
                                   glass.getBottom() + glass.getHeight(), true);
        g.setGradientFill (glow);
        g.fillEllipse (glass.expanded (glass.getWidth() * 0.5f, glass.getHeight() * 0.3f));

        g.setColour (accentDim.withAlpha (0.75f));
        g.drawRoundedRectangle (glass, glass.getWidth() * 0.5f, 1.3f);

        g.setColour (accent.withAlpha (0.85f));
        g.fillRoundedRectangle (glass.getCentreX() - 1.2f,
                                glass.getY() + glass.getHeight() * 0.35f,
                                2.4f, glass.getHeight() * 0.45f, 1.2f);
    }
}

void MyFirstTubeAmpAudioProcessorEditor::drawMeter (juce::Graphics& g,
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

        const auto levelDb = juce::Decibels::gainToDecibels (meterDisplay[(size_t) channel], -60.0f);
        const auto amount = juce::jlimit (0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);

        if (amount <= 0.0f)
            continue;

        const auto filled = bar.withWidth (juce::roundToInt ((float) bar.getWidth() * amount));

        g.setColour (levelDb > -3.0f ? juce::Colour (0xffe0c04a) : accent);
        g.fillRoundedRectangle (filled.toFloat(), 3.0f);
    }
}

//==============================================================================
void MyFirstTubeAmpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    area.removeFromBottom (16);   // the meter is painted, not laid out

    auto titleRow = area.removeFromTop (34);
    titleRow.removeFromRight (110);   // the valves are painted, not laid out
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

    // Two rows: the four tone controls on top, presence / sag / output and the
    // cabinet switch below.
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

            const auto knobIndex = row * columns + column;

            if (knobIndex >= (int) knobs.size())
            {
                cabinetButton.setBounds (cell.reduced (18, 46));
                continue;
            }

            auto& knob = knobs[(size_t) knobIndex];

            knob.label.setBounds (cell.removeFromTop (16));
            knob.slider.setBounds (cell.reduced (4, 0));
        }
    }

}
