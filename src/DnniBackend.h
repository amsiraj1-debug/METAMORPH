#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>

class DnniModelBackend
{
public:
    DnniModelBackend() = default;
    ~DnniModelBackend();

    bool loadModel (const juce::File& modelFile, juce::String& errorMessage);
    void clearModel();

    void prepare (double sampleRate, int maximumBlockSize, int channels);
    void reset();

    bool process (juce::AudioBuffer<float>& buffer);

    bool hasModel() const noexcept { return modelIsValid; }
    bool isReady() const noexcept { return session != nullptr && processFn != nullptr; }

    juce::File getModelFile() const;
    juce::String getStatus() const;
    juce::String getBridgePath() const;

    static constexpr juce::int64 expectedModelSize = 87529366;
    static constexpr const char* expectedSha256 =
        "48fe10df60bb4d92d2a5f19f02b4d712dc070bebba9d5ea1ef2172d9f82a428c";

private:
    using CreateFn = void* (*) (const char* modelPathUtf8,
                                double sampleRate,
                                int maximumBlockSize,
                                int channels);
    using ProcessFn = int (*) (void* session,
                               float** channelData,
                               int channels,
                               int samples);
    using ResetFn = void (*) (void* session);
    using DestroyFn = void (*) (void* session);
    using LastErrorFn = const char* (*) (void* session);

    bool validateModelFile (const juce::File&, juce::String& errorMessage) const;
    bool loadBridge();
    bool createSession();
    void destroySession();
    void setStatus (juce::String text);

    mutable juce::CriticalSection stateLock;
    juce::File selectedModel;
    juce::String status { "No DNnI model loaded" };
    juce::String bridgePath;

    std::unique_ptr<juce::DynamicLibrary> bridge;
    CreateFn createFn { nullptr };
    ProcessFn processFn { nullptr };
    ResetFn resetFn { nullptr };
    DestroyFn destroyFn { nullptr };
    LastErrorFn lastErrorFn { nullptr };
    void* session { nullptr };

    double preparedSampleRate { 0.0 };
    int preparedMaximumBlockSize { 0 };
    int preparedChannels { 0 };
    bool modelIsValid { false };
};
