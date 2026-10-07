#include "DnniBackend.h"

namespace
{
constexpr unsigned char expectedHeader[8] {
    0xff, 0x00, 0xca, 0x7f, 0x02, 0x00, 0x00, 0x00
};

constexpr std::array<double, 10> nativeBandFrequencies {
    90.0, 160.0, 280.0, 500.0, 900.0,
    1600.0, 2900.0, 5200.0, 9000.0, 14500.0
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

juce::String DnniModelBackend::nativeSummary() const
{
    return juce::String (nativeModel.recordCount) + " records, "
        + juce::String ((int) nativeModel.quantMatrices.size()) + " quantized matrices, "
        + juce::String ((int) nativeModel.floatVectors.size()) + " float vectors, "
        + juce::String ((int) nativeModel.conv1dOps.size()) + " Conv1D descriptors";
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

    if (file.getSize() < 28)
    {
        errorMessage = "DNnI model is too small to contain a valid record stream.";
        return false;
    }

    auto stream = file.createInputStream();
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

bool DnniModelBackend::loadNativeModel (const juce::File& file,
                                        juce::String& errorMessage)
{
    DnniNativeModelInfo parsed;

    if (! nativeReader.parseNativeModel (file, parsed, errorMessage))
        return false;

    std::array<float, 10> signature {};

    if (! nativeReader.deriveModelSpectralSignature (parsed, signature, errorMessage))
        return false;

    nativeModel = std::move (parsed);
    nativeSignatureDb = signature;
    nativeReady = true;

    if (nativePrepared)
        updateNativeFilterCoefficients();

    return true;
}

bool DnniModelBackend::loadModel (const juce::File& file,
                                  juce::String& errorMessage)
{
    destroySession();
    nativeReady = false;
    nativeModel = {};
    nativeSignatureDb.fill (0.0f);

    if (! validateModelFile (file, errorMessage))
    {
        modelIsValid = false;
        setStatus (errorMessage);
        return false;
    }

    if (! loadNativeModel (file, errorMessage))
    {
        modelIsValid = false;
        setStatus ("DNnI native parser could not load model: " + errorMessage);
        return false;
    }

    {
        const juce::ScopedLock lock (stateLock);
        selectedModel = file;
    }

    modelIsValid = true;

    const bool bridgeAvailable = loadBridge();

    if (bridgeAvailable && preparedSampleRate > 0.0 && createSession())
    {
        setStatus (
            "DNnI full runtime active. Native parser also verified " + nativeSummary() + ".");
        return true;
    }

    setStatus (
        "Native DNnI engine active: " + nativeSummary()
        + ". Built-in model-assisted processing enabled; full operator graph decoding is still partial.");

    return true;
}

void DnniModelBackend::clearModel()
{
    destroySession();
    modelIsValid = false;
    nativeReady = false;
    nativeModel = {};
    nativeSignatureDb.fill (0.0f);

    {
        const juce::ScopedLock lock (stateLock);
        selectedModel = {};
        bridgePath.clear();
    }

    setStatus ("No DNnI model loaded");
}

void DnniModelBackend::prepareNativeFilters()
{
    if (preparedSampleRate <= 0.0
        || preparedMaximumBlockSize <= 0
        || preparedChannels <= 0)
        return;

    juce::dsp::ProcessSpec spec {
        preparedSampleRate,
        (juce::uint32) preparedMaximumBlockSize,
        (juce::uint32) preparedChannels
    };

    for (auto& filter : nativeFilters)
    {
        filter.prepare (spec);
        filter.reset();
    }

    nativePrepared = true;
    updateNativeFilterCoefficients();
}

void DnniModelBackend::updateNativeFilterCoefficients()
{
    if (! nativePrepared || preparedSampleRate <= 0.0)
        return;

    for (int i = 0; i < (int) nativeFilters.size(); ++i)
    {
        const double frequency =
            juce::jmin (nativeBandFrequencies[(size_t) i], preparedSampleRate * 0.45);

        // Keep this stage intentionally conservative. The values come from
        // decoded DNnI tensor row-scale structure and are followed by the
        // existing reference-voice matching stage in the processor.
        const float gainDb =
            juce::jlimit (-4.5f, 4.5f, nativeSignatureDb[(size_t) i] * 0.75f);

        *nativeFilters[(size_t) i].state =
            *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                preparedSampleRate,
                frequency,
                0.90,
                juce::Decibels::decibelsToGain (gainDb));
    }
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
        return false;

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

    return session != nullptr;
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

    prepareNativeFilters();

    if (modelIsValid)
    {
        if (bridge == nullptr)
            loadBridge();

        if (bridge != nullptr && createSession())
            setStatus ("DNnI full runtime active. Native parser verified " + nativeSummary() + ".");
        else if (nativeReady)
            setStatus (
                "Native DNnI engine active: " + nativeSummary()
                + ". Built-in model-assisted processing enabled; full operator graph decoding is still partial.");
    }
}

void DnniModelBackend::reset()
{
    for (auto& filter : nativeFilters)
        filter.reset();

    if (session != nullptr && resetFn != nullptr)
        resetFn (session);
}

bool DnniModelBackend::process (juce::AudioBuffer<float>& buffer)
{
    if (isFullGraphReady())
    {
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
                ? "DNnI full-runtime error: " + detail + ". Native model-assisted path remains active."
                : "DNnI full-runtime error. Native model-assisted path remains active.");

        destroySession();
    }

    if (! nativeReady || ! nativePrepared)
        return false;

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    for (auto& filter : nativeFilters)
        filter.process (context);

    return true;
}
