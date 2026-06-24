#pragma once
#include <JuceHeader.h>

inline juce::Colour getBandColor(int bandIndex) noexcept
{
    static const juce::Colour palette[] = {
        juce::Colour(0xffff5252),  // red
        juce::Colour(0xffff9800),  // orange
        juce::Colour(0xffffeb3b),  // yellow
        juce::Colour(0xff8bc34a),  // lime
        juce::Colour(0xff26c6da),  // cyan
        juce::Colour(0xff5c9eff),  // blue
        juce::Colour(0xffab47bc),  // purple
        juce::Colour(0xfff06292),  // pink
        juce::Colour(0xff90a4ae),  // steel
    };
    return palette[bandIndex % 9];
}

inline const char* getFilterTypeShort(int typeIndex) noexcept
{
    static const char* names[] = { "B","LS","HS","LC","HC","N","BP","AP","T" };
    if (typeIndex >= 0 && typeIndex < 9) return names[typeIndex];
    return "?";
}

inline const char* getChannelModeShort(int modeIndex) noexcept
{
    static const char* names[] = { "", "L", "R", "M", "S" };
    if (modeIndex >= 0 && modeIndex < 5) return names[modeIndex];
    return "";
}
