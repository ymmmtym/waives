#include "FrequencyResponseCurve.h"
#include "PeakResponse.h"
#include "PluginProcessor.h"

#include <cmath>

namespace
{
    const juce::Colour background    { 0xff14151f };
    const juce::Colour gridLine      { 0xff2c2f3f };
    const juce::Colour gridText      { 0xff6f7488 };
    const juce::Colour curveColour   { 0xff9a86ff };
    const juce::Colour curveFill     { 0x338b6cff };

    constexpr double minimumHz = 20.0;
    constexpr double maximumHz = 20000.0;
    constexpr float  maximumDb = 20.0f;   // half-height of the graph

    float frequencyToX (double hz, float width)
    {
        const auto normalised = std::log10 (hz / minimumHz) / std::log10 (maximumHz / minimumHz);
        return (float) normalised * width;
    }

    float dbToY (float db, float height)
    {
        const auto normalised = (db + maximumDb) / (2.0f * maximumDb);
        return height - normalised * height;
    }

}

FrequencyResponseCurve::FrequencyResponseCurve (juce::AudioProcessorValueTreeState& state,
                                                std::function<double()> sampleRateProvider)
    : apvts (state),
      getSampleRate (std::move (sampleRateProvider))
{
    setInterceptsMouseClicks (false, false);

    // Polling keeps the audio thread completely out of this: a parameter
    // listener would run there and posting a message from it can allocate.
    lastSnapshot = readSnapshot();
    startTimerHz (20);
}

FrequencyResponseCurve::~FrequencyResponseCurve()
{
    stopTimer();
}

FrequencyResponseCurve::Snapshot FrequencyResponseCurve::readSnapshot() const
{
    Snapshot snapshot;

    snapshot.frequency  = apvts.getRawParameterValue (ParamIDs::frequency)->load();
    snapshot.gainDb     = apvts.getRawParameterValue (ParamIDs::gain)->load();
    snapshot.q          = apvts.getRawParameterValue (ParamIDs::q)->load();
    snapshot.sampleRate = getSampleRate();

    return snapshot;
}

bool FrequencyResponseCurve::isDifferent (const Snapshot& a, const Snapshot& b) noexcept
{
    constexpr float tolerance = 1.0e-4f;

    return std::abs (a.frequency - b.frequency) > tolerance
        || std::abs (a.gainDb - b.gainDb) > tolerance
        || std::abs (a.q - b.q) > tolerance
        || std::abs (a.sampleRate - b.sampleRate) > 1.0;
}

void FrequencyResponseCurve::timerCallback()
{
    const auto snapshot = readSnapshot();

    if (! isDifferent (snapshot, lastSnapshot))
        return;

    lastSnapshot = snapshot;
    repaint();
}

void FrequencyResponseCurve::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto plot = bounds.reduced (34.0f, 10.0f);

    // Horizontal grid: dB steps.
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.setColour (gridText);

    for (int db = -18; db <= 18; db += 6)
    {
        const auto y = plot.getY() + dbToY ((float) db, plot.getHeight());

        g.setColour (db == 0 ? gridLine.brighter (0.5f) : gridLine);
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());

        g.setColour (gridText);
        g.drawText (juce::String (db) + " dB",
                    juce::Rectangle<float> (bounds.getX() + 2.0f, y - 6.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }

    // Vertical grid: frequency decades.
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const auto x = plot.getX() + frequencyToX (hz, plot.getWidth());

        g.setColour (gridLine);
        g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());

        g.setColour (gridText);
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                    juce::Rectangle<float> (x - 12.0f, plot.getBottom() + 1.0f, 24.0f, 10.0f),
                    juce::Justification::centred, false);
    }

    // Draw the snapshot the repaint decision was made from, not the live
    // values: paint() runs later and could otherwise show a transient state
    // that no further repaint is scheduled for.
    const auto& snapshot = lastSnapshot;
    const auto rate = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : 48000.0;

    const waives::peak::BandSettings band { snapshot.frequency, snapshot.gainDb, snapshot.q };

    // The centre is clamped below Nyquist, so the marker has to follow the
    // clamped value rather than the raw parameter.
    const auto effectiveFrequency = (double) waives::peak::clampToNyquist (band, rate).frequency;

    juce::Path path;
    constexpr int numPoints = 240;

    for (int i = 0; i <= numPoints; ++i)
    {
        const auto proportion = (double) i / (double) numPoints;
        const auto hz = minimumHz * std::pow (maximumHz / minimumHz, proportion);
        const auto db = waives::peak::responseDb (rate, band, hz);

        const auto x = plot.getX() + (float) proportion * plot.getWidth();
        const auto y = plot.getY() + dbToY (juce::jlimit (-maximumDb, maximumDb, db), plot.getHeight());

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    // Soft fill between the curve and the 0 dB line.
    juce::Path fill (path);
    fill.lineTo (plot.getRight(), plot.getY() + dbToY (0.0f, plot.getHeight()));
    fill.lineTo (plot.getX(),     plot.getY() + dbToY (0.0f, plot.getHeight()));
    fill.closeSubPath();

    g.setColour (curveFill);
    g.fillPath (fill);

    g.setColour (curveColour);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Marker at the centre frequency.
    const auto markerX = plot.getX() + frequencyToX (juce::jlimit (minimumHz, maximumHz, effectiveFrequency),
                                                    plot.getWidth());
    const auto markerY = plot.getY() + dbToY (juce::jlimit (-maximumDb, maximumDb, band.gainDb),
                                              plot.getHeight());

    g.setColour (curveColour.brighter (0.6f));
    g.fillEllipse (markerX - 3.5f, markerY - 3.5f, 7.0f, 7.0f);

    g.setColour (gridLine.brighter (0.2f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}
