#include "Editor.h"

namespace
{
constexpr auto panelColour = 0xff171b24;
constexpr auto canvasColour = 0xff10131a;
constexpr auto accentColour = 0xff78e7dc;
constexpr auto textColour = 0xffe8edf5;
constexpr auto mutedColour = 0xff7f8998;

juce::Rectangle<float> canvasInnerBounds (const juce::Component& c)
{
    return c.getLocalBounds().toFloat().reduced (22.0f);
}
}

juce::Point<float> MorphCanvas::toNormalised (juce::Point<float> p) const
{
    const auto b = canvasInnerBounds (*this);

    return {
        juce::jlimit (0.0f, 1.0f, (p.x - b.getX()) / juce::jmax (1.0f, b.getWidth())),
        juce::jlimit (0.0f, 1.0f, (p.y - b.getY()) / juce::jmax (1.0f, b.getHeight()))
    };
}

juce::Point<float> MorphCanvas::fromNormalised (juce::Point<float> p) const
{
    const auto b = canvasInnerBounds (*this);
    return { b.getX() + p.x * b.getWidth(), b.getY() + p.y * b.getHeight() };
}

void MorphCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (canvasColour));
    const auto bounds = canvasInnerBounds (*this);

    juce::ColourGradient gradient (
        juce::Colour (0xff1c2330), bounds.getTopLeft(),
        juce::Colour (0xff0e1117), bounds.getBottomRight(), false);

    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, 18.0f);

    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (int i = 1; i < 10; ++i)
    {
        const float x = bounds.getX() + bounds.getWidth() * (float) i / 10.0f;
        const float y = bounds.getY() + bounds.getHeight() * (float) i / 10.0f;
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
        g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
    }

    const auto profiles = processor.getProfilesSnapshot();
    const juce::Point<float> cursorNorm {
        processor.parameters.getRawParameterValue ("morphX")->load(),
        processor.parameters.getRawParameterValue ("morphY")->load()
    };

    const auto cursor = fromNormalised (cursorNorm);
    const float radiusNorm = processor.parameters.getRawParameterValue ("radius")->load();
    const float radiusPx = radiusNorm * juce::jmin (bounds.getWidth(), bounds.getHeight());

    for (const auto& profile : profiles)
    {
        const auto centre = fromNormalised (profile.position);
        const float distance = centre.getDistanceFrom (cursor);
        const float influence = juce::jlimit (0.0f, 1.0f, 1.0f - distance / juce::jmax (1.0f, radiusPx));

        if (distance < radiusPx * 2.15f)
        {
            g.setColour (profile.colour.withAlpha (0.12f + influence * 0.35f));
            g.drawLine (juce::Line<float> (centre, cursor), 1.0f + influence * 4.0f);
        }

        juce::Path curve;
        std::array<juce::Point<float>, VoiceProfile::bandCount> points {};

        for (int i = 0; i < VoiceProfile::bandCount; ++i)
        {
            const float dx = ((float) i - 4.5f) * 8.5f;
            const float timbreY = profile.bandDb[(size_t) i] * 1.05f;
            const float pitchArc = std::sin ((float) i / 9.0f * juce::MathConstants<float>::pi) * 8.0f;
            points[(size_t) i] = centre + juce::Point<float> (dx, timbreY - pitchArc);

            if (i == 0)
                curve.startNewSubPath (points[(size_t) i]);
            else
                curve.lineTo (points[(size_t) i]);
        }

        g.setColour (profile.colour.withAlpha (0.22f + influence * 0.22f));
        g.strokePath (curve, juce::PathStrokeType (2.0f + influence * 1.2f));

        for (int i = 0; i < VoiceProfile::bandCount; ++i)
        {
            const float size = (i % 3 == 0 ? 5.5f : 4.0f) + influence * 1.5f;
            g.setColour (profile.colour.withAlpha (0.65f + influence * 0.30f));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (points[(size_t) i]));
        }

        g.setColour (juce::Colour (textColour));
        g.setFont (12.5f);
        g.drawText (
            profile.name,
            juce::Rectangle<float> (centre.x - 70.0f, centre.y + 22.0f, 140.0f, 18.0f),
            juce::Justification::centred,
            true);

        const auto pitchText =
            juce::String ((int) std::round (profile.pitchLowHz))
            + "-"
            + juce::String ((int) std::round (profile.pitchHighHz))
            + " Hz";

        g.setColour (juce::Colour (mutedColour));
        g.setFont (10.5f);
        g.drawText (
            pitchText,
            juce::Rectangle<float> (centre.x - 55.0f, centre.y + 38.0f, 110.0f, 15.0f),
            juce::Justification::centred,
            false);
    }

    if (processor.getWaypointCount() > 1)
    {
        juce::Path route;
        route.startNewSubPath (fromNormalised (processor.getWaypoint (0)));

        for (int i = 1; i < processor.getWaypointCount(); ++i)
            route.lineTo (fromNormalised (processor.getWaypoint (i)));

        g.setColour (juce::Colour (accentColour).withAlpha (0.24f));
        g.strokePath (route, juce::PathStrokeType (1.4f));
    }

    for (int i = 0; i < processor.getWaypointCount(); ++i)
    {
        const auto p = fromNormalised (processor.getWaypoint (i));
        const bool active = i == processor.getActiveWaypoint();

        g.setColour (active
            ? juce::Colour (accentColour)
            : juce::Colours::white.withAlpha (0.35f));

        juce::Path diamond;
        diamond.startNewSubPath (p.x, p.y - 7.0f);
        diamond.lineTo (p.x + 7.0f, p.y);
        diamond.lineTo (p.x, p.y + 7.0f);
        diamond.lineTo (p.x - 7.0f, p.y);
        diamond.closeSubPath();
        g.fillPath (diamond);

        g.drawText (
            juce::String (i + 1),
            (int) p.x - 9,
            (int) p.y + 9,
            18,
            14,
            juce::Justification::centred,
            false);
    }

    g.setColour (juce::Colour (accentColour).withAlpha (0.09f));
    g.fillEllipse (juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f).withCentre (cursor));

    g.setColour (juce::Colour (accentColour).withAlpha (0.75f));
    g.drawEllipse (juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f).withCentre (cursor), 1.5f);

    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (cursor));

    g.setColour (juce::Colour (accentColour));
    g.drawEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (cursor), 2.2f);

    if (profiles.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.setFont (17.0f);
        g.drawFittedText (
            "Drop a clean vocal reference here\nor choose IMPORT VOICE",
            bounds.toNearestInt().reduced (100),
            juce::Justification::centred,
            2);
    }

    if (draggingFile)
    {
        g.setColour (juce::Colour (accentColour).withAlpha (0.10f));
        g.fillRoundedRectangle (bounds, 18.0f);
        g.setColour (juce::Colour (accentColour));
        g.drawRoundedRectangle (bounds, 18.0f, 2.0f);
    }
}

