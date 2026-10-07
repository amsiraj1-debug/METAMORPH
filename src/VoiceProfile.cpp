#include "VoiceProfile.h"

juce::ValueTree VoiceProfile::toValueTree() const
{
    juce::ValueTree t { "VOICE" };
    t.setProperty ("name", name, nullptr);
    t.setProperty ("sourcePath", sourcePath, nullptr);
    t.setProperty ("hexCode", hexCode, nullptr);
    t.setProperty ("colour", colour.toString(), nullptr);
    t.setProperty ("x", position.x, nullptr);
    t.setProperty ("y", position.y, nullptr);
    t.setProperty ("pitchLowHz", pitchLowHz, nullptr);
    t.setProperty ("pitchHighHz", pitchHighHz, nullptr);
    t.setProperty ("generated", generated, nullptr);

    for (int i = 0; i < bandCount; ++i)
        t.setProperty ("b" + juce::String (i), bandDb[(size_t) i], nullptr);

    return t;
}

std::optional<VoiceProfile> VoiceProfile::fromValueTree (const juce::ValueTree& t)
{
    if (! t.isValid() || ! t.hasType ("VOICE"))
        return std::nullopt;

    VoiceProfile p;
    p.name = t.getProperty ("name", "Voice").toString();
    p.sourcePath = t.getProperty ("sourcePath", "").toString();
    p.hexCode = t.getProperty ("hexCode", "#808080").toString();
    p.colour = juce::Colour::fromString (t.getProperty ("colour", "ff6495ed").toString());
    p.position.x = (float) t.getProperty ("x", 0.5);
    p.position.y = (float) t.getProperty ("y", 0.5);
    p.pitchLowHz = (float) t.getProperty ("pitchLowHz", 90.0);
    p.pitchHighHz = (float) t.getProperty ("pitchHighHz", 300.0);
    p.generated = (bool) t.getProperty ("generated", false);

    for (int i = 0; i < bandCount; ++i)
        p.bandDb[(size_t) i] = (float) t.getProperty ("b" + juce::String (i), 0.0);

    return p;
}
