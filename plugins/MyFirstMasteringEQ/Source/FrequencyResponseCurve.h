#pragma once

#include "PeakResponse.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

/** Draws the response of all four bands and lets the nodes be dragged.

    Every curve is derived from the same coefficients the audio thread uses
    (juce::dsp::IIR::ArrayCoefficients), and the combined curve is the sum of the
    bands' decibels, exactly as the series biquads behave.
*/
class FrequencyResponseCurve final : public juce::Component,
                                     private juce::Timer
{
public:
    FrequencyResponseCurve (juce::AudioProcessorValueTreeState& state,
                            std::function<double()> sampleRateProvider);
    ~FrequencyResponseCurve() override;

    /** Called when a node is clicked, so the editor can move its knobs there. */
    std::function<void (int)> onBandSelected;

    /** The band the editor is editing; it is drawn brighter than the others. */
    void setSelectedBand (int band) noexcept;
    int getSelectedBand() const noexcept { return selectedBand; }

    /** The combined curve in dB at `hz`, computed the way paint() does. */
    float combinedResponseDbAt (double hz) const;

    /** Re-reads the parameters. The timer does this 20 times a second. */
    void refreshNow();

    /** Mapping between graph pixels and parameter values. */
    struct Geometry
    {
        static juce::Rectangle<float> plotFor (juce::Rectangle<float> bounds) noexcept;
        static float xFor (double hz, juce::Rectangle<float> plot) noexcept;
        static double hzFor (float x, juce::Rectangle<float> plot) noexcept;
        static float yFor (float db, juce::Rectangle<float> plot) noexcept;
        static float dbFor (float y, juce::Rectangle<float> plot) noexcept;
    };

    /** Node dragging, in steps so the maths can be checked without synthesising
        mouse events: beginNodeDrag takes the host's change gestures and remembers
        the starting values, dragNodeBy applies a pixel delta, endNodeDrag closes
        the gestures. */
    void beginNodeDrag (int band);
    void dragNodeBy (int band, juce::Point<float> delta);
    void endNodeDrag();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override;

private:
    struct Snapshot
    {
        std::array<waives::peak::BandSettings, waives::peak::maxBands> bands {};
        double sampleRate = 0.0;
    };

    void paintGrid (juce::Graphics& g, juce::Rectangle<float> bounds) const;
    void paintBand (juce::Graphics& g, juce::Rectangle<float> plot, int band) const;
    void paintCombined (juce::Graphics& g, juce::Rectangle<float> plot) const;
    void paintNodes (juce::Graphics& g, juce::Rectangle<float> plot) const;

    void timerCallback() override;
    Snapshot readSnapshot() const;
    static bool isDifferent (const Snapshot& a, const Snapshot& b) noexcept;
    double effectiveSampleRate() const noexcept;

    int nodeAt (juce::Point<float> position) const;
    juce::Point<float> nodePosition (int band, juce::Rectangle<float> plot) const;
    struct Drag
    {
        int band = -1;
        double frequency = 0.0;
        float gainDb = 0.0f;
    };

    void setBand (int band, double hz, float gainDb);
    void scaleBandQ (int band, double factor);

    juce::AudioProcessorValueTreeState& apvts;
    std::function<double()> getSampleRate;

    Snapshot lastSnapshot;
    int selectedBand = 0;

    Drag drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrequencyResponseCurve)
};