void MorphCanvas::updateCursorFromMouse (juce::Point<float> p)
{
    const auto n = toNormalised (p);
    processor.setMorphPointFromUI (n.x, n.y);
    repaint();
}

void MorphCanvas::mouseDown (const juce::MouseEvent& e)
{
    updateCursorFromMouse (e.position);
}

void MorphCanvas::mouseDrag (const juce::MouseEvent& e)
{
    updateCursorFromMouse (e.position);
}

void MorphCanvas::mouseWheelMove (
    const juce::MouseEvent&,
    const juce::MouseWheelDetails& wheel)
{
    const float current = processor.parameters.getRawParameterValue ("radius")->load();
    processor.setInfluenceFromUI (current + wheel.deltaY * 0.08f);
    repaint();
}

bool MorphCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const auto ext = juce::File (path).getFileExtension().toLowerCase();

        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac")
        {
            draggingFile = true;
            repaint();
            return true;
        }
    }

    return false;
}

void MorphCanvas::fileDragExit (const juce::StringArray&)
{
    draggingFile = false;
    repaint();
}

void MorphCanvas::filesDropped (const juce::StringArray& files, int, int)
{
    draggingFile = false;

    for (const auto& path : files)
    {
        juce::String error;

        if (! processor.addVoiceFromFile (juce::File (path), error))
        {
            if (onStatus)
                onStatus (error);
        }
        else if (onStatus)
        {
            onStatus ("Imported " + juce::File (path).getFileName());
        }
    }

    repaint();
}

