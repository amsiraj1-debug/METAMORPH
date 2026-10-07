#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>

class MorphProcessor final : public juce::AudioProcessor, private juce::Thread
{
public:
    MorphProcessor();
    ~MorphProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Metamorph Rebuild"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    bool setModel(const juce::File&);
    juce::String modelName() const;
    bool armRecord();
    void stopRecord();
    bool importAudio(const juce::File&);
    bool transform();
    void cancelTransform();
    void resetAudio();
    void togglePreview();
    bool exportAudio(const juce::File&);
    void setResources(const juce::File&);
    juce::String status() const;
    std::array<float,256> waveform() const;
    bool hasAudio() const { return recordedSamples.load() > 0; }
    double duration() const { return recordedSamples.load() / sampleRate.load(); }
    bool isBusy() const { return busy.load(); }
    bool isArmed() const { return armed.load(); }
    bool hasResult() const { return resultReady.load(); }
    bool isPreviewing() const { return preview.load(); }
    float level() const { return meter.load(); }
    float completion() const { return progress.load(); }
    juce::AudioProcessorValueTreeState parameters;

private:
    void run() override;
    void message(juce::String);
    juce::File resourceRoot() const;
    bool writeWave(const juce::File&, const juce::AudioBuffer<float>&, int, double) const;
    bool readWave(const juce::File&, juce::AudioBuffer<float>&, double, int maximum) const;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    mutable juce::CriticalSection textLock;
    mutable juce::SpinLock audioLock;
    juce::String statusText { "Choose a voice model, then record or import audio." };
    juce::File modelFile, resourcesOverride, sessionFolder;
    juce::AudioBuffer<float> captured, transformed;
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> recordedSamples { 0 };
    std::atomic<bool> armed { false }, busy { false }, resultReady { false }, preview { false };
    std::atomic<float> meter { 0 }, progress { 0 };
    std::atomic<juce::int64> lastHostSample { 0 };
    juce::int64 originSample = 0, fallbackSample = 0;
    int previewSample = 0;
    bool awaitingOrigin = false, wasRecording = false;
    float jobPitch = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphProcessor)
};
