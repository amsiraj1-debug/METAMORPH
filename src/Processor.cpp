#include "Processor.h"
#include "Editor.h"
#include <algorithm>
#include <limits>
#include <numeric>

namespace
{
constexpr std::array<double, VoiceProfile::bandCount> bandCentres {
    90.0, 160.0, 280.0, 500.0, 900.0, 1600.0, 2900.0, 5200.0, 9000.0, 14500.0
};

constexpr std::array<double, VoiceProfile::bandCount + 1> bandEdges {
    55.0, 120.0, 210.0, 370.0, 660.0, 1180.0, 2100.0, 3750.0, 6650.0, 11200.0, 19000.0
};

juce::String colourToHex (juce::Colour c)
{
    return juce::String::formatted ("#%02X%02X%02X", c.getRed(), c.getGreen(), c.getBlue());
}

juce::Colour colourFromHex (juce::String text)
{
    text = text.trim().removeCharacters ("#");
    if (text.length() != 6)
        return juce::Colours::cornflowerblue;

    const auto rgb = (juce::uint32) text.getHexValue32();
    return juce::Colour::fromRGB ((juce::uint8) ((rgb >> 16) & 0xff),
                                  (juce::uint8) ((rgb >> 8) & 0xff),
                                  (juce::uint8) (rgb & 0xff));
}
}

MorphProcessor::MorphProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "METAMORPH_STATE", layout())
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < 8; ++i)
    {
        waypointX[(size_t) i].store (0.5f);
        waypointY[(size_t) i].store (0.5f);
        waypointRadius[(size_t) i].store (0.34f);
    }
}

MorphProcessor::APVTS::ParameterLayout MorphProcessor::layout()
{
    APVTS::ParameterLayout out;

    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pregain", 1 }, "Pre Gain",
                                                           juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output Gain",
                                                           juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Dry Wet",
                                                           juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pitch", 1 }, "Pitch Shift",
                                                           juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "morphX", 1 }, "Morph X",
                                                           juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "morphY", 1 }, "Morph Y",
                                                           juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "radius", 1 }, "Influence Radius",
                                                           juce::NormalisableRange<float> (0.08f, 0.75f, 0.0001f), 0.34f));
    out.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "strength", 1 }, "Transform Strength",
                                                           juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 155.0f));
    out.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "realtime", 1 }, "Realtime Mode", true));
    out.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "quality", 1 }, "Quality",
                                                            juce::StringArray { "Lowest Latency", "Lower Latency", "Higher Quality", "Highest Quality" }, 1));
    out.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "inputMode", 1 }, "Stereo Input",
                                                            juce::StringArray { "Stereo / Auto", "Left", "Right" }, 0));
    out.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return out;
}

juce::AudioProcessorParameter* MorphProcessor::getBypassParameter() const
{
    return parameters.getParameter ("bypass");
}

void MorphProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto channels = juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) channels };

    for (int i = 0; i < VoiceProfile::bandCount; ++i)
    {
        bandFilters[(size_t) i].prepare (spec);
        bandFilters[(size_t) i].reset();
        smoothedBandDb[(size_t) i].reset (sampleRate, 0.06);
        smoothedBandDb[(size_t) i].setCurrentAndTargetValue (0.0f);
    }

    bodyShelf.prepare (spec);
    presenceShelf.prepare (spec);
    airShelf.prepare (spec);
    bodyShelf.reset();
    presenceShelf.reset();
    airShelf.reset();

    pitchShifter.prepare (sampleRate, samplesPerBlock, channels);
    dryBuffer.setSize (channels, samplesPerBlock, false, false, true);

    liveAnalysisRing.fill (0.0f);
    liveFftData.fill (0.0f);
    liveSourceBandDb.fill (0.0f);
    liveAnalysisWrite = 0;
    liveAnalysisFill = 0;

    loudnessCompensation.reset (sampleRate, 0.08);
    loudnessCompensation.setCurrentAndTargetValue (1.0f);
}

bool MorphProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return output == layouts.getMainInputChannelSet();
}

float MorphProcessor::computePeak (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    return peak;
}

float MorphProcessor::computeRms (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return 0.0f;

    double sumSquares = 0.0;
    const double count = (double) buffer.getNumChannels() * (double) buffer.getNumSamples();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* samples = buffer.getReadPointer (ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            sumSquares += (double) samples[s] * (double) samples[s];
    }

    return (float) std::sqrt (sumSquares / juce::jmax (1.0, count));
}