MorphEditor::MorphEditor (MorphProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      morphCanvas (p)
{
    setSize (1120, 720);
    setResizable (true, true);
    setResizeLimits (900, 600, 1500, 1000);

    titleLabel.setText ("METAMORPH CR", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (textColour));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("real-time reference timbre morphing", juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (mutedColour));
    addAndMakeVisible (subtitleLabel);

    statusLabel.setText ("Ready", juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (mutedColour));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    latencyLabel.setColour (juce::Label::textColourId, juce::Colour (mutedColour));
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    waypointInfoLabel.setText ("WAYPOINTS  •  saved light positions  •  MIDI C2-G2", juce::dontSendNotification);
    waypointInfoLabel.setColour (juce::Label::textColourId, juce::Colour (mutedColour));
    waypointInfoLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    waypointInfoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (waypointInfoLabel);

    morphCanvas.onStatus = [this] (const juce::String& s)
    {
        showStatus (s);
        updateVoiceList();
    };
    addAndMakeVisible (morphCanvas);

    configureSlider (preGainSlider, " dB");
    configureSlider (pitchSlider, " st");
    configureSlider (outputSlider, " dB");
    configureSlider (mixSlider, " %");
    configureSlider (radiusSlider);
    configureSlider (strengthSlider, " %");
    configureSlider (voiceSpaceSlider);
    configureSlider (toneSlider);

    preGainSlider.setRange (-24.0, 24.0, 0.1);
    pitchSlider.setRange (-12.0, 12.0, 0.01);
    outputSlider.setRange (-24.0, 24.0, 0.1);
    mixSlider.setRange (0.0, 100.0, 0.1);
    radiusSlider.setRange (0.08, 0.75, 0.001);
    strengthSlider.setRange (0.0, 200.0, 0.1);
    voiceSpaceSlider.setRange (0.0, 1.0, 0.001);
    toneSlider.setRange (0.0, 1.0, 0.001);
    voiceSpaceSlider.setValue (0.5);
    toneSlider.setValue (0.5);

    for (auto* slider : {
        &preGainSlider,
        &pitchSlider,
        &outputSlider,
        &mixSlider,
        &radiusSlider,
        &strengthSlider,
        &voiceSpaceSlider,
        &toneSlider
    })
    {
        addAndMakeVisible (*slider);
    }

    realtimeButton.setColour (juce::ToggleButton::textColourId, juce::Colour (textColour));
    bypassButton.setColour (juce::ToggleButton::textColourId, juce::Colour (textColour));
    addAndMakeVisible (realtimeButton);
    addAndMakeVisible (bypassButton);

    qualityBox.addItemList (
        { "Lowest Latency", "Lower Latency", "Higher Quality", "Highest Quality" },
        1);

    inputModeBox.addItemList (
        { "Stereo / Auto", "Left only", "Right only" },
        1);

    addAndMakeVisible (qualityBox);
    addAndMakeVisible (inputModeBox);

    hexEditor.setText ("#57D8CC");
    hexEditor.setJustification (juce::Justification::centred);
    hexEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e1219));
    hexEditor.setColour (juce::TextEditor::textColourId, juce::Colour (textColour));
    hexEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.12f));
    addAndMakeVisible (hexEditor);

    presetBox.addItem ("Synthetic starter voices", 1000);
    presetBox.addSeparator();

    for (int i = 0; i < 40; ++i)
        presetBox.addItem ("Synthetic " + juce::String (i + 1).paddedLeft ('0', 2), i + 1);

    presetBox.setSelectedId (1000, juce::dontSendNotification);
    addAndMakeVisible (presetBox);
    addAndMakeVisible (voiceList);

    for (auto* button : {
        &importButton,
        &generateButton,
        &removeButton,
        &clearButton,
        &saveWaypointButton,
        &previousWaypointButton,
        &nextWaypointButton,
        &clearWaypointsButton
    })
    {
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252c38));
        button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (accentColour));
        button->setColour (juce::TextButton::textColourOffId, juce::Colour (textColour));
        addAndMakeVisible (*button);
    }

    for (int i = 0; i < (int) waypointButtons.size(); ++i)
    {
        waypointButtons[(size_t) i].setButtonText (juce::String (i + 1));
        waypointButtons[(size_t) i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff252c38));
        waypointButtons[(size_t) i].setColour (juce::TextButton::textColourOffId, juce::Colour (textColour));
        waypointButtons[(size_t) i].onClick = [this, i]
        {
            processor.activateWaypoint (i);
            morphCanvas.repaint();
            showStatus ("Waypoint " + juce::String (i + 1) + " recalled");
        };
        addAndMakeVisible (waypointButtons[(size_t) i]);
    }

    importButton.onClick = [this]
    {
        chooseReferenceFile();
    };

    generateButton.onClick = [this]
    {
        auto hex = hexEditor.getText().trim();

        if (! hex.startsWithChar ('#'))
            hex = "#" + hex;

        if (hex.length() != 7)
        {
            showStatus ("Hex voice code must look like #57D8CC");
            return;
        }

        processor.addGeneratedVoice (
            hex,
            (float) voiceSpaceSlider.getValue(),
            (float) toneSlider.getValue());

        showStatus ("Generated voice " + hex);
        updateVoiceList();
        morphCanvas.repaint();
    };

    removeButton.onClick = [this]
    {
        const int index = voiceList.getSelectedId() - 1;
        processor.removeVoice (index);
        updateVoiceList();
        morphCanvas.repaint();
    };

    clearButton.onClick = [this]
    {
        processor.clearVoices();
        updateVoiceList();
        morphCanvas.repaint();
        showStatus ("Target voices cleared");
    };

    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();

        if (id >= 1 && id <= 40)
        {
            processor.addSyntheticPreset (id - 1);
            updateVoiceList();
            morphCanvas.repaint();
            presetBox.setSelectedId (1000, juce::dontSendNotification);
        }
    };

    saveWaypointButton.onClick = [this]
    {
        if (processor.getWaypointCount() >= 8)
        {
            showStatus ("Maximum 8 waypoints reached");
            return;
        }

        processor.addWaypointFromCurrent();
        morphCanvas.repaint();
        showStatus ("Saved waypoint " + juce::String (processor.getWaypointCount())
                    + " (cursor position + influence radius)");
    };

    previousWaypointButton.onClick = [this]
    {
        processor.activatePreviousWaypoint();
        morphCanvas.repaint();
        showStatus ("Previous waypoint");
    };

    nextWaypointButton.onClick = [this]
    {
        processor.activateNextWaypoint();
        morphCanvas.repaint();
        showStatus ("Next waypoint");
    };

    clearWaypointsButton.onClick = [this]
    {
        processor.clearWaypoints();
        morphCanvas.repaint();
        showStatus ("Waypoints cleared");
    };

    preGainAttachment = std::make_unique<SliderAttachment> (processor.parameters, "pregain", preGainSlider);
    pitchAttachment = std::make_unique<SliderAttachment> (processor.parameters, "pitch", pitchSlider);
    outputAttachment = std::make_unique<SliderAttachment> (processor.parameters, "output", outputSlider);
    mixAttachment = std::make_unique<SliderAttachment> (processor.parameters, "mix", mixSlider);
    radiusAttachment = std::make_unique<SliderAttachment> (processor.parameters, "radius", radiusSlider);
    strengthAttachment = std::make_unique<SliderAttachment> (processor.parameters, "strength", strengthSlider);
    qualityAttachment = std::make_unique<ComboAttachment> (processor.parameters, "quality", qualityBox);
    inputModeAttachment = std::make_unique<ComboAttachment> (processor.parameters, "inputMode", inputModeBox);
    realtimeAttachment = std::make_unique<ButtonAttachment> (processor.parameters, "realtime", realtimeButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.parameters, "bypass", bypassButton);

    updateVoiceList();
    startTimerHz (15);
}

