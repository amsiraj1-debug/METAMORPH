#include "Processor.h"
#include <iostream>
#include <stdexcept>

void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    try
    {
        MorphProcessor plugin;plugin.prepareToPlay(48000,256);
        juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;
        for(int c=0;c<2;++c)for(int i=0;i<256;++i)audio.setSample(c,i,.2f);
        plugin.processBlock(audio,midi);require(audio.getSample(0,40)==.2f,"Dry signal changed");
        require(!plugin.transform(),"Empty recording transformed");
        require(plugin.armRecord(),"Failed to arm");
        for(int block=0;block<100;++block)plugin.processBlock(audio,midi);
        plugin.stopRecord();require(std::abs(plugin.duration()-25600.0/48000)<.00001,"Recorded length differs");
        auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("metamorph-test",".wav");
        require(plugin.exportAudio(file),"Export failed");plugin.resetAudio();require(!plugin.hasAudio(),"Reset failed");
        require(plugin.importAudio(file),"Import failed");require(std::abs(plugin.duration()-25600.0/48000)<.00001,"Import length differs");file.deleteFile();
        auto* mix=plugin.parameters.getParameter("mix");mix->setValueNotifyingHost(.25f);
        juce::MemoryBlock state;plugin.getStateInformation(state);mix->setValueNotifyingHost(1);plugin.setStateInformation(state.getData(),(int)state.getSize());require(std::abs(mix->getValue()-.25f)<.001f,"State restore failed");
        if(argc>2)
        {
            require(plugin.setModel(juce::File(juce::String::fromUTF8(argv[2]))),"Voice model selection failed");
            require(plugin.transform(),"Conversion did not start");
            const auto deadline=juce::Time::getMillisecondCounterHiRes()+300000;
            while(plugin.isBusy() && juce::Time::getMillisecondCounterHiRes()<deadline)juce::Thread::sleep(100);
            if(!plugin.hasResult())std::cerr<<plugin.status()<<"\n";
            require(plugin.hasResult(),"Native plugin/worker end-to-end conversion failed");
            std::cout<<"PASS: native VST processor launched worker and received converted audio\n";
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditor());
        require(editor->getWidth()==720 && editor->getHeight()==820,"Editor dimensions differ");
        if(argc>1){auto shot=editor->createComponentSnapshot(editor->getLocalBounds());juce::File out(juce::String::fromUTF8(argv[1]));auto stream=out.createOutputStream();require(stream!=nullptr,"Snapshot output failed");juce::PNGImageFormat png;require(png.writeImageToStream(shot,*stream),"Snapshot failed");}
        std::cout<<"PASS: passthrough, recording, import/export, reset, parameter state, editor creation\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
