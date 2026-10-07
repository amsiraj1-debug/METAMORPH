#include "DnniBackend.h"

namespace
{
constexpr unsigned char expectedHeader[8] {
    0xff, 0x00, 0xca, 0x7f, 0x02, 0x00, 0x00, 0x00
};

juce::File findBridgeFile()
{
    if (auto value = juce::SystemStats::getEnvironmentVariable ("METAMORPH_DNNI_BRIDGE", {});
        value.isNotEmpty())
    {
        juce::File fromEnvironment (value);
        if (fromEnvironment.existsAsFile())
            return fromEnvironment;
    }

   #if JUCE_WINDOWS
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto besideExecutable = executable.getSiblingFile ("MetamorphDnniBridge.dll");
    if (besideExecutable.existsAsFile())
        return besideExecutable;
   #endif

    return {};
}
}

DnniModelBackend::~DnniModelBackend()
{
    destroySession();
}

void DnniModelBackend::setStatus (juce::String text)
{
    const juce::ScopedLock lock (stateLock);
    status = std::move (text);
}

juce::String DnniModelBackend::getStatus() const
{
    const juce::ScopedLock lock (stateLock);
    return status;
}

juce::File DnniModelBackend::getModelFile() const
{
    const juce::ScopedLock lock (stateLock);
    return selectedModel;
}

juce::String DnniModelBackend::getBridgePath() const
{
    const juce::ScopedLock lock (stateLock);
    return bridgePath;
}

bool DnniModelBackend::validateModelFile (const juce::File& file,
                                          juce::String& errorMessage) const
{
    if (! file.existsAsFile())
    {
        errorMessage = "DNnI model file does not exist.";
        return false;
    }

    if (file.getFileExtension().toLowerCase() != ".dnni")
    {
        errorMessage = "Select a .dnni model file.";
        return false;
    }

    if (file.getSize() < 1024 * 1024)
    {
        errorMessage = "DNnI model is too small to be a usable neural model.";
        return false;
    }

    std::unique_ptr<juce::FileInputStream> stream (file.createInputStream());
    if (stream == nullptr || ! stream->openedOk())
    {
        errorMessage = "Could not read the DNnI model.";
        return false;
    }

    unsigned char header[8] {};
    if (stream->read (header, 8) != 8)
    {
        errorMessage = "Could not read the DNnI model header.";
        return false;
    }

    for (int i = 0; i < 8; ++i)
    {
        if (header[i] != expectedHeader[i])
        {
            errorMessage =
                "The selected .dnni file does not match the supported DNnI container signature.";
            return false;
        }
    }

    return true;
}

bool DnniModelBackend::loadModel (const juce::File& file,
                                  juce::String& errorMessage)
{
    destroySession();

    if (! validateModelFile (file, errorMessage))
    {
        modelIsValid = false;
        setStatus (errorMessage);
        return false;
    }

    {
        const juce::ScopedLock lock (stateLock);
        selectedModel = file;
    }

    modelIsValid = true;

    if (file.getSize() == expectedModelSize)
    {
        setStatus ("DNnI model validated: model.dnni (87.5 MB). Looking for runtime bridge...");
    }
    else
    {
        setStatus ("DNnI model validated. Looking for runtime bridge...");
    }

    if (! loadBridge())
    {
        setStatus (
            "DNnI model loaded, but no compatible DNNI runtime bridge is installed. "
            "Using Reference Match fallback.");
        return true;
    }

    if (preparedSampleRate > 0.0 && ! createSession())
    {
        errorMessage = getStatus();
        return true;
    }

    return true;
}

void DnniModelBackend::clearModel()
{
    destroySession();
    modelIsValid = false;

    {
        const juce::ScopedLock lock (stateLock);
        selectedModel = {};
    }

    setStatus ("No DNnI model loaded");
}