void MorphProcessor::updateLiveSourceSpectrum (const juce::AudioBuffer<float>& source)
{
    const int channels = juce::jmax (1, source.getNumChannels());

    for (int s = 0; s < source.getNumSamples(); ++s)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            mono += source.getSample (ch, s);
        mono /= (float) channels;

        liveAnalysisRing[(size_t) liveAnalysisWrite] = mono;
        liveAnalysisWrite = (liveAnalysisWrite + 1) % liveFftSize;
        liveAnalysisFill = juce::jmin (liveFftSize, liveAnalysisFill + 1);
    }

    if (liveAnalysisFill < liveFftSize)
        return;

    liveFftData.fill (0.0f);

    for (int i = 0; i < liveFftSize; ++i)
    {
        const int index = (liveAnalysisWrite + i) % liveFftSize;
        liveFftData[(size_t) i] = liveAnalysisRing[(size_t) index];
    }

    liveWindow.multiplyWithWindowingTable (liveFftData.data(), liveFftSize);
    liveFft.performFrequencyOnlyForwardTransform (liveFftData.data());

    std::array<float, VoiceProfile::bandCount> currentDb {};
    float meanDb = 0.0f;

    for (int band = 0; band < VoiceProfile::bandCount; ++band)
    {
        const int lo = juce::jlimit (1, liveFftSize / 2 - 1,
            (int) std::floor (bandEdges[(size_t) band] * liveFftSize / currentSampleRate));
        const int hi = juce::jlimit (lo + 1, liveFftSize / 2,
            (int) std::ceil (bandEdges[(size_t) band + 1] * liveFftSize / currentSampleRate));

        double energy = 0.0;
        for (int bin = lo; bin < hi; ++bin)
        {
            const double magnitude = liveFftData[(size_t) bin];
            energy += magnitude * magnitude;
        }

        currentDb[(size_t) band] =
            (float) (10.0 * std::log10 (energy / (double) juce::jmax (1, hi - lo) + 1.0e-12));
        meanDb += currentDb[(size_t) band];
    }

    meanDb /= (float) VoiceProfile::bandCount;

    for (int band = 0; band < VoiceProfile::bandCount; ++band)
    {
        const float centred = juce::jlimit (-18.0f, 18.0f, (currentDb[(size_t) band] - meanDb) * 0.90f);
        liveSourceBandDb[(size_t) band] =
            0.82f * liveSourceBandDb[(size_t) band] + 0.18f * centred;
    }
}

void MorphProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0 || numChannels == 0)
        return;

    inputMeter.store (computePeak (buffer));

    if (parameters.getRawParameterValue ("bypass")->load() > 0.5f)
    {
        outputMeter.store (inputMeter.load());
        return;
    }

    if (numChannels == 2)
    {
        const int inputMode = (int) parameters.getRawParameterValue ("inputMode")->load();
        if (inputMode == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
        else if (inputMode == 2)
            buffer.copyFrom (0, 0, buffer, 1, 0, numSamples);
    }

    if (dryBuffer.getNumChannels() != numChannels || dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize (numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    updateLiveSourceSpectrum (dryBuffer);

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            const int index = msg.getNoteNumber() - 36;
            if (index >= 0 && index < waypointCount.load())
            {
                midiMorphX.store (waypointX[(size_t) index].load());
                midiMorphY.store (waypointY[(size_t) index].load());
                midiRadius.store (waypointRadius[(size_t) index].load());
                midiMorphOverride.store (true);
                activeWaypoint.store (index);
            }
            else if (msg.getNoteNumber() == 44 && waypointCount.load() > 0)
            {
                midiMorphX.store (waypointX[0].load());
                midiMorphY.store (waypointY[0].load());
                midiRadius.store (waypointRadius[0].load());
                midiMorphOverride.store (true);
                activeWaypoint.store (0);
            }
            else if (msg.getNoteNumber() == 45 && waypointCount.load() > 0)
            {
                const int next = (activeWaypoint.load() + 1 + waypointCount.load()) % waypointCount.load();
                midiMorphX.store (waypointX[(size_t) next].load());
                midiMorphY.store (waypointY[(size_t) next].load());
                midiRadius.store (waypointRadius[(size_t) next].load());
                midiMorphOverride.store (true);
                activeWaypoint.store (next);
            }
            else if (msg.getNoteNumber() == 46 && waypointCount.load() > 0)
            {
                int previous = activeWaypoint.load() - 1;
                if (previous < 0) previous = waypointCount.load() - 1;
                midiMorphX.store (waypointX[(size_t) previous].load());
                midiMorphY.store (waypointY[(size_t) previous].load());
                midiRadius.store (waypointRadius[(size_t) previous].load());
                midiMorphOverride.store (true);
                activeWaypoint.store (previous);
            }
        }
        else if (msg.isController())
        {
            const float value = (float) msg.getControllerValue() / 127.0f;
            if (msg.getControllerNumber() == 20) { midiMorphX.store (value); midiMorphOverride.store (true); }
            if (msg.getControllerNumber() == 21) { midiMorphY.store (value); midiMorphOverride.store (true); }
            if (msg.getControllerNumber() == 22) { midiRadius.store (juce::jmap (value, 0.08f, 0.75f)); }
        }
    }

    buffer.applyGain (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("pregain")->load()));

    pitchShifter.setSemitones (parameters.getRawParameterValue ("pitch")->load());
    pitchShifter.process (buffer);

    const float x = midiMorphOverride.load() ? midiMorphX.load() : parameters.getRawParameterValue ("morphX")->load();
    const float y = midiMorphOverride.load() ? midiMorphY.load() : parameters.getRawParameterValue ("morphY")->load();
    const float radius = midiMorphOverride.load() ? midiRadius.load() : parameters.getRawParameterValue ("radius")->load();

    const auto targetProfileDb = computeMorphBandGains (x, y, radius);
    const float strength = parameters.getRawParameterValue ("strength")->load() * 0.01f;

    std::array<float, VoiceProfile::bandCount> matchBandDb {};
    for (int band = 0; band < VoiceProfile::bandCount; ++band)
    {
        const float spectralDifference = targetProfileDb[(size_t) band] - liveSourceBandDb[(size_t) band];
        matchBandDb[(size_t) band] = juce::jlimit (-18.0f, 18.0f, spectralDifference * strength);
    }

    float correctionMean = 0.0f;
    for (const auto value : matchBandDb)
        correctionMean += value;
    correctionMean /= (float) VoiceProfile::bandCount;

    for (auto& value : matchBandDb)
        value -= correctionMean;

    updateFilterTargets (matchBandDb);

    const float bodyDb = juce::jlimit (-9.0f, 9.0f,
        (matchBandDb[0] + matchBandDb[1] + matchBandDb[2]) / 3.0f * 0.48f);
    const float presenceDb = juce::jlimit (-9.0f, 9.0f,
        (matchBandDb[5] + matchBandDb[6] + matchBandDb[7]) / 3.0f * 0.48f);
    const float airDb = juce::jlimit (-7.0f, 7.0f,
        (matchBandDb[8] + matchBandDb[9]) * 0.22f);

    *bodyShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        currentSampleRate, 220.0, 0.72, juce::Decibels::decibelsToGain (bodyDb));
    *presenceShelf.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate, juce::jmin (2400.0, currentSampleRate * 0.40), 0.72,
        juce::Decibels::decibelsToGain (presenceDb));
    *airShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        currentSampleRate, juce::jmin (7000.0, currentSampleRate * 0.40), 0.72,
        juce::Decibels::decibelsToGain (airDb));

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    for (auto& filter : bandFilters)
        filter.process (context);
    bodyShelf.process (context);
    presenceShelf.process (context);
    airShelf.process (context);

    const float preGainLinear =
        juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("pregain")->load());
    const float targetRms = computeRms (dryBuffer) * preGainLinear;
    const float wetRms = computeRms (buffer);

    float compensation = 1.0f;
    if (targetRms > 1.0e-5f && wetRms > 1.0e-5f)
        compensation = juce::jlimit (0.45f, 2.20f, targetRms / wetRms);

    loudnessCompensation.setTargetValue (compensation);
    const float gainStart = loudnessCompensation.getCurrentValue();
    const float gainEnd = loudnessCompensation.skip (numSamples);
    buffer.applyGainRamp (0, numSamples, gainStart, gainEnd);

    const float wet = parameters.getRawParameterValue ("mix")->load() * 0.01f;
    const float dry = 1.0f - wet;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = buffer.getWritePointer (ch);
        const auto* in = dryBuffer.getReadPointer (ch);

        for (int s = 0; s < numSamples; ++s)
            out[s] = out[s] * wet + in[s] * dry;
    }

    buffer.applyGain (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("output")->load()));
    outputMeter.store (computePeak (buffer));
}

