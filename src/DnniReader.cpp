#include "DnniReader.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr std::array<uint8_t, 8> expectedSignature {
    0xff, 0x00, 0xca, 0x7f, 0x02, 0x00, 0x00, 0x00
};

bool isPrintableByte (uint8_t value)
{
    return (value >= 32 && value <= 126) || value == '\n' || value == '\r' || value == '\t';
}

float readFloat32LittleEndian (const uint8_t* p)
{
    uint32_t bits =
        (uint32_t) p[0]
        | ((uint32_t) p[1] << 8)
        | ((uint32_t) p[2] << 16)
        | ((uint32_t) p[3] << 24);

    float value = 0.0f;
    std::memcpy (&value, &bits, sizeof (value));
    return value;
}

bool plausibleHalfBits (uint16_t bits)
{
    const uint16_t exponent = (bits >> 10) & 0x1f;
    const uint16_t mantissa = bits & 0x03ff;

    if (exponent == 0x1f)
        return mantissa == 0;

    return true;
}
}

juce::String DnniReader::kindToString (DnniRegionKind kind)
{
    switch (kind)
    {
        case DnniRegionKind::Text:                  return "text";
        case DnniRegionKind::Float32Candidate:      return "float32_candidate";
        case DnniRegionKind::Float16Candidate:      return "float16_candidate";
        case DnniRegionKind::Int8Candidate:         return "int8_candidate";
        case DnniRegionKind::CompressedOrEncrypted:return "compressed_or_encrypted";
        default:                                    return "unknown";
    }
}

double DnniReader::calculateEntropy (const uint8_t* data, size_t size)
{
    if (size == 0)
        return 0.0;

    std::array<size_t, 256> counts {};
    for (size_t i = 0; i < size; ++i)
        ++counts[data[i]];

    double entropy = 0.0;
    for (const auto count : counts)
    {
        if (count == 0)
            continue;

        const double p = (double) count / (double) size;
        entropy -= p * std::log2 (p);
    }

    return entropy;
}

double DnniReader::calculatePrintableRatio (const uint8_t* data, size_t size)
{
    if (size == 0)
        return 0.0;

    size_t printable = 0;
    for (size_t i = 0; i < size; ++i)
        if (isPrintableByte (data[i]))
            ++printable;

    return (double) printable / (double) size;
}

double DnniReader::calculateZeroRatio (const uint8_t* data, size_t size)
{
    if (size == 0)
        return 0.0;

    size_t zeros = 0;
    for (size_t i = 0; i < size; ++i)
        if (data[i] == 0)
            ++zeros;

    return (double) zeros / (double) size;
}

bool DnniReader::looksLikeFloat32 (const uint8_t* data, size_t size)
{
    if (size < 256)
        return false;

    const size_t count = juce::jmin ((size_t) 4096, size / 4);
    if (count < 64)
        return false;

    size_t finite = 0;
    size_t bounded = 0;
    size_t nonZero = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const float value = readFloat32LittleEndian (data + i * 4);

        if (std::isfinite (value))
        {
            ++finite;

            if (std::abs (value) <= 256.0f)
                ++bounded;

            if (std::abs (value) > 1.0e-12f)
                ++nonZero;
        }
    }

    const double finiteRatio = (double) finite / (double) count;
    const double boundedRatio = (double) bounded / (double) count;
    const double nonZeroRatio = (double) nonZero / (double) count;

    return finiteRatio > 0.985 && boundedRatio > 0.94 && nonZeroRatio > 0.20;
}

bool DnniReader::looksLikeFloat16 (const uint8_t* data, size_t size)
{
    if (size < 256)
        return false;

    const size_t count = juce::jmin ((size_t) 8192, size / 2);
    if (count < 128)
        return false;

    size_t plausible = 0;
    size_t finiteNormalish = 0;
    size_t nonZero = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const uint16_t bits =
            (uint16_t) data[i * 2]
            | ((uint16_t) data[i * 2 + 1] << 8);

        if (plausibleHalfBits (bits))
            ++plausible;

        const uint16_t exponent = (bits >> 10) & 0x1f;
        const uint16_t mantissa = bits & 0x03ff;

        if (exponent > 0 && exponent < 0x1f)
            ++finiteNormalish;

        if (exponent != 0 || mantissa != 0)
            ++nonZero;
    }

    return (double) plausible / (double) count > 0.995
        && (double) finiteNormalish / (double) count > 0.20
        && (double) nonZero / (double) count > 0.20;
}

bool DnniReader::looksLikeInt8Weights (const uint8_t* data, size_t size)
{
    if (size < 1024)
        return false;

    std::array<size_t, 256> counts {};
    for (size_t i = 0; i < size; ++i)
        ++counts[data[i]];

    size_t occupied = 0;
    size_t centreMass = 0;

    for (size_t i = 0; i < counts.size(); ++i)
    {
        if (counts[i] != 0)
            ++occupied;

        const int signedValue = (int) (int8_t) (uint8_t) i;
        if (std::abs (signedValue) <= 48)
            centreMass += counts[i];
    }

    const double centreRatio = (double) centreMass / (double) size;
    return occupied > 48 && centreRatio > 0.55;
}

