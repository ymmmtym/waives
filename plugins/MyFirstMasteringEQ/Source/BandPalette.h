#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// One colour per band, shared by the graph and the band buttons so the two
// always agree about which band is which.
namespace waives::ui
{
    inline juce::Colour bandColour (int band) noexcept
    {
        static const juce::Colour colours[] {
            juce::Colour (0xff9a86ff),   // violet
            juce::Colour (0xff35c1b8),   // teal
            juce::Colour (0xfff2b755),   // amber
            juce::Colour (0xffe2639c),   // pink
            juce::Colour (0xff5b8dff),   // blue
            juce::Colour (0xff86d96b),   // green
            juce::Colour (0xffff8a5c),   // orange
            juce::Colour (0xffd78bff),   // lilac
        };

        return colours[juce::jlimit (0, (int) (sizeof (colours) / sizeof (colours[0])) - 1, band)];
    }
}