std::array<float, VoiceProfile::bandCount> MorphProcessor::computeMorphBandGains (float x, float y, float radius) const
{
    std::array<float, VoiceProfile::bandCount> result {};
    const juce::ScopedLock lock (profilesLock);

    if (profiles.empty())
        return result;

    std::vector<float> weights;
    weights.reserve (profiles.size());
    float absSum = 0.0f;

    for (const auto& profile : profiles)
    {
        const float dx = profile.position.x - x;
        const float dy = profile.position.y - y;
        const float d = std::sqrt (dx * dx + dy * dy);
        float w = 0.0f;

        if (d <= radius)
        {
            const float n = 1.0f - d / juce::jmax (0.001f, radius);
            w = n * n;
        }
        else if (d < radius * 2.15f)
        {
            const float n = (d - radius) / juce::jmax (0.001f, radius * 1.15f);
            w = -0.22f * juce::jlimit (0.0f, 1.0f, n);
        }

        weights.push_back (w);
        absSum += std::abs (w);
    }

    if (absSum < 0.0001f)
    {
        size_t closest = 0;
        float closestDistance = std::numeric_limits<float>::max();

        for (size_t i = 0; i < profiles.size(); ++i)
        {
            const auto d = profiles[i].position.getDistanceFrom (juce::Point<float> (x, y));
            if (d < closestDistance)
            {
                closestDistance = d;
                closest = i;
            }
        }

        weights.assign (profiles.size(), 0.0f);
        weights[closest] = 1.0f;
        absSum = 1.0f;
    }

    for (size_t i = 0; i < profiles.size(); ++i)
        for (int band = 0; band < VoiceProfile::bandCount; ++band)
            result[(size_t) band] += (weights[i] / absSum) * profiles[i].bandDb[(size_t) band];

    for (auto& value : result)
        value = juce::jlimit (-18.0f, 18.0f, value);

    return result;
}

void MorphProcessor::updateFilterTargets (const std::array<float, VoiceProfile::bandCount>& targetDb)
{
    const bool realtime = parameters.getRawParameterValue ("realtime")->load() > 0.5f;
    const int quality = (int) parameters.getRawParameterValue ("quality")->load();
    const float depth = realtime ? (0.82f + 0.06f * (float) quality) : 1.0f;

    for (int i = 0; i < VoiceProfile::bandCount; ++i)
    {
        auto& smoother = smoothedBandDb[(size_t) i];
        smoother.setTargetValue (targetDb[(size_t) i] * depth);
        const float db = smoother.skip (64);
        const double frequency = juce::jmin (bandCentres[(size_t) i], currentSampleRate * 0.45);

        auto coeff = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            currentSampleRate, frequency, 0.82, juce::Decibels::decibelsToGain (db));

        *bandFilters[(size_t) i].state = *coeff;
    }
}

