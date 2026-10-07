#pragma once
#include <JuceHeader.h>

class DualDelayPitchShifter
{
public:
    void prepare (double newSampleRate, int maximumBlockSize, int numChannels)
    {
        sampleRate = newSampleRate;
        channels = juce::jmax (1, numChannels);
        const int required = (int) std::ceil (sampleRate * 0.18) + maximumBlockSize + 8;
        delayBuffer.setSize (channels, required);
        delayBuffer.clear();
        writePosition = 0;
        phase = 0.0;
    }

    void reset()
    {
        delayBuffer.clear();
        writePosition = 0;
        phase = 0.0;
    }

    void setSemitones (float newSemitones)
    {
        semitones = juce::jlimit (-12.0f, 12.0f, newSemitones);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (std::abs (semitones) < 0.001f || delayBuffer.getNumSamples() == 0)
            return;

        const double ratio = std::pow (2.0, (double) semitones / 12.0);
        const double delta = ratio - 1.0;
        const double minDelay = juce::jmax (8.0, sampleRate * 0.004);
        const double range = juce::jmax (64.0, sampleRate * 0.040);
        const double phaseIncrement = juce::jmax (1.0e-7, std::abs (delta) / range);
        const int bufferLength = delayBuffer.getNumSamples();
        const int processChannels = juce::jmin (buffer.getNumChannels(), channels);

        auto readInterpolated = [this, bufferLength] (int channel, double delaySamples)
        {
            double read = (double) writePosition - delaySamples;
            while (read < 0.0) read += bufferLength;
            while (read >= bufferLength) read -= bufferLength;

            const int i0 = (int) std::floor (read);
            const int i1 = (i0 + 1) % bufferLength;
            const float frac = (float) (read - (double) i0);
            const float a = delayBuffer.getSample (channel, i0);
            const float b = delayBuffer.getSample (channel, i1);
            return a + frac * (b - a);
        };

        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            for (int ch = 0; ch < processChannels; ++ch)
                delayBuffer.setSample (ch, writePosition, buffer.getSample (ch, s));

            const double p1 = phase;
            double p2 = phase + 0.5;
            if (p2 >= 1.0) p2 -= 1.0;

            auto delayForPhase = [delta, minDelay, range] (double p)
            {
                return delta >= 0.0 ? minDelay + (1.0 - p) * range
                                    : minDelay + p * range;
            };

            const double d1 = delayForPhase (p1);
            const double d2 = delayForPhase (p2);
            const float w1 = (float) std::pow (std::sin (juce::MathConstants<double>::pi * p1), 2.0);
            const float w2 = (float) std::pow (std::sin (juce::MathConstants<double>::pi * p2), 2.0);
            const float norm = 1.0f / juce::jmax (0.001f, w1 + w2);

            for (int ch = 0; ch < processChannels; ++ch)
            {
                const float y = (readInterpolated (ch, d1) * w1 + readInterpolated (ch, d2) * w2) * norm;
                buffer.setSample (ch, s, y);
            }

            writePosition = (writePosition + 1) % bufferLength;
            phase += phaseIncrement;
            while (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    juce::AudioBuffer<float> delayBuffer;
    double sampleRate { 44100.0 };
    int channels { 2 };
    int writePosition { 0 };
    double phase { 0.0 };
    float semitones { 0.0f };
};