bool DnniModelBackend::loadBridge()
{
    bridge.reset();
    createFn = nullptr;
    processFn = nullptr;
    resetFn = nullptr;
    destroyFn = nullptr;
    lastErrorFn = nullptr;

    const auto bridgeFile = findBridgeFile();
    if (! bridgeFile.existsAsFile())
        return false;

    auto candidate = std::make_unique<juce::DynamicLibrary>();
    if (! candidate->open (bridgeFile.getFullPathName()))
    {
        setStatus ("Found DNNI bridge but could not load it: " + bridgeFile.getFileName());
        return false;
    }

    createFn = reinterpret_cast<CreateFn> (candidate->getFunction ("metamorph_dnni_create"));
    processFn = reinterpret_cast<ProcessFn> (candidate->getFunction ("metamorph_dnni_process"));
    resetFn = reinterpret_cast<ResetFn> (candidate->getFunction ("metamorph_dnni_reset"));
    destroyFn = reinterpret_cast<DestroyFn> (candidate->getFunction ("metamorph_dnni_destroy"));
    lastErrorFn = reinterpret_cast<LastErrorFn> (candidate->getFunction ("metamorph_dnni_last_error"));

    if (createFn == nullptr || processFn == nullptr || destroyFn == nullptr)
    {
        createFn = nullptr;
        processFn = nullptr;
        resetFn = nullptr;
        destroyFn = nullptr;
        lastErrorFn = nullptr;
        setStatus (
            "DNNI bridge is missing required Metamorph adapter exports. "
            "Using Reference Match fallback.");
        return false;
    }

    {
        const juce::ScopedLock lock (stateLock);
        bridgePath = bridgeFile.getFullPathName();
    }

    bridge = std::move (candidate);
    return true;
}

bool DnniModelBackend::createSession()
{
    destroySession();

    if (! modelIsValid || createFn == nullptr || preparedSampleRate <= 0.0)
        return false;

    const auto modelPathString = selectedModel.getFullPathName();
    const auto modelPath = modelPathString.toUTF8();
    session = createFn (modelPath.getAddress(),
                        preparedSampleRate,
                        preparedMaximumBlockSize,
                        preparedChannels);

    if (session == nullptr)
    {
        juce::String detail;
        if (lastErrorFn != nullptr)
        {
            if (const auto* message = lastErrorFn (nullptr))
                detail = juce::String::fromUTF8 (message);
        }

        setStatus (
            detail.isNotEmpty()
                ? "DNNI runtime could not open model: " + detail
                : "DNNI runtime could not open the selected model. Using Reference Match fallback.");
        return false;
    }

    setStatus ("DNnI model active: neural conversion backend ready");
    return true;
}

void DnniModelBackend::destroySession()
{
    if (session != nullptr && destroyFn != nullptr)
        destroyFn (session);

    session = nullptr;
}

void DnniModelBackend::prepare (double sampleRate,
                                int maximumBlockSize,
                                int channels)
{
    preparedSampleRate = sampleRate;
    preparedMaximumBlockSize = maximumBlockSize;
    preparedChannels = channels;

    if (modelIsValid)
    {
        if (bridge == nullptr)
            loadBridge();

        if (bridge != nullptr)
            createSession();
    }
}

void DnniModelBackend::reset()
{
    if (session != nullptr && resetFn != nullptr)
        resetFn (session);
}

bool DnniModelBackend::process (juce::AudioBuffer<float>& buffer)
{
    if (! isReady())
        return false;

    std::vector<float*> channels;
    channels.reserve ((size_t) buffer.getNumChannels());

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        channels.push_back (buffer.getWritePointer (channel));

    const int result =
        processFn (session,
                   channels.data(),
                   buffer.getNumChannels(),
                   buffer.getNumSamples());

    if (result == 0)
        return true;

    juce::String detail;
    if (lastErrorFn != nullptr)
    {
        if (const auto* message = lastErrorFn (session))
            detail = juce::String::fromUTF8 (message);
    }

    setStatus (
        detail.isNotEmpty()
            ? "DNNI processing error: " + detail + ". Using Reference Match fallback."
            : "DNNI processing error. Using Reference Match fallback.");

    return false;
}