std::optional<VoiceProfile> MorphProcessor::analyseVoiceFile (const juce::File& file, juce::String& errorMessage)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        errorMessage = "Unsupported audio file. Use WAV, AIFF or FLAC.";
        return std::nullopt;
    }

    const juce::int64 maximumSamples = (juce::int64) (reader->sampleRate * 60.0);
    const int samplesToRead = (int) juce::jmin (reader->lengthInSamples, maximumSamples);

    if (samplesToRead < 4096)
    {
        errorMessage = "The reference recording is too short.";
        return std::nullopt;
    }

    const int sourceChannels = juce::jmax (1, (int) reader->numChannels);
    juce::AudioBuffer<float> source (sourceChannels, samplesToRead);
    reader->read (&source, 0, samplesToRead, 0, true, true);

    juce::AudioBuffer<float> mono (1, samplesToRead);
    mono.clear();

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
        mono.addFrom (0, 0, source, ch, 0, samplesToRead, 1.0f / (float) source.getNumChannels());

    constexpr int fftOrder = 11;
    constexpr int fftSize = 1 << fftOrder;
    constexpr int hop = fftSize / 2;

    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, true);
    std::vector<float> data ((size_t) fftSize * 2, 0.0f);
    std::array<double, VoiceProfile::bandCount> energies {};
    std::vector<float> pitches;
    int frameCount = 0;

    const float* samples = mono.getReadPointer (0);

    for (int start = 0; start + fftSize < samplesToRead; start += hop)
    {
        const float rms = mono.getRMSLevel (0, start, fftSize);
        if (rms < 0.002f)
            continue;

        std::fill (data.begin(), data.end(), 0.0f);
        std::copy (samples + start, samples + start + fftSize, data.begin());
        window.multiplyWithWindowingTable (data.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (data.data());

        for (int band = 0; band < VoiceProfile::bandCount; ++band)
        {
            const int lo = juce::jlimit (1, fftSize / 2 - 1,
                (int) std::floor (bandEdges[(size_t) band] * fftSize / reader->sampleRate));
            const int hi = juce::jlimit (lo + 1, fftSize / 2,
                (int) std::ceil (bandEdges[(size_t) band + 1] * fftSize / reader->sampleRate));

            double energy = 0.0;
            for (int bin = lo; bin < hi; ++bin)
                energy += (double) data[(size_t) bin] * (double) data[(size_t) bin];

            energies[(size_t) band] += energy / (double) juce::jmax (1, hi - lo);
        }

        int positiveCrossings = 0;
        float previous = samples[start];

        for (int n = start + 1; n < start + fftSize; ++n)
        {
            const float current = samples[n];
            if (previous <= 0.0f && current > 0.0f)
                ++positiveCrossings;
            previous = current;
        }

        const float roughPitch = (float) positiveCrossings * (float) reader->sampleRate / (float) fftSize;
        if (roughPitch >= 55.0f && roughPitch <= 700.0f)
            pitches.push_back (roughPitch);

        ++frameCount;
    }

    if (frameCount == 0)
    {
        errorMessage = "No usable vocal frames were detected in the reference.";
        return std::nullopt;
    }

    VoiceProfile profile;
    profile.name = file.getFileNameWithoutExtension();
    profile.sourcePath = file.getFullPathName();

    std::array<float, VoiceProfile::bandCount> db {};
    float meanDb = 0.0f;

    for (int i = 0; i < VoiceProfile::bandCount; ++i)
    {
        const double avg = energies[(size_t) i] / (double) frameCount;
        db[(size_t) i] = (float) (10.0 * std::log10 (avg + 1.0e-12));
        meanDb += db[(size_t) i];
    }

    meanDb /= (float) VoiceProfile::bandCount;

    for (int i = 0; i < VoiceProfile::bandCount; ++i)
        profile.bandDb[(size_t) i] =
            juce::jlimit (-18.0f, 18.0f, (db[(size_t) i] - meanDb) * 0.90f);

    if (! pitches.empty())
    {
        std::sort (pitches.begin(), pitches.end());
        profile.pitchLowHz = pitches[(size_t) ((pitches.size() - 1) * 0.10)];
        profile.pitchHighHz = pitches[(size_t) ((pitches.size() - 1) * 0.90)];
    }

    const juce::int64 hash = file.getFullPathName().hashCode64();
    const float hue = (float) ((juce::uint64) hash % 1000) / 1000.0f;
    profile.colour = juce::Colour::fromHSV (hue, 0.62f, 0.92f, 1.0f);
    profile.hexCode = colourToHex (profile.colour);

    float low = 0.0f;
    float high = 0.0f;
    for (int i = 0; i < 4; ++i) low += profile.bandDb[(size_t) i];
    for (int i = 6; i < VoiceProfile::bandCount; ++i) high += profile.bandDb[(size_t) i];
    low *= 0.25f;
    high *= 0.25f;

    const float jitterX = ((float) (((juce::uint64) hash >> 12) & 255) / 255.0f - 0.5f) * 0.12f;
    const float jitterY = ((float) (((juce::uint64) hash >> 20) & 255) / 255.0f - 0.5f) * 0.12f;

    profile.position = {
        juce::jlimit (0.08f, 0.92f, 0.50f + high / 28.0f + jitterX),
        juce::jlimit (0.08f, 0.92f, 0.50f - low / 28.0f + jitterY)
    };

    return profile;
}

bool MorphProcessor::addVoiceFromFile (const juce::File& file, juce::String& errorMessage)
{
    auto analysed = analyseVoiceFile (file, errorMessage);

    if (! analysed.has_value())
        return false;

    const juce::ScopedLock lock (profilesLock);

    if (profiles.size() >= 16)
    {
        errorMessage = "This build supports up to 16 simultaneous target voices.";
        return false;
    }

    profiles.push_back (*analysed);
    return true;
}

