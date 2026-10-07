#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include <vector>

enum class DnniRegionKind
{
    Unknown,
    Text,
    Float32Candidate,
    Float16Candidate,
    Int8Candidate,
    CompressedOrEncrypted
};

struct DnniRegion
{
    juce::int64 offset { 0 };
    juce::int64 size { 0 };
    double entropy { 0.0 };
    double printableRatio { 0.0 };
    double zeroRatio { 0.0 };
    DnniRegionKind kind { DnniRegionKind::Unknown };
};

struct DnniRecordInfo
{
    std::array<uint8_t, 4> marker {};
    std::array<uint8_t, 12> identifier {};
    juce::int64 recordOffset { 0 };
    juce::int64 payloadOffset { 0 };
    uint32_t payloadSize { 0 };
};

struct DnniQuantMatrixInfo
{
    int recordIndex { -1 };
    uint32_t code { 0 };
    uint32_t rows { 0 };
    uint32_t cols { 0 };
    juce::int64 scalesOffset { 0 };
    juce::int64 weightsOffset { 0 };
};

struct DnniFloatVectorInfo
{
    int recordIndex { -1 };
    uint32_t count { 0 };
    juce::int64 dataOffset { 0 };
};

struct DnniConv1DInfo
{
    int recordIndex { -1 };
    uint32_t kernelSize { 0 };
    uint32_t stride { 0 };
    uint32_t padding { 0 };
    uint32_t dilation { 0 };
    uint32_t groups { 0 };
};

struct DnniNativeModelInfo
{
    juce::File file;
    juce::int64 fileSize { 0 };
    int recordCount { 0 };
    int marker40Count { 0 };
    int marker41Count { 0 };
    bool consumedExactly { false };
    std::vector<DnniRecordInfo> records;
    std::vector<DnniQuantMatrixInfo> quantMatrices;
    std::vector<DnniFloatVectorInfo> floatVectors;
    std::vector<DnniConv1DInfo> conv1dOps;
};

struct DnniInspectionReport
{
    juce::File file;
    juce::String sha256;
    juce::int64 fileSize { 0 };
    juce::String headerHex;
    bool signatureMatches { false };
    std::vector<DnniRegion> regions;
    juce::StringArray strings;

    juce::var toJson() const;
};

class DnniReader
{
public:
    static constexpr int defaultBlockSize = 64 * 1024;

    bool inspect (const juce::File& file,
                  DnniInspectionReport& report,
                  juce::String& errorMessage,
                  int blockSize = defaultBlockSize) const;

    static juce::String kindToString (DnniRegionKind kind);

    bool parseNativeModel (const juce::File& file,
                           DnniNativeModelInfo& model,
                           juce::String& errorMessage) const;

    bool readQuantMatrixScales (const DnniNativeModelInfo& model,
                                int matrixIndex,
                                std::vector<float>& scales,
                                juce::String& errorMessage) const;

    bool readQuantMatrixRow (const DnniNativeModelInfo& model,
                             int matrixIndex,
                             int row,
                             std::vector<float>& output,
                             juce::String& errorMessage) const;

    bool deriveModelSpectralSignature (const DnniNativeModelInfo& model,
                                       std::array<float, 10>& signatureDb,
                                       juce::String& errorMessage) const;

private:
    static double calculateEntropy (const uint8_t* data, size_t size);
    static double calculatePrintableRatio (const uint8_t* data, size_t size);
    static double calculateZeroRatio (const uint8_t* data, size_t size);
    static bool looksLikeFloat32 (const uint8_t* data, size_t size);
    static bool looksLikeFloat16 (const uint8_t* data, size_t size);
    static bool looksLikeInt8Weights (const uint8_t* data, size_t size);
    static DnniRegionKind classifyBlock (const uint8_t* data,
                                         size_t size,
                                         double entropy,
                                         double printableRatio,
                                         double zeroRatio);
    static void extractStrings (const uint8_t* data,
                                size_t size,
                                juce::StringArray& output,
                                int minimumLength = 5);
    static juce::String bytesToHex (const uint8_t* data, size_t size);
};
