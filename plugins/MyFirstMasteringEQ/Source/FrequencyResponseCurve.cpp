#include "FrequencyResponseCurve.h"
#include "BandPalette.h"
#include "PluginProcessor.h"

#include <cmath>

namespace
{
    const juce::Colour background    { 0xff14151f };
    const juce::Colour gridLine      { 0xff2c2f3f };
    const juce::Colour gridText      { 0xff6f7488 };
    const juce::Colour curveColour   { 0xffd7d9e6 };

    constexpr double minimumHz = 20.0;
    constexpr double maximumHz = 20000.0;
    constexpr float  maximumDb = 20.0f;   // half-height of the graph
    constexpr float  nodeRadius = 6.0f;
    constexpr float  hitRadius = 14.0f;

    // Dark text on the light band colours, light text on nothing else exists
    // yet, so one rule covers the palette.
    juce::Colour textContrastFor (juce::Colour colour)
    {
        return colour.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff14151f)
                                                       : juce::Colours::white;
    }

    juce::Path makeCurvePath (juce::Rectangle<float> plot,
                              const std::function<float (double)>& dbAt,
                              int numPoints = 240)
    {
        juce::Path path;

        for (int i = 0; i <= numPoints; ++i)
        {
            const auto proportion = (double) i / (double) numPoints;
            const auto hz = minimumHz * std::pow (maximumHz / minimumHz, proportion);
            const auto db = juce::jlimit (-maximumDb, maximumDb, dbAt (hz));

            const auto x = plot.getX() + (float) proportion * plot.getWidth();
            const auto y = FrequencyResponseCurve::Geometry::yFor (db, plot);

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        return path;
    }
}

//==============================================================================
juce::Rectangle<float> FrequencyResponseCurve::Geometry::plotFor (juce::Rectangle<float> bounds) noexcept
{
    // Room for the dB labels on the left and the frequency labels underneath,
    // which sit below the plot and would otherwise be clipped by the frame.
    return bounds.reduced (34.0f, 10.0f).withTrimmedBottom (6.0f);
}

float FrequencyResponseCurve::Geometry::xFor (double hz, juce::Rectangle<float> plot) noexcept
{
    const auto normalised = std::log10 (juce::jlimit (minimumHz, maximumHz, hz) / minimumHz)
                          / std::log10 (maximumHz / minimumHz);

    return plot.getX() + (float) normalised * plot.getWidth();
}

double FrequencyResponseCurve::Geometry::hzFor (float x, juce::Rectangle<float> plot) noexcept
{
    if (plot.getWidth() <= 0.0f)
        return minimumHz;

    const auto normalised = juce::jlimit (0.0, 1.0, (double) ((x - plot.getX()) / plot.getWidth()));

    return minimumHz * std::pow (maximumHz / minimumHz, normalised);
}

float FrequencyResponseCurve::Geometry::yFor (float db, juce::Rectangle<float> plot) noexcept
{
    const auto normalised = (db + maximumDb) / (2.0f * maximumDb);

    return plot.getBottom() - normalised * plot.getHeight();
}

float FrequencyResponseCurve::Geometry::dbFor (float y, juce::Rectangle<float> plot) noexcept
{
    if (plot.getHeight() <= 0.0f)
        return 0.0f;

    const auto normalised = 1.0f - (y - plot.getY()) / plot.getHeight();

    return -maximumDb + normalised * 2.0f * maximumDb;
}

//==============================================================================
FrequencyResponseCurve::FrequencyResponseCurve (juce::AudioProcessorValueTreeState& state,
                                                std::function<double()> sampleRateProvider)
    : apvts (state),
      getSampleRate (std::move (sampleRateProvider))
{
    // The nodes are the only interactive part; clicks that miss them are ignored
    // rather than swallowing the drag that resizes the editor.
    setInterceptsMouseClicks (true, false);

    // Polling keeps the audio thread out of the editor: a parameter listener
    // would run there, and posting a message from it can allocate.
    refreshNow();
    startTimerHz (20);
}