void MorphProcessor::addGeneratedVoice (juce::String hexCode, float voiceSpace, float tone, juce::String customName)
{
    const auto colour = colourFromHex (hexCode);
    juce::Random random ((juce::int64) hexCode.hashCode64()
                         ^ ((juce::int64) (voiceSpace * 1000.0f) << 12)
                         ^ (juce::int64) (tone * 1000.0f));

    VoiceProfile p;
    p.generated = true;
    p.colour = colour;
    p.hexCode = colourToHex (colour);
    p.name = customName.isNotEmpty() ? customName : "Generated " + p.hexCode;
    p.position = {
        juce::jlimit (0.08f, 0.92f, voiceSpace),
        juce::jlimit (0.08f, 0.92f, 1.0f - tone)
    };
    p.pitchLowHz = juce::jmap (voiceSpace, 0.0f, 1.0f, 80.0f, 145.0f);
    p.pitchHighHz = juce::jmap (voiceSpace, 0.0f, 1.0f, 210.0f, 410.0f);

    for (int i = 0; i < VoiceProfile::bandCount; ++i)
    {
        const float spectralTilt = juce::jmap (voiceSpace, 0.0f, 1.0f, -1.0f, 1.0f)
                                 * juce::jmap ((float) i, 0.0f, 9.0f, -4.0f, 4.0f);
        const float toneCurve = std::sin ((float) i * 0.78f
                                        + tone * juce::MathConstants<float>::twoPi) * 2.8f;
        const float randomShape = (random.nextFloat() - 0.5f) * 4.5f;
        p.bandDb[(size_t) i] = juce::jlimit (-16.0f, 16.0f,
            spectralTilt * 1.75f + toneCurve * 1.45f + randomShape * 1.25f);
    }

    const juce::ScopedLock lock (profilesLock);
    if (profiles.size() < 16)
        profiles.push_back (p);
}

void MorphProcessor::addSyntheticPreset (int presetIndex)
{
    presetIndex = juce::jlimit (0, 39, presetIndex);
    const float hue = (float) presetIndex / 40.0f;
    const auto colour = juce::Colour::fromHSV (hue, 0.68f, 0.92f, 1.0f);
    const float voiceSpace = 0.12f + 0.76f * ((float) ((presetIndex * 17) % 41) / 40.0f);
    const float tone = 0.10f + 0.80f * ((float) ((presetIndex * 29 + 7) % 41) / 40.0f);

    addGeneratedVoice (colourToHex (colour), voiceSpace, tone,
                       "Synthetic " + juce::String (presetIndex + 1).paddedLeft ('0', 2));
}

void MorphProcessor::removeVoice (int index)
{
    const juce::ScopedLock lock (profilesLock);
    if (juce::isPositiveAndBelow (index, (int) profiles.size()))
        profiles.erase (profiles.begin() + index);
}

void MorphProcessor::clearVoices()
{
    const juce::ScopedLock lock (profilesLock);
    profiles.clear();
}

std::vector<VoiceProfile> MorphProcessor::getProfilesSnapshot() const
{
    const juce::ScopedLock lock (profilesLock);
    return profiles;
}

