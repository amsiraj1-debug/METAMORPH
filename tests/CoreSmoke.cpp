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
        const auto dnniTestFile = tempDir.getNonexistentChildFile ("metamorph-dnni-smoke", ".dnni");

        if (auto output = dnniTestFile.createOutputStream())
        {
            const unsigned char header[8] { 0xff, 0x00, 0xca, 0x7f, 0x02, 0x00, 0x00, 0x00 };
            output->write (header, 8);
            output->setPosition (1024 * 1024 + 64);
            output->writeByte (0);
            output->flush();
        }

        juce::String dnniError;
        if (! processor.loadDnniModel (dnniTestFile, dnniError))
        {
            std::cerr << "FAIL: valid DNnI container signature was rejected: "
                      << dnniError << "\n";
            dnniTestFile.deleteFile();
            return 8;
        }

        if (processor.isDnniReady())
        {
            std::cerr << "FAIL: test unexpectedly found a DNNI runtime bridge\n";
            dnniTestFile.deleteFile();
            return 9;
        }

        if (! processor.getDnniStatus().containsIgnoreCase ("runtime"))
        {
            std::cerr << "FAIL: DNnI fallback status did not explain runtime requirement\n";
            dnniTestFile.deleteFile();
            return 10;
        }

        processor.clearDnniModel();
        dnniTestFile.deleteFile();
    }

    processor.setMorphPointFromUI (profiles.front().position.x, profiles.front().position.y);
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
