#include <JuceHeader.h>
#include "../src/DnniReader.h"
#include <iostream>

namespace
{
void printUsage()
{
    std::cout
        << "Metamorph DNnI Inspector\n"
        << "Usage:\n"
        << "  DnniInspector <model.dnni> [report.json] [block-size]\n\n"
        << "Example:\n"
        << "  DnniInspector model.dnni model-report.json 65536\n";
}
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    const juce::File input (juce::String::fromUTF8 (argv[1]));

    juce::File output =
        argc >= 3
            ? juce::File (juce::String::fromUTF8 (argv[2]))
            : input.getSiblingFile (input.getFileNameWithoutExtension() + "-report.json");

    int blockSize = DnniReader::defaultBlockSize;

    if (argc >= 4)
        blockSize = juce::String::fromUTF8 (argv[3]).getIntValue();

    DnniReader reader;
    DnniInspectionReport report;
    juce::String error;

    if (! reader.inspect (input, report, error, blockSize))
    {
        std::cerr << "ERROR: " << error << "\n";
        return 2;
    }

    const auto json = juce::JSON::toString (report.toJson(), true);

    if (! output.replaceWithText (json))
    {
        std::cerr << "ERROR: could not write report to "
                  << output.getFullPathName() << "\n";
        return 3;
    }

    int float32Regions = 0;
    int float16Regions = 0;
    int int8Regions = 0;
    int opaqueRegions = 0;

    for (const auto& region : report.regions)
    {
        switch (region.kind)
        {
            case DnniRegionKind::Float32Candidate: ++float32Regions; break;
            case DnniRegionKind::Float16Candidate: ++float16Regions; break;
            case DnniRegionKind::Int8Candidate: ++int8Regions; break;
            case DnniRegionKind::CompressedOrEncrypted: ++opaqueRegions; break;
            default: break;
        }
    }

    std::cout
        << "File: " << input.getFullPathName() << "\n"
        << "Size: " << report.fileSize << " bytes\n"
        << "SHA-256: " << report.sha256 << "\n"
        << "Signature: " << (report.signatureMatches ? "recognized" : "unknown") << "\n"
        << "Merged regions: " << report.regions.size() << "\n"
        << "Candidate FP32 regions: " << float32Regions << "\n"
        << "Candidate FP16 regions: " << float16Regions << "\n"
        << "Candidate INT8 regions: " << int8Regions << "\n"
        << "High-entropy opaque regions: " << opaqueRegions << "\n"
        << "Extracted strings: " << report.strings.size() << "\n"
        << "JSON report: " << output.getFullPathName() << "\n";

    return 0;
}
