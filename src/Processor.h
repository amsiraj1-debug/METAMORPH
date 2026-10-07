#pragma once
#include <JuceHeader.h>
#include "VoiceProfile.h"
#include "PitchShifter.h"
#include "DnniBackend.h"
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

    bool loadDnniModel (const juce::File& file, juce::String& errorMessage);
    void clearDnniModel();
    juce::String getDnniStatus() const { return dnniBackend.getStatus(); }
    juce::File getDnniModelFile() const { return dnniBackend.getModelFile(); }
    bool isDnniReady() const noexcept { return dnniBackend.isReady(); }

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
    float getWaypointRadius (int index) const;
    juce::Point<float> getEffectiveMorphPoint() const;
    float getEffectiveRadius() const;

    float getInputMeter() const noexcept { return inputMeter.load(); }
    float getOutputMeter() const noexcept { return outputMeter.load(); }

    static APVTS::ParameterLayout layout();

private:
    using BandFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    std::optional<VoiceProfile> analyseVoiceFile (const juce::File& file, juce::String& errorMessage);
    std::array<float, VoiceProfile::bandCount> computeMorphBandGains (float x, float y, float radius) const;
    void updateFilterTargets (const std::array<float, VoiceProfile::bandCount>& targetDb);
    void updateLiveSourceSpectrum (const juce::AudioBuffer<float>& source);
    void applyParameterValue (const juce::String& id, float plainValue);
    static float computePeak (const juce::AudioBuffer<float>& buffer);
    static float computeRms (const juce::AudioBuffer<float>& buffer);

    juce::AudioFormatManager formatManager;
    mutable juce::CriticalSection profilesLock;
    std::vector<VoiceProfile> profiles;

    std::array<BandFilter, VoiceProfile::bandCount> bandFilters;
    BandFilter bodyShelf;
    BandFilter presenceShelf;
    BandFilter airShelf;
    std::array<juce::SmoothedValue<float>, VoiceProfile::bandCount> smoothedBandDb;
    DualDelayPitchShifter pitchShifter;
    DnniModelBackend dnniBackend;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> modelWorkBuffer;

    static constexpr int liveFftOrder = 11;
    static constexpr int liveFftSize = 1 << liveFftOrder;
    juce::dsp::FFT liveFft { liveFftOrder };
    juce::dsp::WindowingFunction<float> liveWindow { (size_t) liveFftSize, juce::dsp::WindowingFunction<float>::hann, true };
    std::array<float, liveFftSize> liveAnalysisRing {};
    std::array<float, liveFftSize * 2> liveFftData {};
    std::array<float, VoiceProfile::bandCount> liveSourceBandDb {};
    int liveAnalysisWrite { 0 };
    int liveAnalysisFill { 0 };
    juce::SmoothedValue<float> loudnessCompensation;

    double currentSampleRate { 44100.0 };

    std::array<std::atomic<float>, 8> waypointX {};
    std::array<std::atomic<float>, 8> waypointY {};
    std::array<std::atomic<float>, 8> waypointRadius {};
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