DnniRegionKind DnniReader::classifyBlock (const uint8_t* data,
                                          size_t size,
                                          double entropy,
                                          double printableRatio,
                                          double zeroRatio)
{
    if (printableRatio > 0.83)
        return DnniRegionKind::Text;

    if (looksLikeFloat32 (data, size))
        return DnniRegionKind::Float32Candidate;

    if (looksLikeFloat16 (data, size))
        return DnniRegionKind::Float16Candidate;

    if (entropy < 7.45 && looksLikeInt8Weights (data, size))
        return DnniRegionKind::Int8Candidate;

    if (entropy > 7.78 && zeroRatio < 0.02)
        return DnniRegionKind::CompressedOrEncrypted;

    return DnniRegionKind::Unknown;
}

void DnniReader::extractStrings (const uint8_t* data,
                                 size_t size,
                                 juce::StringArray& output,
                                 int minimumLength)
{
    juce::String current;

    auto flush = [&]
    {
        if (current.length() >= minimumLength && ! output.contains (current))
            output.add (current);

        current.clear();
    };

    for (size_t i = 0; i < size; ++i)
    {
        const auto value = data[i];

        if (value >= 32 && value <= 126)
        {
            if (current.length() < 256)
                current += juce::String::charToString ((juce_wchar) value);
        }
        else
        {
            flush();
        }
    }

    flush();
}

juce::String DnniReader::bytesToHex (const uint8_t* data, size_t size)
{
    juce::String result;

    for (size_t i = 0; i < size; ++i)
    {
        if (i != 0)
            result += " ";

        result += juce::String::formatted ("%02X", (unsigned int) data[i]);
    }

    return result;
}

bool DnniReader::inspect (const juce::File& file,
                          DnniInspectionReport& report,
                          juce::String& errorMessage,
                          int blockSize) const
{
    if (! file.existsAsFile())
    {
        errorMessage = "File does not exist: " + file.getFullPathName();
        return false;
    }

    if (file.getFileExtension().toLowerCase() != ".dnni")
    {
        errorMessage = "Expected a .dnni file.";
        return false;
    }

    if (blockSize < 4096 || blockSize > 4 * 1024 * 1024)
    {
        errorMessage = "Block size must be between 4096 and 4194304 bytes.";
        return false;
    }

    auto stream = file.createInputStream();
    if (stream == nullptr || ! stream->openedOk())
    {
        errorMessage = "Could not open DNnI file.";
        return false;
    }

    report = {};
    report.file = file;
    report.fileSize = file.getSize();

    juce::SHA256 sha (file);
    report.sha256 = sha.toHexString();

    std::array<uint8_t, 32> header {};
    const int headerBytes = stream->read (header.data(), (int) header.size());

    if (headerBytes <= 0)
    {
        errorMessage = "Could not read DNnI header.";
        return false;
    }

    report.headerHex = bytesToHex (header.data(), (size_t) headerBytes);
    report.signatureMatches =
        headerBytes >= (int) expectedSignature.size()
        && std::equal (expectedSignature.begin(), expectedSignature.end(), header.begin());

    stream->setPosition (0);

    std::vector<uint8_t> block ((size_t) blockSize);
    juce::int64 offset = 0;

    while (! stream->isExhausted())
    {
        const int bytesRead = stream->read (block.data(), blockSize);
        if (bytesRead <= 0)
            break;

        const size_t size = (size_t) bytesRead;
        const double entropy = calculateEntropy (block.data(), size);
        const double printableRatio = calculatePrintableRatio (block.data(), size);
        const double zeroRatio = calculateZeroRatio (block.data(), size);
        const auto kind = classifyBlock (block.data(), size, entropy, printableRatio, zeroRatio);

        if (report.regions.empty() || report.regions.back().kind != kind)
        {
            DnniRegion region;
            region.offset = offset;
            region.size = bytesRead;
            region.entropy = entropy;
            region.printableRatio = printableRatio;
            region.zeroRatio = zeroRatio;
            region.kind = kind;
            report.regions.push_back (region);
        }
        else
        {
            auto& region = report.regions.back();
            const double oldSize = (double) region.size;
            const double newSize = oldSize + (double) bytesRead;

            region.entropy =
                (region.entropy * oldSize + entropy * (double) bytesRead) / newSize;
            region.printableRatio =
                (region.printableRatio * oldSize + printableRatio * (double) bytesRead) / newSize;
            region.zeroRatio =
                (region.zeroRatio * oldSize + zeroRatio * (double) bytesRead) / newSize;
            region.size += bytesRead;
        }

        if (printableRatio > 0.05)
            extractStrings (block.data(), size, report.strings);

        offset += bytesRead;
    }

    return true;
}

juce::var DnniInspectionReport::toJson() const
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("file", file.getFullPathName());
    root->setProperty ("file_size", fileSize);
    root->setProperty ("sha256", sha256);
    root->setProperty ("signature_matches", signatureMatches);
    root->setProperty ("header_hex", headerHex);

    juce::Array<juce::var> regionArray;

    for (const auto& region : regions)
    {
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty ("offset", region.offset);
        item->setProperty ("size", region.size);
        item->setProperty ("end", region.offset + region.size);
        item->setProperty ("entropy", region.entropy);
        item->setProperty ("printable_ratio", region.printableRatio);
        item->setProperty ("zero_ratio", region.zeroRatio);
        item->setProperty ("kind", DnniReader::kindToString (region.kind));
        regionArray.add (juce::var (item.release()));
    }

    root->setProperty ("regions", regionArray);

    juce::Array<juce::var> stringArray;
    const int maximumStrings = juce::jmin (strings.size(), 500);

    for (int i = 0; i < maximumStrings; ++i)
        stringArray.add (strings[i]);

    root->setProperty ("strings", stringArray);
    root->setProperty ("string_count", strings.size());

    return juce::var (root.release());
}
