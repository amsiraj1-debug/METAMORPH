#include "../src/Processor.h"
#include <JuceHeader.h>
#include <cmath>
#include <iostream>

namespace
{
bool near (float a, float b, float tolerance = 0.0025f)
{
    return std::abs (a - b) <= tolerance;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    MorphProcessor processor;
    processor.setPlayConfigDetails (1, 1, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    processor.addGeneratedVoice ("#E04B9A", 0.86f, 0.22f, "Regression Voice");
    const auto profiles = processor.getProfilesSnapshot();

    if (profiles.empty())
    {
        std::cerr << "FAIL: generated target voice was not created\n";
        return 1;
    }


    {
        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto dnniTestFile =
            tempDir.getNonexistentChildFile ("metamorph-native-dnni-smoke", ".dnni");

        auto writeU32 = [] (juce::OutputStream& out, uint32_t value)
        {
            const uint8_t bytes[4] {
                (uint8_t) (value & 0xff),
                (uint8_t) ((value >> 8) & 0xff),
                (uint8_t) ((value >> 16) & 0xff),
                (uint8_t) ((value >> 24) & 0xff)
            };
            out.write (bytes, 4);
        };

        auto writeRecord = [&writeU32] (juce::OutputStream& out,
                                        uint8_t markerType,
                                        const juce::MemoryBlock& payload)
        {
            const uint8_t marker[4] { 0xff, markerType, 0xca, 0x7f };
            const uint8_t id[12] {
                markerType, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
            };
            out.write (marker, 4);
            out.write (id, 12);
            writeU32 (out, (uint32_t) payload.getSize());
            if (payload.getSize() > 0)
                out.write (payload.getData(), payload.getSize());
        };

        if (auto output = dnniTestFile.createOutputStream())
        {
            const uint8_t fileHeader[8] {
                0xff, 0x00, 0xca, 0x7f, 0x02, 0x00, 0x00, 0x00
            };
            output->write (fileHeader, 8);

            juce::MemoryOutputStream vectorPayload;
            writeU32 (vectorPayload, 10);
            for (int i = 0; i < 10; ++i)
            {
                const float value = 0.1f + (float) i * 0.03f;
                vectorPayload.write (&value, sizeof (value));
            }
            juce::MemoryBlock vectorBlock (
                vectorPayload.getData(), vectorPayload.getDataSize());
            writeRecord (*output, 0x40, vectorBlock);

            juce::MemoryOutputStream matrixPayload;
            writeU32 (matrixPayload, 16);
            writeU32 (matrixPayload, 10);
            writeU32 (matrixPayload, 0);
            writeU32 (matrixPayload, 10);

            for (int row = 0; row < 10; ++row)
            {
                const float scale = 0.04f + (float) row * 0.006f;
                matrixPayload.write (&scale, sizeof (scale));
            }

            for (int row = 0; row < 10; ++row)
                for (int col = 0; col < 16; ++col)
                {
                    const int8_t q =
                        (int8_t) (((row * 13 + col * 7) % 101) - 50);
                    matrixPayload.write (&q, 1);
                }

            const uint8_t trailer[8] {};
            matrixPayload.write (trailer, 8);

            juce::MemoryBlock matrixBlock (
                matrixPayload.getData(), matrixPayload.getDataSize());
            writeRecord (*output, 0x40, matrixBlock);

            juce::MemoryOutputStream convPayload;
            writeU32 (convPayload, 1);
            writeU32 (convPayload, 1);
            writeU32 (convPayload, 0);
            writeU32 (convPayload, 1);
            writeU32 (convPayload, 1);

            juce::MemoryBlock convBlock (
                convPayload.getData(), convPayload.getDataSize());
            writeRecord (*output, 0x41, convBlock);

            output->flush();
        }

        juce::String dnniError;
        if (! processor.loadDnniModel (dnniTestFile, dnniError))
        {
            std::cerr << "FAIL: native DNnI record stream was rejected: "
                      << dnniError << "\n";
            dnniTestFile.deleteFile();
            return 8;
        }

        if (! processor.isDnniReady())
        {
            std::cerr << "FAIL: built-in DNnI engine did not become ready\n";
            dnniTestFile.deleteFile();
            return 9;
        }

        const auto nativeStatus = processor.getDnniStatus();

        if (! nativeStatus.containsIgnoreCase ("Native DNnI engine active"))
        {
            std::cerr << "FAIL: native engine status was not reported: "
                      << nativeStatus << "\n";
            dnniTestFile.deleteFile();
            return 10;
        }

        if (nativeStatus.containsIgnoreCase ("no compatible DNNI runtime bridge"))
        {
            std::cerr << "FAIL: obsolete runtime-bridge warning is still exposed\n";
            dnniTestFile.deleteFile();
            return 11;
        }

        juce::AudioBuffer<float> nativeTestAudio (1, 256);
        for (int i = 0; i < nativeTestAudio.getNumSamples(); ++i)
            nativeTestAudio.setSample (
                0, i, 0.15f * std::sin ((float) i * 0.08f));

        juce::MidiBuffer emptyMidi;
        processor.processBlock (nativeTestAudio, emptyMidi);

        for (int i = 0; i < nativeTestAudio.getNumSamples(); ++i)
        {
            if (! std::isfinite (nativeTestAudio.getSample (0, i)))
            {
                std::cerr << "FAIL: native DNnI model-assisted stage produced invalid audio\n";
                dnniTestFile.deleteFile();
                return 12;
            }
        }

        processor.clearDnniModel();
        dnniTestFile.deleteFile();
    }

    processor.setMorphPointFromUI    processor.setMorphPointFromUI (profiles.front().position.x, profiles.front().position.y);
    processor.setInfluenceFromUI (0.28f);

    if (auto* strength = processor.parameters.getParameter ("strength"))
        strength->setValueNotifyingHost (strength->convertTo0to1 (200.0f));

    juce::MidiBuffer midi;
    double differenceEnergy = 0.0;
    double sourceEnergy = 0.0;
    double outputEnergy = 0.0;
    double phase = 0.0;
    float maxOutput = 0.0f;

    for (int blockIndex = 0; blockIndex < 96; ++blockIndex)
    {
        juce::AudioBuffer<float> audio (1, 512);
        juce::AudioBuffer<float> dry (1, 512);

        for (int n = 0; n < 512; ++n)
        {
            const double t = phase / 48000.0;
            const float sample =
                0.16f * std::sin (juce::MathConstants<double>::twoPi * 120.0 * t)
              + 0.12f * std::sin (juce::MathConstants<double>::twoPi * 470.0 * t)
              + 0.09f * std::sin (juce::MathConstants<double>::twoPi * 1450.0 * t)
              + 0.06f * std::sin (juce::MathConstants<double>::twoPi * 5100.0 * t);

            audio.setSample (0, n, sample);
            dry.setSample (0, n, sample);
            phase += 1.0;
        }

        processor.processBlock (audio, midi);

        if (blockIndex > 12)
        {
            for (int n = 0; n < 512; ++n)
            {
                const double a = audio.getSample (0, n);
                const double b = dry.getSample (0, n);
                const double d = a - b;
                differenceEnergy += d * d;
                sourceEnergy += b * b;
                outputEnergy += a * a;
                maxOutput = juce::jmax (maxOutput, (float) std::abs (a));

                if (! std::isfinite (a))
                {
                    std::cerr << "FAIL: non-finite sample at 200% reference match\n";
                    return 5;
                }
            }
        }
    }

    const double relativeDifference =
        std::sqrt (differenceEnergy / std::max (1.0e-12, sourceEnergy));

    if (relativeDifference < 0.12)
    {
        std::cerr << "FAIL: morph DSP is too close to dry input, relative difference="
                  << relativeDifference << "\n";
        return 2;
    }

    const double rmsRatio =
        std::sqrt (outputEnergy / std::max (1.0e-12, sourceEnergy));
    const double loudnessDeltaDb =
        20.0 * std::log10 (std::max (1.0e-9, rmsRatio));

    if (std::abs (loudnessDeltaDb) > 1.25)
    {
        std::cerr << "FAIL: 200% reference match changed RMS by "
                  << loudnessDeltaDb << " dB instead of preserving level\n";
        return 6;
    }

    if (maxOutput > 0.98f)
    {
        std::cerr << "FAIL: 200% reference match approached clipping, peak="
                  << maxOutput << "\n";
        return 7;
    }

    processor.setMorphPointFromUI (0.23f, 0.71f);
    processor.setInfluenceFromUI (0.57f);
    processor.addWaypointFromCurrent();

    processor.setMorphPointFromUI (0.84f, 0.15f);
    processor.setInfluenceFromUI (0.11f);
    processor.activateWaypoint (0);

    const auto recalled = processor.getEffectiveMorphPoint();
    const float recalledRadius = processor.getEffectiveRadius();

    if (! near (recalled.x, 0.23f)
        || ! near (recalled.y, 0.71f)
        || ! near (recalledRadius, 0.57f)
        || processor.getActiveWaypoint() != 0)
    {
        std::cerr << "FAIL: waypoint did not restore X/Y/radius\n";
        return 3;
    }

    juce::MidiBuffer waypointMidi;
    waypointMidi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
    juce::AudioBuffer<float> silence (1, 512);
    silence.clear();
    processor.processBlock (silence, waypointMidi);

    const auto midiRecalled = processor.getEffectiveMorphPoint();

    if (! near (midiRecalled.x, 0.23f)
        || ! near (midiRecalled.y, 0.71f)
        || ! near (processor.getEffectiveRadius(), 0.57f))
    {
        std::cerr << "FAIL: MIDI waypoint recall did not restore X/Y/radius\n";
        return 4;
    }

    std::cout << "PASS: audible morph relative difference=" << relativeDifference
              << ", 200% loudness delta=" << loudnessDeltaDb
              << " dB, waypoint and MIDI recall OK\n";
    return 0;
}