MorphEditor::~MorphEditor()
{
    stopTimer();
}

void MorphEditor::configureSlider (juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (accentColour));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.11f));
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (textColour));
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void MorphEditor::chooseReferenceFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Choose a clean vocal reference",
        juce::File {},
        "*.wav;*.aif;*.aiff;*.flac");

    const auto flags =
        juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles;

    const juce::Component::SafePointer<MorphEditor> safe (this);

    chooser->launchAsync (flags, [safe] (const juce::FileChooser& fc)
    {
        if (! safe)
            return;

        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        juce::String error;

        if (safe->processor.addVoiceFromFile (file, error))
            safe->showStatus ("Imported " + file.getFileName());
        else
            safe->showStatus (error);

        safe->updateVoiceList();
        safe->morphCanvas.repaint();
    });
}

void MorphEditor::showStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void MorphEditor::updateVoiceList()
{
    const auto voices = processor.getProfilesSnapshot();
    const int oldSelection = voiceList.getSelectedId();

    voiceList.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) voices.size(); ++i)
        voiceList.addItem (voices[(size_t) i].name, i + 1);

    if (! voices.empty())
    {
        voiceList.setSelectedId (
            juce::jlimit (1, (int) voices.size(), oldSelection > 0 ? oldSelection : 1),
            juce::dontSendNotification);
    }

    lastVoiceCount = (int) voices.size();
}

void MorphEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0e13));

    const auto r = getLocalBounds().toFloat();

    g.setColour (juce::Colour (panelColour));
    g.fillRect (juce::Rectangle<float> (0.0f, 64.0f, 194.0f, r.getHeight() - 96.0f));
    g.fillRect (juce::Rectangle<float> (r.getWidth() - 214.0f, 64.0f, 214.0f, r.getHeight() - 96.0f));

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (63, 0.0f, r.getWidth());
    g.drawHorizontalLine ((int) r.getHeight() - 32, 0.0f, r.getWidth());

    g.setColour (juce::Colour (mutedColour));
    g.setFont (11.0f);

    g.drawText ("PRE GAIN", 18, 83, 76, 18, juce::Justification::centred);
    g.drawText ("PITCH", 100, 83, 76, 18, juce::Justification::centred);
    g.drawText ("OUTPUT", 18, 208, 76, 18, juce::Justification::centred);
    g.drawText ("MIX", 100, 208, 76, 18, juce::Justification::centred);
    g.drawText ("INFLUENCE", 18, 333, 76, 18, juce::Justification::centred);
    g.drawText ("STRENGTH", 100, 333, 76, 18, juce::Justification::centred);

    const int rightX = getWidth() - 198;

    g.drawText ("GENERATED VOICE", rightX, 84, 180, 18, juce::Justification::centredLeft);
    g.drawText ("VOICE CODE", rightX, 112, 180, 18, juce::Justification::centredLeft);
    g.drawText ("VOICE SPACE", rightX, 170, 80, 18, juce::Justification::centred);
    g.drawText ("TONE", rightX + 92, 170, 80, 18, juce::Justification::centred);
    g.drawText ("TARGET VOICES", rightX, 330, 180, 18, juce::Justification::centredLeft);

    const float in = juce::jlimit (0.0f, 1.0f, processor.getInputMeter());
    const float out = juce::jlimit (0.0f, 1.0f, processor.getOutputMeter());

    const auto meterArea = juce::Rectangle<float> (
        (float) getWidth() - 156.0f,
        19.0f,
        138.0f,
        9.0f);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (meterArea, 4.0f);
    g.fillRoundedRectangle (meterArea.translated (0.0f, 14.0f), 4.0f);

    g.setColour (juce::Colour (accentColour));
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * in), 4.0f);

    g.setColour (juce::Colour (0xff9db6ff));
    g.fillRoundedRectangle (
        meterArea.translated (0.0f, 14.0f).withWidth (meterArea.getWidth() * out),
        4.0f);
}

void MorphEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    titleLabel.setBounds (18, 10, 220, 28);
    subtitleLabel.setBounds (18, 35, 280, 18);

    bypassButton.setBounds (w - 520, 18, 88, 28);
    realtimeButton.setBounds (w - 430, 18, 92, 28);
    qualityBox.setBounds (w - 334, 17, 145, 30);
    latencyLabel.setBounds (w - 182, 17, 160, 30);

    statusLabel.setBounds (18, h - 30, w - 36, 24);

    preGainSlider.setBounds (14, 101, 82, 102);
    pitchSlider.setBounds (98, 101, 82, 102);
    outputSlider.setBounds (14, 226, 82, 102);
    mixSlider.setBounds (98, 226, 82, 102);
    radiusSlider.setBounds (14, 351, 82, 102);
    strengthSlider.setBounds (98, 351, 82, 102);

    inputModeBox.setBounds (18, 468, 158, 28);
    importButton.setBounds (18, 510, 158, 32);
    presetBox.setBounds (18, 550, 158, 28);

    const int rx = w - 198;

    hexEditor.setBounds (rx, 132, 180, 30);
    voiceSpaceSlider.setBounds (rx, 190, 82, 108);
    toneSlider.setBounds (rx + 92, 190, 82, 108);
    generateButton.setBounds (rx, 294, 180, 32);
    voiceList.setBounds (rx, 350, 180, 30);
    removeButton.setBounds (rx, 390, 86, 30);
    clearButton.setBounds (rx + 94, 390, 86, 30);

    const int centreX = 194;
    const int centreW = w - 194 - 214;
    morphCanvas.setBounds (centreX, 64, centreW, h - 164);

    const int waypointY = h - 92;
    waypointInfoLabel.setBounds (centreX + 18, waypointY - 20, centreW - 36, 18);
    saveWaypointButton.setBounds (centreX + 18, waypointY, 104, 30);
    previousWaypointButton.setBounds (centreX + 128, waypointY, 30, 30);
    nextWaypointButton.setBounds (centreX + 162, waypointY, 30, 30);

    int buttonX = centreX + 204;
    for (int i = 0; i < (int) waypointButtons.size(); ++i)
    {
        waypointButtons[(size_t) i].setBounds (buttonX, waypointY, 30, 30);
        buttonX += 34;
    }

    clearWaypointsButton.setBounds (centreX + centreW - 86, waypointY, 68, 30);
}

void MorphEditor::timerCallback()
{
    const auto count = (int) processor.getProfilesSnapshot().size();

    if (count != lastVoiceCount)
        updateVoiceList();

    const int quality = (int) processor.parameters.getRawParameterValue ("quality")->load();
    static constexpr int latencies[] { 35, 50, 95, 105 };

    latencyLabel.setText (
        "mode " + juce::String (latencies[juce::jlimit (0, 3, quality)]) + " ms",
        juce::dontSendNotification);

    const int waypointCount = processor.getWaypointCount();
    const int activeWaypoint = processor.getActiveWaypoint();

    for (int i = 0; i < (int) waypointButtons.size(); ++i)
    {
        waypointButtons[(size_t) i].setEnabled (i < waypointCount);
        waypointButtons[(size_t) i].setColour (
            juce::TextButton::buttonColourId,
            i == activeWaypoint ? juce::Colour (accentColour).darker (0.45f)
                                : juce::Colour (0xff252c38));
    }

    morphCanvas.repaint();
    repaint();
}
