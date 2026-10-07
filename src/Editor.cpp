#include "Editor.h"

namespace { const juce::Colour mint{0xff83edcf}, ink{0xff0c1118},muted{0xff81929f}; }
MorphEditor::MorphEditor(MorphProcessor& p):AudioProcessorEditor(p),processor(p)
{
    look.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff22313c));
    look.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffedf6f6));
    look.setColour(juce::Slider::thumbColourId,mint);
    look.setColour(juce::Slider::rotarySliderFillColourId,mint);
    look.setColour(juce::Slider::trackColourId,mint);
    look.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    setLookAndFeel(&look);
    for(auto* b:{&model,&import,&record,&transform,&reset,&preview,&save,&settings})addAndMakeVisible(b);
    for(auto* c:std::initializer_list<juce::Component*>{&bypass,&mix,&pitch,&statusLabel,&modelLabel})addAndMakeVisible(c);
    mix.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);mix.setTextBoxStyle(juce::Slider::TextBoxBelow,false,90,24);
    mix.textFromValueFunction=[](double v){return juce::String((int)std::round(v*100))+"%";};
    mix.valueFromTextFunction=[](const juce::String& s){return s.getDoubleValue()/100;};
    pitch.setSliderStyle(juce::Slider::LinearHorizontal);pitch.setTextBoxStyle(juce::Slider::TextBoxRight,false,80,24);
    pitch.setTextValueSuffix(" st");
    mixAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,"mix",mix);
    pitchAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,"pitch",pitch);
    bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"bypass",bypass);
    transform.setColour(juce::TextButton::buttonColourId,mint);transform.setColour(juce::TextButton::textColourOffId,ink);
    statusLabel.setColour(juce::Label::textColourId,juce::Colour(0xffb6c6cf));statusLabel.setJustificationType(juce::Justification::centred);statusLabel.setFont(14.0f);
    modelLabel.setJustificationType(juce::Justification::centred);modelLabel.setFont(19.0f);
    model.onClick=[this]{choose(0);};import.onClick=[this]{choose(1);};save.onClick=[this]{choose(2);};settings.onClick=[this]{choose(3);};
    record.onClick=[this]{if(processor.isArmed())processor.stopRecord();else processor.armRecord();};
    transform.onClick=[this]{if(processor.isBusy())processor.cancelTransform();else processor.transform();};
    reset.onClick=[this]{processor.resetAudio();};preview.onClick=[this]{processor.togglePreview();};
    setSize(720,820);startTimerHz(12);timerCallback();
}
MorphEditor::~MorphEditor(){stopTimer();setLookAndFeel(nullptr);}
void MorphEditor::resized()
{
    bypass.setBounds(586,24,100,30);settings.setBounds(526,764,162,28);
    modelLabel.setBounds(72,309,576,30);model.setBounds(220,351,280,38);
    import.setBounds(32,586,134,38);record.setBounds(178,586,134,38);transform.setBounds(324,586,222,38);reset.setBounds(558,586,130,38);
    mix.setBounds(543,636,140,113);pitch.setBounds(112,653,390,40);
    preview.setBounds(32,710,138,34);save.setBounds(182,710,138,34);
    statusLabel.setBounds(32,763,472,37);
}
void MorphEditor::paint(juce::Graphics& g)
{
    g.fillAll(ink);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b3440),360,160,ink,360,550,true));g.fillRect(getLocalBounds());
    g.setColour(juce::Colour(0xffeaf3f2));g.setFont(juce::FontOptions(27.0f,juce::Font::bold));g.drawText("METAMORPH",32,22,370,35,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(11.0f);g.drawText("REBUILD  /  OFFLINE VOICE TRANSFORMATION",33,60,490,18,juce::Justification::centredLeft);
    const auto centre=juce::Point<float>(360,198);
    for(int i=0;i<6;++i){g.setColour(mint.withAlpha(.035f+.012f*i));const float radius=108.0f-i*5;g.drawEllipse(centre.x-radius,centre.y-radius,radius*2,radius*2,1.0f);}
    g.setColour(juce::Colour(0xff101e26));g.fillEllipse(278,116,164,164);
    g.setColour(mint.withAlpha(.5f));g.drawEllipse(278,116,164,164,1.2f);
    for(int i=0;i<29;++i)
    {
        const float x=296+i*4.6f,phase=(float)i*.58f;
        const float shape=std::sin((float)i/28.0f*juce::MathConstants<float>::pi);
        const float height=10+shape*(25+19*std::sin(phase)*std::sin(phase)+juce::jmin(.6f,processor.level())*75);
        g.setColour(mint.withAlpha(.5f+.5f*shape));g.fillRoundedRectangle(x,198-height*.5f,2.6f,height,1.3f);
    }
    g.setColour(muted);g.setFont(12.0f);g.drawText("YOUR VOICE MODEL",180,282,360,20,juce::Justification::centred);
    juce::Rectangle<float> wave(32,414,656,142);
    g.setColour(juce::Colour(0xff101b24));g.fillRoundedRectangle(wave,12);
    g.setColour(juce::Colour(0xff2b3b45));g.drawRoundedRectangle(wave,12,1);
    g.setColour(muted);g.setFont(11.0f);g.drawText("RECORDING",48,425,150,20,juce::Justification::centredLeft);
    g.drawText(juce::String(processor.duration(),1)+" s / 180 s",450,425,220,20,juce::Justification::centredRight);
    const auto values=processor.waveform();g.setColour(processor.isArmed()?juce::Colour(0xffff8e9c):mint);
    for(int i=0;i<256;++i){const float h=juce::jmax(1.0f,juce::jmin(1.0f,values[(size_t)i])*72);g.fillRect(48+i*2.43f,498-h*.5f,1.3f,h);}
    if(!processor.hasAudio()){g.setColour(muted);g.setFont(14.0f);g.drawText("Record your vocal track or drop an audio file here",80,519,560,24,juce::Justification::centred);}
    g.setColour(muted);g.setFont(12.0f);g.drawText("PITCH",32,660,80,25,juce::Justification::centredLeft);g.drawText("DRY / WET",538,743,150,18,juce::Justification::centred);
    g.setColour(juce::Colour(0xff293b43));g.fillRoundedRectangle(32,567,656,4,2);
    if(processor.completion()>0){g.setColour(mint);g.fillRoundedRectangle(32,567,656*processor.completion(),4,2);}
}
void MorphEditor::timerCallback()
{
    const bool busy=processor.isBusy();
    model.setEnabled(!busy);import.setEnabled(!busy);record.setEnabled(!busy);reset.setEnabled(!busy);settings.setEnabled(!busy);
    transform.setButtonText(busy?"Cancel":"Transform");transform.setEnabled(busy||processor.hasAudio());
    preview.setEnabled(!busy&&processor.hasAudio()&&!processor.isArmed());save.setEnabled(!busy&&processor.hasAudio()&&!processor.isArmed());
    record.setButtonText(processor.isArmed()?"Stop recording":"Record");preview.setButtonText(processor.isPreviewing()?"Stop preview":"Preview");
    statusLabel.setText(processor.status(),juce::dontSendNotification);
    modelLabel.setText(processor.modelName().isEmpty()?"A new voice starts here":processor.modelName(),juce::dontSendNotification);repaint();
}
void MorphEditor::choose(int kind)
{
    const juce::String titles[]={"Choose an RVC .pth voice model","Import vocal audio","Export audio","Choose the bundled Resources folder"};
    const juce::String filters[]={"*.pth","*.wav;*.aif;*.aiff;*.flac","*.wav","*"};
    chooser=std::make_unique<juce::FileChooser>(titles[kind],juce::File{},filters[kind]);
    int flags=kind==2?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode|(kind==3?juce::FileBrowserComponent::canSelectDirectories:juce::FileBrowserComponent::canSelectFiles);
    const juce::Component::SafePointer<MorphEditor> safe(this);
    chooser->launchAsync(flags,[safe,kind](const juce::FileChooser& c){if(!safe)return;const auto f=c.getResult();if(f==juce::File{})return;
        if(kind==0)safe->processor.setModel(f);else if(kind==1)safe->processor.importAudio(f);else if(kind==2)safe->processor.exportAudio(f.withFileExtension("wav"));else safe->processor.setResources(f);});
}
bool MorphEditor::isInterestedInFileDrag(const juce::StringArray& files){return files.size()==1 && juce::File(files[0]).hasFileExtension("pth;wav;aif;aiff;flac");}
void MorphEditor::filesDropped(const juce::StringArray& files,int,int){if(files.size()!=1)return;const juce::File f(files[0]);if(f.hasFileExtension("pth"))processor.setModel(f);else processor.importAudio(f);}
