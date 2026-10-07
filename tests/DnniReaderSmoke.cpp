#include "../src/DnniReader.h"
#include <JuceHeader.h>
#include <iostream>

int main()
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("dnni-reader-smoke", ".dnni");

    {
        auto stream = temp.createOutputStream();
        if (stream == nullptr || ! stream->openedOk())
            return 1;

        const uint8_t header[8] { 0xff, 0x00, 0xca, 0x7f, 0x02, 0x00, 0x00, 0x00 };
        stream->write (header, 8);

        const juce::String text ("layer encoder tensor weights test_metadata");
        stream->write (text.toRawUTF8(), text.getNumBytesAsUTF8());

        for (int i = 0; i < 4096; ++i)
        {
            const float value = std::sin ((float) i * 0.013f) * 0.25f;
            stream->write (&value, sizeof (value));
        }

        stream->flush();
    }

    DnniReader reader;
    DnniInspectionReport report;
    juce::String error;

    const bool ok = reader.inspect (temp, report, error, 4096);
    temp.deleteFile();

    if (! ok)
    {
        std::cerr << "Reader failed: " << error << "\n";
        return 2;
    }

    if (! report.signatureMatches)
    {
        std::cerr << "Signature not recognized\n";
        return 3;
    }

    if (report.fileSize <= 0 || report.regions.empty())
    {
        std::cerr << "No regions generated\n";
        return 4;
    }

    if (! report.strings.joinIntoString (" ").containsIgnoreCase ("encoder"))
    {
        std::cerr << "Expected ASCII string was not extracted\n";
        return 5;
    }

    const auto json = juce::JSON::toString (report.toJson(), false);
    if (! json.contains ("signature_matches"))
    {
        std::cerr << "JSON report is incomplete\n";
        return 6;
    }

    std::cout << "PASS: DNnI reader smoke test\n";
    return 0;
}
