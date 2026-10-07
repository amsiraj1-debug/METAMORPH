#pragma once
#include "Processor.h"

class MorphEditor final : public juce::AudioProcessorEditor, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    explicit MorphEditor(MorphProcessor&);
    ~MorphEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&,int,int) override;
private:
    void timerCallback() override;
    void choose(int kind);
    MorphProcessor& processor;
    juce::LookAndFeel_V4 look;
    juce::TextButton model{"Choose voice .pth"},import{"Import audio"},record{"Record"},transform{"Transform"},reset{"Reset"},preview{"Preview"},save{"Export WAV"},settings{"Runtime folder"};
    juce::ToggleButton bypass{"Bypass"};
    juce::Slider mix,pitch;
    juce::Label statusLabel,modelLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment,pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphEditor)
};
