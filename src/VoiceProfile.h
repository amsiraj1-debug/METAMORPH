#pragma once
#include <JuceHeader.h>
#include <array>
#include <optional>

struct VoiceProfile
{
    static constexpr int bandCount = 10;

    juce::String name { "Voice" };
    juce::String sourcePath;
    juce::String hexCode { "#808080" };
    juce::Colour colour { juce::Colours::cornflowerblue };
    juce::Point<float> position { 0.5f, 0.5f };
    std::array<float, bandCount> bandDb {};
    float pitchLowHz { 90.0f };
    float pitchHighHz { 300.0f };
    bool generated { false };

    juce::ValueTree toValueTree() const;
    static std::optional<VoiceProfile> fromValueTree (const juce::ValueTree& tree);
};