FrequencyResponseCurve::~FrequencyResponseCurve()
{
    stopTimer();

    // The editor can be closed mid-drag; an unclosed gesture leaves the host
    // thinking the parameter is still being touched.
    endNodeDrag();
}

void FrequencyResponseCurve::setSelectedBand (int band) noexcept
{
    if (! juce::isPositiveAndBelow (band, ParamIDs::numBands))
        return;

    selectedBand = band;
    repaint();
}

double FrequencyResponseCurve::effectiveSampleRate() const noexcept
{
    return lastSnapshot.sampleRate > 0.0 ? lastSnapshot.sampleRate : 48000.0;
}

FrequencyResponseCurve::Snapshot FrequencyResponseCurve::readSnapshot() const
{
    Snapshot snapshot;

    for (int band = 0; band < ParamIDs::numBands; ++band)
    {
        const auto index = (size_t) band;

        snapshot.bands[index].frequency = apvts.getRawParameterValue (ParamIDs::frequency[index])->load();
        snapshot.bands[index].gainDb    = apvts.getRawParameterValue (ParamIDs::gain[index])->load();
        snapshot.bands[index].q         = apvts.getRawParameterValue (ParamIDs::q[index])->load();
    }

    snapshot.sampleRate = getSampleRate();

    return snapshot;
}

bool FrequencyResponseCurve::isDifferent (const Snapshot& a, const Snapshot& b) noexcept
{
    constexpr float tolerance = 1.0e-4f;

    if (std::abs (a.sampleRate - b.sampleRate) > 1.0)
        return true;

    for (size_t band = 0; band < a.bands.size(); ++band)
    {
        if (std::abs (a.bands[band].frequency - b.bands[band].frequency) > tolerance
            || std::abs (a.bands[band].gainDb - b.bands[band].gainDb) > tolerance
            || std::abs (a.bands[band].q - b.bands[band].q) > tolerance)
            return true;
    }

    return false;
}

void FrequencyResponseCurve::refreshNow()
{
    const auto snapshot = readSnapshot();

    if (isDifferent (snapshot, lastSnapshot))
    {
        lastSnapshot = snapshot;
        repaint();
    }
}

void FrequencyResponseCurve::timerCallback()
{
    refreshNow();
}

float FrequencyResponseCurve::combinedResponseDbAt (double hz) const
{
    return waives::peak::combinedResponseDb (effectiveSampleRate(), lastSnapshot.bands, hz);
}

//==============================================================================
juce::Point<float> FrequencyResponseCurve::nodePosition (int band, juce::Rectangle<float> plot) const
{
    const auto& settings = lastSnapshot.bands[(size_t) band];
    const auto frequency = (double) waives::peak::clampToNyquist (settings, effectiveSampleRate()).frequency;

    return { Geometry::xFor (frequency, plot), Geometry::yFor (settings.gainDb, plot) };
}

int FrequencyResponseCurve::nodeAt (juce::Point<float> position) const
{
    const auto plot = Geometry::plotFor (getLocalBounds().toFloat());

    // Bands can sit on top of each other - all four start at 1 kHz, 0 dB - so the
    // selected band wins the hit test. Otherwise a drag would grab whichever band
    // is drawn first instead of the one the knobs are pointing at.
    if (juce::isPositiveAndBelow (selectedBand, ParamIDs::numBands)
        && position.getDistanceFrom (nodePosition (selectedBand, plot)) <= hitRadius)
        return selectedBand;

    int best = -1;
    auto bestDistance = hitRadius;

    for (int band = 0; band < ParamIDs::numBands; ++band)
    {
        const auto distance = position.getDistanceFrom (nodePosition (band, plot));

        if (distance <= bestDistance)
        {
            best = band;
            bestDistance = distance;
        }
    }

    return best;
}

