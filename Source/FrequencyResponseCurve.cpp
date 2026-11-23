#include "FrequencyResponseCurve.h"
#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <complex>

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

    // |H(e^jw)| for coefficients { b0, b1, b2, a0, a1, a2 }.
    float responseDb (const std::array<float, 6>& raw, double sampleRate, double probeHz)
    {
        const auto a0 = raw[3];

        if (std::abs (a0) < 1.0e-12f)
            return 0.0f;

        const auto b0 = raw[0] / a0;
        const auto b1 = raw[1] / a0;
        const auto b2 = raw[2] / a0;
        const auto a1 = raw[4] / a0;
        const auto a2 = raw[5] / a0;

        const auto omega = 2.0 * juce::MathConstants<double>::pi * probeHz / sampleRate;
        const std::complex<double> z1 (std::cos (-omega), std::sin (-omega));
        const auto z2 = z1 * z1;

        const auto numerator   = (double) b0 + (double) b1 * z1 + (double) b2 * z2;
        const auto denominator = 1.0 + (double) a1 * z1 + (double) a2 * z2;

        return (float) juce::Decibels::gainToDecibels (std::abs (numerator / denominator), -100.0);
    }
}

FrequencyResponseCurve::FrequencyResponseCurve (juce::AudioProcessorValueTreeState& state,
                                                std::function<double()> sampleRateProvider)
    : apvts (state),
      getSampleRate (std::move (sampleRateProvider))
{
    setInterceptsMouseClicks (false, false);

    for (auto* id : { ParamIDs::frequency, ParamIDs::gain, ParamIDs::q })
        apvts.addParameterListener (id, this);
}

FrequencyResponseCurve::~FrequencyResponseCurve()
{
    apvts.removeParameterListener (ParamIDs::frequency, this);
    apvts.removeParameterListener (ParamIDs::gain, this);
    apvts.removeParameterListener (ParamIDs::q, this);
    cancelPendingUpdate();
}

void FrequencyResponseCurve::parameterChanged (const juce::String&, float)
{
    // Automation can call this from the audio thread, so never repaint directly.
    triggerAsyncUpdate();
}

void FrequencyResponseCurve::handleAsyncUpdate()
{
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

    // Build the curve from the live parameter values.
    const auto sampleRate = getSampleRate();
    const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    const auto frequency = (double) apvts.getRawParameterValue (ParamIDs::frequency)->load();
    const auto gainDb    = apvts.getRawParameterValue (ParamIDs::gain)->load();
    const auto q         = apvts.getRawParameterValue (ParamIDs::q)->load();

    const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (
        rate,
        (float) juce::jlimit (20.0, rate * 0.45, frequency),
        juce::jmax (0.05f, q),
        juce::Decibels::decibelsToGain (gainDb));

    juce::Path path;
    constexpr int numPoints = 240;

    for (int i = 0; i <= numPoints; ++i)
    {
        const auto proportion = (double) i / (double) numPoints;
        const auto hz = minimumHz * std::pow (maximumHz / minimumHz, proportion);
        const auto db = responseDb (raw, rate, hz);

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
    const auto markerX = plot.getX() + frequencyToX (juce::jlimit (minimumHz, maximumHz, frequency),
                                                    plot.getWidth());
    const auto markerY = plot.getY() + dbToY (juce::jlimit (-maximumDb, maximumDb, gainDb),
                                              plot.getHeight());

    g.setColour (curveColour.brighter (0.6f));
    g.fillEllipse (markerX - 3.5f, markerY - 3.5f, 7.0f, 7.0f);

    g.setColour (gridLine.brighter (0.2f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}
