#pragma once
#include <JuceHeader.h>
#include "VoiceProfile.h"
#include "PitchShifter.h"
#include <array>
#include <atomic>
#include <optional>
#include <vector>

class MorphProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    MorphProcessor();
    ~MorphProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Metamorph CR"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    APVTS parameters;

    bool addVoiceFromFile (const juce::File& file, juce::String& errorMessage);
    void addGeneratedVoice (juce::String hexCode, float voiceSpace, float tone, juce::String customName = {});
    void addSyntheticPreset (int presetIndex);
    void removeVoice (int index);
    void clearVoices();
    std::vector<VoiceProfile> getProfilesSnapshot() const;

    void setMorphPointFromUI (float x, float y);
    void setInfluenceFromUI (float radius);

    void addWaypointFromCurrent();
    void clearWaypoints();
    void activateWaypoint (int index);
    void activateNextWaypoint();
    void activatePreviousWaypoint();
    int getWaypointCount() const noexcept { return waypointCount.load(); }
    int getActiveWaypoint() const noexcept { return activeWaypoint.load(); }
    juce::Point<float> getWaypoint (int index) const;

    float getInputMeter() const noexcept { return inputMeter.load(); }
    float getOutputMeter() const noexcept { return outputMeter.load(); }

    static APVTS::ParameterLayout layout();

private:
    using BandFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    std::optional<VoiceProfile> analyseVoiceFile (const juce::File& file, juce::String& errorMessage);
    std::array<float, VoiceProfile::bandCount> computeMorphBandGains (float x, float y, float radius) const;
    void updateFilterTargets (const std::array<float, VoiceProfile::bandCount>& targetDb);
    void applyParameterValue (const juce::String& id, float plainValue);
    static float computePeak (const juce::AudioBuffer<float>& buffer);

    juce::AudioFormatManager formatManager;
    mutable juce::CriticalSection profilesLock;
    std::vector<VoiceProfile> profiles;

    std::array<BandFilter, VoiceProfile::bandCount> bandFilters;
    std::array<juce::SmoothedValue<float>, VoiceProfile::bandCount> smoothedBandDb;
    DualDelayPitchShifter pitchShifter;
    juce::AudioBuffer<float> dryBuffer;

    double currentSampleRate { 44100.0 };

    std::array<std::atomic<float>, 8> waypointX {};
    std::array<std::atomic<float>, 8> waypointY {};
    std::atomic<int> waypointCount { 0 };
    std::atomic<int> activeWaypoint { -1 };

    std::atomic<bool> midiMorphOverride { false };
    std::atomic<float> midiMorphX { 0.5f };
    std::atomic<float> midiMorphY { 0.5f };
    std::atomic<float> midiRadius { 0.34f };

    std::atomic<float> inputMeter { 0.0f };
    std::atomic<float> outputMeter { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphProcessor)
};