void FrequencyResponseCurve::setBand (int band, double hz, float gainDb)
{
    if (! juce::isPositiveAndBelow (band, ParamIDs::numBands))
        return;

    const auto index = (size_t) band;

    // Going through the APVTS keeps the knobs, the host's automation and the
    // session state in step with the graph.
    if (auto* parameter = apvts.getParameter (ParamIDs::frequency[index]))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) hz));

    if (auto* parameter = apvts.getParameter (ParamIDs::gain[index]))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (gainDb));
}

void FrequencyResponseCurve::scaleBandQ (int band, double factor)
{
    if (! juce::isPositiveAndBelow (band, ParamIDs::numBands))
        return;

    auto* parameter = apvts.getParameter (ParamIDs::q[(size_t) band]);

    if (parameter == nullptr)
        return;

    const auto current = parameter->convertFrom0to1 (parameter->getValue());
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) (current * factor)));
    parameter->endChangeGesture();
}

void FrequencyResponseCurve::beginNodeDrag (int band)
{
    if (! juce::isPositiveAndBelow (band, ParamIDs::numBands))
        return;

    endNodeDrag();

    drag.band = band;
    drag.frequency = apvts.getRawParameterValue (ParamIDs::frequency[(size_t) band])->load();
    drag.gainDb = apvts.getRawParameterValue (ParamIDs::gain[(size_t) band])->load();

    for (const auto* parameterID : { ParamIDs::frequency[(size_t) band], ParamIDs::gain[(size_t) band] })
        if (auto* parameter = apvts.getParameter (parameterID))
            parameter->beginChangeGesture();
}

void FrequencyResponseCurve::dragNodeBy (int band, juce::Point<float> delta)
{
    // A mouse-down that missed every node leaves the drag closed, so band and
    // drag.band can both be -1; that must not reach the parameter arrays.
    if (! juce::isPositiveAndBelow (band, ParamIDs::numBands) || band != drag.band)
        return;

    auto* frequencyParameter = apvts.getParameter (ParamIDs::frequency[(size_t) band]);
    auto* gainParameter = apvts.getParameter (ParamIDs::gain[(size_t) band]);

    if (frequencyParameter == nullptr || gainParameter == nullptr)
        return;

    const auto plot = Geometry::plotFor (getLocalBounds().toFloat());

    // The pointer moves the node, so the deltas are taken from where the drag
    // started rather than from the absolute position. That matters for frequency:
    // the node is drawn at the Nyquist-clamped value, which is below the parameter
    // at low sample rates, so a gain-only drag must not write the drawn value back.
    const auto ratio = plot.getWidth() > 0.0f
                           ? std::pow (maximumHz / minimumHz, (double) delta.x / (double) plot.getWidth())
                           : 1.0;
    const auto hz = drag.frequency * ratio;

    const auto dbPerPixel = 2.0f * maximumDb / juce::jmax (1.0f, plot.getHeight());
    const auto gainDb = drag.gainDb - delta.y * dbPerPixel;

    const auto& frequencyRange = frequencyParameter->getNormalisableRange();
    const auto& gainRange = gainParameter->getNormalisableRange();

    setBand (band,
             juce::jlimit ((double) frequencyRange.start, (double) frequencyRange.end, hz),
             juce::jlimit (gainRange.start, gainRange.end, gainDb));

    refreshNow();
}

void FrequencyResponseCurve::endNodeDrag()
{
    if (drag.band < 0)
        return;

    for (const auto* parameterID : { ParamIDs::frequency[(size_t) drag.band],
                                     ParamIDs::gain[(size_t) drag.band] })
        if (auto* parameter = apvts.getParameter (parameterID))
            parameter->endChangeGesture();

    drag.band = -1;
}

//==============================================================================
void FrequencyResponseCurve::mouseDown (const juce::MouseEvent& event)
{
    const auto band = nodeAt (event.position);

    if (band < 0)
        return;

    beginNodeDrag (band);

    if (onBandSelected != nullptr)
        onBandSelected (band);
}

void FrequencyResponseCurve::mouseDrag (const juce::MouseEvent& event)
{
    dragNodeBy (drag.band, event.position - event.mouseDownPosition);
}