void MorphProcessor::applyParameterValue (const juce::String& id, float plainValue)
{
    if (auto* p = parameters.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void MorphProcessor::setMorphPointFromUI (float x, float y)
{
    midiMorphOverride.store (false);
    activeWaypoint.store (-1);
    applyParameterValue ("morphX", juce::jlimit (0.0f, 1.0f, x));
    applyParameterValue ("morphY", juce::jlimit (0.0f, 1.0f, y));
}

void MorphProcessor::setInfluenceFromUI (float radius)
{
    midiMorphOverride.store (false);
    applyParameterValue ("radius", juce::jlimit (0.08f, 0.75f, radius));
}

void MorphProcessor::addWaypointFromCurrent()
{
    const int count = waypointCount.load();
    if (count >= 8)
        return;

    waypointX[(size_t) count].store (parameters.getRawParameterValue ("morphX")->load());
    waypointY[(size_t) count].store (parameters.getRawParameterValue ("morphY")->load());
    waypointRadius[(size_t) count].store (parameters.getRawParameterValue ("radius")->load());
    waypointCount.store (count + 1);
    activeWaypoint.store (count);
}

void MorphProcessor::clearWaypoints()
{
    waypointCount.store (0);
    activeWaypoint.store (-1);
}

juce::Point<float> MorphProcessor::getWaypoint (int index) const
{
    if (! juce::isPositiveAndBelow (index, waypointCount.load()))
        return { 0.5f, 0.5f };

    return { waypointX[(size_t) index].load(), waypointY[(size_t) index].load() };
}

float MorphProcessor::getWaypointRadius (int index) const
{
    if (! juce::isPositiveAndBelow (index, waypointCount.load()))
        return 0.34f;

    return waypointRadius[(size_t) index].load();
}

juce::Point<float> MorphProcessor::getEffectiveMorphPoint() const
{
    if (midiMorphOverride.load())
        return { midiMorphX.load(), midiMorphY.load() };

    return {
        parameters.getRawParameterValue ("morphX")->load(),
        parameters.getRawParameterValue ("morphY")->load()
    };
}

float MorphProcessor::getEffectiveRadius() const
{
    return midiMorphOverride.load()
        ? midiRadius.load()
        : parameters.getRawParameterValue ("radius")->load();
}

void MorphProcessor::activateWaypoint (int index)
{
    if (! juce::isPositiveAndBelow (index, waypointCount.load()))
        return;

    const auto p = getWaypoint (index);
    setMorphPointFromUI (p.x, p.y);
    setInfluenceFromUI (getWaypointRadius (index));
    activeWaypoint.store (index);
}

void MorphProcessor::activateNextWaypoint()
{
    const int count = waypointCount.load();
    if (count == 0)
        return;

    activateWaypoint ((activeWaypoint.load() + 1 + count) % count);
}

void MorphProcessor::activatePreviousWaypoint()
{
    const int count = waypointCount.load();
    if (count == 0)
        return;

    int index = activeWaypoint.load() - 1;
    if (index < 0)
        index = count - 1;

    activateWaypoint (index);
}

void MorphProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    juce::ValueTree voicesTree { "VOICES" };
    {
        const juce::ScopedLock lock (profilesLock);
        for (const auto& profile : profiles)
            voicesTree.addChild (profile.toValueTree(), -1, nullptr);
    }
    state.addChild (voicesTree, -1, nullptr);

    juce::ValueTree waypointsTree { "WAYPOINTS" };
    for (int i = 0; i < waypointCount.load(); ++i)
    {
        juce::ValueTree point { "POINT" };
        point.setProperty ("x", waypointX[(size_t) i].load(), nullptr);
        point.setProperty ("y", waypointY[(size_t) i].load(), nullptr);
        point.setProperty ("radius", waypointRadius[(size_t) i].load(), nullptr);
        waypointsTree.addChild (point, -1, nullptr);
    }
    waypointsTree.setProperty ("active", activeWaypoint.load(), nullptr);
    state.addChild (waypointsTree, -1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MorphProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    auto voicesTree = tree.getChildWithName ("VOICES");
    auto waypointsTree = tree.getChildWithName ("WAYPOINTS");

    if (voicesTree.isValid())
        tree.removeChild (voicesTree, nullptr);
    if (waypointsTree.isValid())
        tree.removeChild (waypointsTree, nullptr);

    parameters.replaceState (tree);

    if (voicesTree.isValid())
    {
        const juce::ScopedLock lock (profilesLock);
        profiles.clear();

        for (int i = 0; i < voicesTree.getNumChildren(); ++i)
            if (auto profile = VoiceProfile::fromValueTree (voicesTree.getChild (i)); profile.has_value())
                profiles.push_back (*profile);
    }

    waypointCount.store (0);

    if (waypointsTree.isValid())
    {
        const int count = juce::jmin (8, waypointsTree.getNumChildren());

        for (int i = 0; i < count; ++i)
        {
            const auto point = waypointsTree.getChild (i);
            waypointX[(size_t) i].store ((float) point.getProperty ("x", 0.5));
            waypointY[(size_t) i].store ((float) point.getProperty ("y", 0.5));
            waypointRadius[(size_t) i].store ((float) point.getProperty ("radius", 0.34));
        }

        waypointCount.store (count);
        activeWaypoint.store ((int) waypointsTree.getProperty ("active", -1));
    }
}

juce::AudioProcessorEditor* MorphProcessor::createEditor()
{
    return new MorphEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MorphProcessor();
}
