#pragma once
#include <JuceHeader.h>
#include "Processor.h"

class MorphCanvas final : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    explicit MorphCanvas (MorphProcessor& p) : processor (p) {}

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    std::function<void(const juce::String&)> onStatus;

private:
    juce::Point<float> toNormalised (juce::Point<float> p) const;
    juce::Point<float> fromNormalised (juce::Point<float> p) const;
    void updateCursorFromMouse (juce::Point<float> p);

    MorphProcessor& processor;
    bool draggingFile { false };
};

class MorphEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit MorphEditor (MorphProcessor&);
    ~MorphEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureSlider (juce::Slider& slider, const juce::String& suffix = {});
    void updateVoiceList();
    void chooseReferenceFile();
    void showStatus (const juce::String& text);

    MorphProcessor& processor;
    MorphCanvas morphCanvas;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label latencyLabel;
    juce::Label waypointInfoLabel;

    juce::Slider preGainSlider;
    juce::Slider pitchSlider;
    juce::Slider outputSlider;
    juce::Slider mixSlider;
    juce::Slider radiusSlider;
    juce::Slider strengthSlider;
    juce::Slider voiceSpaceSlider;
    juce::Slider toneSlider;

    juce::ToggleButton realtimeButton { "Realtime" };
    juce::ToggleButton bypassButton { "Bypass" };

    juce::ComboBox qualityBox;
    juce::ComboBox inputModeBox;
    juce::ComboBox voiceList;
    juce::ComboBox presetBox;

    juce::TextEditor hexEditor;

    juce::TextButton importButton { "IMPORT VOICE" };
    juce::TextButton generateButton { "GENERATE" };
    juce::TextButton removeButton { "REMOVE" };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton saveWaypointButton { "+ WAYPOINT" };
    juce::TextButton previousWaypointButton { "<" };
    juce::TextButton nextWaypointButton { ">" };
    juce::TextButton clearWaypointsButton { "CLEAR WP" };
    std::array<juce::TextButton, 8> waypointButtons;

    std::unique_ptr<juce::FileChooser> chooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> preGainAttachment;
    std::unique_ptr<SliderAttachment> pitchAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> radiusAttachment;
    std::unique_ptr<SliderAttachment> strengthAttachment;
    std::unique_ptr<ComboAttachment> qualityAttachment;
    std::unique_ptr<ComboAttachment> inputModeAttachment;
    std::unique_ptr<ButtonAttachment> realtimeAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    int lastVoiceCount { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphEditor)
};