void FrequencyResponseCurve::mouseUp (const juce::MouseEvent&)
{
    endNodeDrag();
}

void FrequencyResponseCurve::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (nodeAt (event.position) >= 0 ? juce::MouseCursor::DraggingHandCursor
                                                 : juce::MouseCursor::NormalCursor);
}

void FrequencyResponseCurve::mouseWheelMove (const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const auto band = nodeAt (event.position);

    if (band < 0)
        return;

    // About one doubling of Q per wheel turn; positive values make the bell
    // narrower, which matches turning the Q knob clockwise.
    scaleBandQ (band, std::pow (2.0, (double) wheel.deltaY * 3.0));
    refreshNow();
}

//==============================================================================
void FrequencyResponseCurve::paintGrid (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    const auto plot = Geometry::plotFor (bounds);

    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    for (int db = -18; db <= 18; db += 6)
    {
        const auto y = Geometry::yFor ((float) db, plot);

        g.setColour (db == 0 ? gridLine.brighter (0.55f) : gridLine);
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());

        g.setColour (gridText);
        g.drawText (juce::String (db) + " dB",
                    juce::Rectangle<float> (bounds.getX() + 2.0f, y - 6.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }

    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const auto x = Geometry::xFor (hz, plot);

        g.setColour (gridLine);
        g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());

        g.setColour (gridText);
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                    juce::Rectangle<float> (x - 12.0f, plot.getBottom() + 1.0f, 24.0f, 10.0f),
                    juce::Justification::centred, false);
    }
}

void FrequencyResponseCurve::paintBand (juce::Graphics& g, juce::Rectangle<float> plot, int band) const
{
    const auto& settings = lastSnapshot.bands[(size_t) band];
    const auto rate = effectiveSampleRate();
    const auto colour = waives::ui::bandColour (band);
    const bool isSelected = band == selectedBand;

    const auto path = makeCurvePath (plot, [&] (double hz)
    {
        return waives::peak::responseDb (rate, settings, hz);
    });

    g.setColour (colour.withAlpha (isSelected ? 0.9f : 0.35f));
    g.strokePath (path, juce::PathStrokeType (isSelected ? 1.6f : 1.0f));
}

void FrequencyResponseCurve::paintCombined (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const auto path = makeCurvePath (plot, [this] (double hz) { return combinedResponseDbAt (hz); });

    juce::Path fill (path);
    fill.lineTo (plot.getRight(), Geometry::yFor (0.0f, plot));
    fill.lineTo (plot.getX(),     Geometry::yFor (0.0f, plot));
    fill.closeSubPath();

    g.setColour (curveColour.withAlpha (0.10f));
    g.fillPath (fill);

    g.setColour (curveColour);
    g.strokePath (path, juce::PathStrokeType (2.2f));
}

void FrequencyResponseCurve::paintNodes (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    // The selected node last, so it sits on top of any neighbours.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int band = 0; band < ParamIDs::numBands; ++band)
        {
            const bool isSelected = band == selectedBand;

            if (isSelected != (pass == 1))
                continue;

            const auto centre = nodePosition (band, plot);
            const auto colour = waives::ui::bandColour (band);
            const auto radius = isSelected ? nodeRadius + 1.5f : nodeRadius;

            g.setColour (background.withAlpha (0.85f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

            g.setColour (colour.withAlpha (isSelected ? 1.0f : 0.6f));
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre),
                           isSelected ? 2.0f : 1.2f);

            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (radius, radius).withCentre (centre));

            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.setColour (textContrastFor (colour));
            g.drawText (juce::String (band + 1),
                        juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre),
                        juce::Justification::centred, false);
        }
    }
}

void FrequencyResponseCurve::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto plot = Geometry::plotFor (bounds);

    paintGrid (g, bounds);

    for (int band = 0; band < ParamIDs::numBands; ++band)
        paintBand (g, plot, band);

    paintCombined (g, plot);
    paintNodes (g, plot);

    g.setColour (gridLine.brighter (0.2f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}
