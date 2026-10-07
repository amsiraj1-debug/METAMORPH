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

uint32_t readUInt32LittleEndian (const uint8_t* p)
{
    return (uint32_t) p[0]
        | ((uint32_t) p[1] << 8)
        | ((uint32_t) p[2] << 16)
        | ((uint32_t) p[3] << 24);
}

float readFloat32LittleEndian (const uint8_t* p)
{
    const uint32_t bits = readUInt32LittleEndian (p);

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


bool DnniReader::parseNativeModel (const juce::File& file,
                                   DnniNativeModelInfo& model,
                                   juce::String& errorMessage) const
{
    model = {};
    model.file = file;
    model.fileSize = file.getSize();

    if (! file.existsAsFile())
    {
        errorMessage = "DNnI file does not exist.";
        return false;
    }

    auto stream = file.createInputStream();
    if (stream == nullptr || ! stream->openedOk())
    {
        errorMessage = "Could not open DNnI file.";
        return false;
    }

    uint8_t signature[8] {};
    if (stream->read (signature, 8) != 8)
    {
        errorMessage = "Could not read DNnI file header.";
        return false;
    }

    if (! std::equal (expectedSignature.begin(), expectedSignature.end(), signature))
    {
        errorMessage = "Unknown DNnI signature.";
        return false;
    }

    juce::int64 position = 8;
    int recordIndex = 0;

    while (position < model.fileSize)
    {
        if (model.fileSize - position < 20)
        {
            errorMessage = "DNnI record header is truncated at offset " + juce::String (position) + ".";
            return false;
        }

        if (! stream->setPosition (position))
        {
            errorMessage = "Could not seek DNnI record at offset " + juce::String (position) + ".";
            return false;
        }

        uint8_t header[20] {};
        if (stream->read (header, 20) != 20)
        {
            errorMessage = "Could not read DNnI record header.";
            return false;
        }

        const bool marker40 =
            header[0] == 0xff && header[1] == 0x40 && header[2] == 0xca && header[3] == 0x7f;
        const bool marker41 =
            header[0] == 0xff && header[1] == 0x41 && header[2] == 0xca && header[3] == 0x7f;

        if (! marker40 && ! marker41)
        {
            errorMessage =
                "Unsupported DNnI record marker at offset " + juce::String (position) + ".";
            return false;
        }

        DnniRecordInfo record;
        std::copy (header, header + 4, record.marker.begin());
        std::copy (header + 4, header + 16, record.identifier.begin());
        record.recordOffset = position;
        record.payloadOffset = position + 20;
        record.payloadSize = readUInt32LittleEndian (header + 16);

        const juce::int64 payloadEnd =
            record.payloadOffset + (juce::int64) record.payloadSize;

        if (payloadEnd < record.payloadOffset || payloadEnd > model.fileSize)
        {
            errorMessage =
                "DNnI record payload extends beyond EOF at record " + juce::String (recordIndex) + ".";
            return false;
        }

        model.records.push_back (record);
        ++model.recordCount;
        if (marker40) ++model.marker40Count;
        if (marker41) ++model.marker41Count;

        const int prefixSize = (int) juce::jmin ((uint32_t) 20, record.payloadSize);
        uint8_t prefix[20] {};

        if (prefixSize > 0)
        {
            if (! stream->setPosition (record.payloadOffset)
                || stream->read (prefix, prefixSize) != prefixSize)
            {
                errorMessage =
                    "Could not read DNnI record payload prefix at record " + juce::String (recordIndex) + ".";
                return false;
            }
        }

        if (marker40 && record.payloadSize >= 4)
        {
            const uint32_t first = readUInt32LittleEndian (prefix);

            if ((juce::int64) record.payloadSize == 4 + (juce::int64) first * 4
                && first > 0 && first < 10000000u)
            {
                DnniFloatVectorInfo vector;
                vector.recordIndex = recordIndex;
                vector.count = first;
                vector.dataOffset = record.payloadOffset + 4;
                model.floatVectors.push_back (vector);
            }

            if (record.payloadSize >= 24 && prefixSize >= 16)
            {
                const uint32_t headerBytes = first;
                const uint32_t code = readUInt32LittleEndian (prefix + 4);
                const uint32_t reserved = readUInt32LittleEndian (prefix + 8);
                const uint32_t rows = readUInt32LittleEndian (prefix + 12);

                if (headerBytes == 16 && reserved == 0 && rows > 0 && rows < 65536u)
                {
                    const juce::int64 fixedBytes = 16 + (juce::int64) rows * 4 + 8;
                    const juce::int64 weightBytes = (juce::int64) record.payloadSize - fixedBytes;

                    if (weightBytes > 0 && weightBytes % rows == 0)
                    {
                        const juce::int64 cols64 = weightBytes / rows;

                        if (cols64 > 0 && cols64 < 65536)
                        {
                            DnniQuantMatrixInfo matrix;
                            matrix.recordIndex = recordIndex;
                            matrix.code = code;
                            matrix.rows = rows;
                            matrix.cols = (uint32_t) cols64;
                            matrix.scalesOffset = record.payloadOffset + 16;
                            matrix.weightsOffset = matrix.scalesOffset + (juce::int64) rows * 4;
                            model.quantMatrices.push_back (matrix);
                        }
                    }
                }
            }
        }

        if (marker41 && record.payloadSize == 20 && prefixSize == 20)
        {
            DnniConv1DInfo op;
            op.recordIndex = recordIndex;
            op.kernelSize = readUInt32LittleEndian (prefix);
            op.stride = readUInt32LittleEndian (prefix + 4);
            op.padding = readUInt32LittleEndian (prefix + 8);
            op.dilation = readUInt32LittleEndian (prefix + 12);
            op.groups = readUInt32LittleEndian (prefix + 16);

            if (op.kernelSize > 0 && op.kernelSize <= 31
                && op.stride > 0 && op.stride <= 16
                && op.dilation > 0 && op.dilation <= 64
                && op.groups > 0 && op.groups <= 65536)
            {
                model.conv1dOps.push_back (op);
            }
        }

        position = payloadEnd;
        ++recordIndex;
    }

    model.consumedExactly = position == model.fileSize;

    if (! model.consumedExactly || model.records.empty())
    {
        errorMessage = "DNnI record stream did not consume the file exactly.";
        return false;
    }

    return true;
}

bool DnniReader::readQuantMatrixScales (const DnniNativeModelInfo& model,
                                        int matrixIndex,
                                        std::vector<float>& scales,
                                        juce::String& errorMessage) const
{
    scales.clear();

    if (! juce::isPositiveAndBelow (matrixIndex, (int) model.quantMatrices.size()))
    {
        errorMessage = "DNnI matrix index is out of range.";
        return false;
    }

    const auto& matrix = model.quantMatrices[(size_t) matrixIndex];
    auto stream = model.file.createInputStream();

    if (stream == nullptr || ! stream->openedOk() || ! stream->setPosition (matrix.scalesOffset))
    {
        errorMessage = "Could not seek DNnI matrix scales.";
        return false;
    }

    std::vector<uint8_t> bytes ((size_t) matrix.rows * 4);
    if (stream->read (bytes.data(), (int) bytes.size()) != (int) bytes.size())
    {
        errorMessage = "Could not read DNnI matrix scales.";
        return false;
    }

    scales.resize (matrix.rows);
    for (uint32_t row = 0; row < matrix.rows; ++row)
    {
        const float value = readFloat32LittleEndian (bytes.data() + (size_t) row * 4);

        if (! std::isfinite (value) || value < 0.0f)
        {
            errorMessage = "DNnI matrix contains an invalid row scale.";
            scales.clear();
            return false;
        }

        scales[(size_t) row] = value;
    }

    return true;
}

bool DnniReader::readQuantMatrixRow (const DnniNativeModelInfo& model,
                                     int matrixIndex,
                                     int row,
                                     std::vector<float>& output,
                                     juce::String& errorMessage) const
{
    output.clear();

    if (! juce::isPositiveAndBelow (matrixIndex, (int) model.quantMatrices.size()))
    {
        errorMessage = "DNnI matrix index is out of range.";
        return false;
    }

    const auto& matrix = model.quantMatrices[(size_t) matrixIndex];

    if (! juce::isPositiveAndBelow (row, (int) matrix.rows))
    {
        errorMessage = "DNnI matrix row is out of range.";
        return false;
    }

    std::vector<float> scales;
    if (! readQuantMatrixScales (model, matrixIndex, scales, errorMessage))
        return false;

    auto stream = model.file.createInputStream();
    const juce::int64 rowOffset =
        matrix.weightsOffset + (juce::int64) row * matrix.cols;

    if (stream == nullptr || ! stream->openedOk() || ! stream->setPosition (rowOffset))
    {
        errorMessage = "Could not seek DNnI matrix row.";
        return false;
    }

    std::vector<uint8_t> quantized (matrix.cols);
    if (stream->read (quantized.data(), (int) quantized.size()) != (int) quantized.size())
    {
        errorMessage = "Could not read DNnI matrix row.";
        return false;
    }

    output.resize (matrix.cols);
    const float scale = scales[(size_t) row] / 127.0f;

    for (uint32_t column = 0; column < matrix.cols; ++column)
        output[(size_t) column] = (float) (int8_t) quantized[(size_t) column] * scale;

    return true;
}

bool DnniReader::deriveModelSpectralSignature (const DnniNativeModelInfo& model,
                                               std::array<float, 10>& signatureDb,
                                               juce::String& errorMessage) const
{
    signatureDb.fill (0.0f);

    if (model.quantMatrices.empty())
    {
        errorMessage = "DNnI model contains no decoded quantized matrices.";
        return false;
    }

    int selected = -1;

    for (int i = 0; i < (int) model.quantMatrices.size(); ++i)
    {
        const auto& matrix = model.quantMatrices[(size_t) i];

        if (matrix.rows == 72 && matrix.cols == 512)
        {
            selected = i;
            break;
        }
    }

    if (selected < 0)
        selected = 0;

    std::vector<float> scales;
    if (! readQuantMatrixScales (model, selected, scales, errorMessage))
        return false;

    if (scales.empty())
    {
        errorMessage = "DNnI matrix has no row scales.";
        return false;
    }

    std::array<float, 10> accum {};
    std::array<int, 10> counts {};

    for (int row = 0; row < (int) scales.size(); ++row)
    {
        const int band =
            juce::jlimit (0, 9, (row * 10) / juce::jmax (1, (int) scales.size()));

        const float db =
            20.0f * std::log10 (juce::jmax (1.0e-9f, scales[(size_t) row]));

        accum[(size_t) band] += db;
        ++counts[(size_t) band];
    }

    float mean = 0.0f;

    for (int band = 0; band < 10; ++band)
    {
        signatureDb[(size_t) band] =
            counts[(size_t) band] > 0
                ? accum[(size_t) band] / (float) counts[(size_t) band]
                : 0.0f;

        mean += signatureDb[(size_t) band];
    }

    mean *= 0.1f;

    for (auto& value : signatureDb)
        value = juce::jlimit (-6.0f, 6.0f, (value - mean) * 0.55f);

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
