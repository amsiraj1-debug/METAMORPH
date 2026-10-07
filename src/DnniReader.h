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
