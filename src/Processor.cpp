#include "Processor.h"
#include "Editor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MorphProcessor::layout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("mix",1),"Mix",0.0f,1.0f,1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("pitch",1),"Pitch",juce::NormalisableRange<float>(-24,24,1),0));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("bypass",1),"Bypass",false));
    return {p.begin(),p.end()};
}

MorphProcessor::MorphProcessor()
 : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
   Thread("Voice transformation"),parameters(*this,nullptr,"MetamorphRebuild",layout())
{
    const auto cache=juce::SystemStats::getEnvironmentVariable("METAMORPH_CACHE_DIR",{});
    auto parent=cache.isNotEmpty()?juce::File(cache):juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MetamorphRebuild/sessions");
    sessionFolder=parent.getChildFile(juce::Uuid().toString());
}
MorphProcessor::~MorphProcessor() { signalThreadShouldExit(); stopThread(5000); }
bool MorphProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{
    return b.getMainInputChannelSet()==b.getMainOutputChannelSet() && (b.getMainOutputChannelSet()==juce::AudioChannelSet::mono() || b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());
}
void MorphProcessor::prepareToPlay(double rate,int)
{
    // Changes in rate invalidate timing; never play a stale buffer at the wrong rate.
    const juce::SpinLock::ScopedLockType lock(audioLock);
    if (sampleRate.load()!=rate || captured.getNumSamples()==0)
    {
        armed=false; preview=false; recordedSamples=0; resultReady=false;
        captured.setSize(1,(int)std::ceil(rate*180.0));
        transformed.setSize(1,0);
    }
    sampleRate=rate;fallbackSample=0;
}
void MorphProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c=getTotalNumInputChannels();c<b.getNumChannels();++c)b.clear(c,0,b.getNumSamples());
    const int n=b.getNumSamples();
    if (n==0 || b.getNumChannels()==0)return;
    meter=b.getRMSLevel(0,0,n);
    bool playing=true;juce::int64 position=fallbackSample;
    if (auto* head=getPlayHead())if (auto info=head->getPosition())
    {
        playing=info->getIsPlaying();
        if (auto samples=info->getTimeInSamples())position=*samples;
    }
    lastHostSample=position;fallbackSample=position+n;
    if(parameters.getRawParameterValue("bypass")->load()>.5f)return;
    const juce::SpinLock::ScopedTryLockType lock(audioLock);
    if (!lock.isLocked())return; // Audio callback never waits for file I/O or inference.
    if (armed.load() && playing)
    {
        if (awaitingOrigin) { originSample=position;awaitingOrigin=false; }
        const int start=recordedSamples.load();
        const int count=juce::jmin(n,captured.getNumSamples()-start);
        auto* dest=captured.getWritePointer(0)+start;
        for(int i=0;i<count;++i)
        {
            float sum=0;for(int c=0;c<getTotalNumInputChannels();++c)sum+=b.getSample(c,i);
            dest[i]=sum/(float)juce::jmax(1,getTotalNumInputChannels());
        }
        recordedSamples=start+count;wasRecording=true;
        if(count<n)armed=false;
    }
    else if (wasRecording && !playing) { armed=false;wasRecording=false; }
    if(armed.load() || busy.load())return;
    const bool audition=preview.load();
    if(!audition && (!playing || !resultReady.load()))return;
    const float mix=parameters.getRawParameterValue("mix")->load();
    const int length=recordedSamples.load();
    for(int i=0;i<n;++i)
    {
        const auto at=audition?(juce::int64)previewSample+i:position+i-originSample;
        if(at<0 || at>=length)continue;
        const float dry=captured.getSample(0,(int)at);
        const float wet=resultReady.load()?transformed.getSample(0,(int)at):dry;
        for(int c=0;c<b.getNumChannels();++c)
            b.setSample(c,i,(audition?dry:b.getSample(c,i))*(1-mix)+wet*mix);
    }
    if(audition) { previewSample+=n;if(previewSample>=length){preview=false;previewSample=0;} }
}
juce::AudioProcessorEditor* MorphProcessor::createEditor(){return new MorphEditor(*this);}
juce::AudioProcessorParameter* MorphProcessor::getBypassParameter() const { return parameters.getParameter("bypass"); }
void MorphProcessor::message(juce::String text){const juce::ScopedLock lock(textLock);statusText=std::move(text);}
juce::String MorphProcessor::status() const {const juce::ScopedLock lock(textLock);return statusText;}
juce::String MorphProcessor::modelName() const {const juce::ScopedLock lock(textLock);return modelFile.getFileNameWithoutExtension();}
bool MorphProcessor::setModel(const juce::File& file)
{
    if(busy)return false;
    if(!file.existsAsFile() || !file.hasFileExtension("pth")){message("Choose an RVC .pth voice model.");return false;}
    {const juce::ScopedLock lock(textLock);modelFile=file;}
    message("Voice selected. Record or import audio, then transform.");return true;
}
bool MorphProcessor::armRecord()
{
    if(busy)return false;
    {const juce::SpinLock::ScopedLockType lock(audioLock);recordedSamples=0;resultReady=false;preview=false;awaitingOrigin=true;wasRecording=false;armed=true;}
    message("Recording armed. Play your vocal track in the DAW.");return true;
}
void MorphProcessor::stopRecord(){armed=false;message("Recording stopped. Ready to transform.");}
void MorphProcessor::resetAudio()
{
    if(busy)return;
    const juce::SpinLock::ScopedLockType lock(audioLock);armed=false;preview=false;recordedSamples=0;resultReady=false;progress=0;
    message("Ready for a new recording.");
}
void MorphProcessor::togglePreview()
{
    if(busy || armed || !hasAudio())return;
    const juce::SpinLock::ScopedLockType lock(audioLock);previewSample=0;preview=!preview.load();
}
std::array<float,256> MorphProcessor::waveform() const
{
    std::array<float,256> peaks{};
    const juce::SpinLock::ScopedTryLockType lock(audioLock);if(!lock.isLocked())return peaks;
    const int n=recordedSamples.load();
    for(int i=0;i<256 && n>0;++i)
        for(int j=0;j<8;++j){const int at=juce::jmin(n-1,(int)((juce::int64)n*(i*8+j)/(256*8)));peaks[(size_t)i]=juce::jmax(peaks[(size_t)i],std::abs(captured.getSample(0,at)));}
    return peaks;
}
bool MorphProcessor::writeWave(const juce::File& file,const juce::AudioBuffer<float>& audio,int count,double rate) const
{
    if(!file.getParentDirectory().createDirectory())return false;
    auto stream=file.createOutputStream();if(!stream)return false;
    stream->setPosition(0);stream->truncate();juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,1,24,{},0));
    return writer && writer->writeFromAudioSampleBuffer(audio,0,count);
}
bool MorphProcessor::readWave(const juce::File& file,juce::AudioBuffer<float>& out,double rate,int maximum) const
{
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader || reader->sampleRate<8000 || reader->sampleRate>192000 || reader->numChannels<1 || reader->numChannels>2 || reader->lengthInSamples<1 || reader->lengthInSamples/reader->sampleRate>180)return false;
    const int count=(int)std::llround(reader->lengthInSamples*rate/reader->sampleRate);
    if(count>maximum || count<1)return false;
    juce::AudioBuffer<float> original((int)reader->numChannels,(int)reader->lengthInSamples+8);original.clear();
    if(!reader->read(&original,0,(int)reader->lengthInSamples,0,true,true))return false;
    if(original.getNumChannels()==2)for(int i=0;i<(int)reader->lengthInSamples;++i)original.setSample(0,i,.5f*(original.getSample(0,i)+original.getSample(1,i)));
    out.setSize(1,count);
    juce::LagrangeInterpolator resampler;resampler.process(reader->sampleRate/rate,original.getReadPointer(0),out.getWritePointer(0),count);
    for(int i=0;i<count;++i)if(!std::isfinite(out.getSample(0,i)))return false;
    return true;
}
bool MorphProcessor::importAudio(const juce::File& file)
{
    if(busy)return false;
    juce::AudioBuffer<float> temp;
    if(!readWave(file,temp,sampleRate.load(),captured.getNumSamples())){message("Use a mono/stereo audio file up to 3 minutes.");return false;}
    {const juce::SpinLock::ScopedLockType lock(audioLock);armed=false;preview=false;resultReady=false;captured.copyFrom(0,0,temp,0,0,temp.getNumSamples());recordedSamples=temp.getNumSamples();originSample=lastHostSample.load();}
    message("Audio imported. Its start is aligned to the current DAW position.");return true;
}
juce::File MorphProcessor::resourceRoot() const
{
    const auto env=juce::SystemStats::getEnvironmentVariable("METAMORPH_RESOURCES",{});
    if(env.isNotEmpty())return juce::File(env);
    {const juce::ScopedLock lock(textLock);if(resourcesOverride.isDirectory())return resourcesOverride;}
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getParentDirectory().getChildFile("Resources");
}
void MorphProcessor::setResources(const juce::File& folder)
{
    if(busy)return;
    {const juce::ScopedLock lock(textLock);resourcesOverride=folder;}
    message("Runtime folder selected.");
}
bool MorphProcessor::transform()
{
    if(busy || isThreadRunning())return false;
    if(!hasAudio()){message("Record or import audio first.");return false;}
    {const juce::ScopedLock lock(textLock);if(!modelFile.existsAsFile()){statusText="Choose a voice .pth file first.";return false;}}
    armed=false;preview=false;busy=true;progress=.01f;
    jobPitch=parameters.getRawParameterValue("pitch")->load();message("Starting voice engine...");
    startThread();return true;
}
void MorphProcessor::cancelTransform(){signalThreadShouldExit();message("Cancelling transformation...");}
void MorphProcessor::run()
{
    struct Finish {std::atomic<bool>& b;~Finish(){b=false;}} finish{busy};
    const auto resources=resourceRoot();
    const auto customPython=juce::SystemStats::getEnvironmentVariable("METAMORPH_PYTHON",{});
    const auto python=customPython.isNotEmpty()?juce::File(customPython):resources.getChildFile("runtime/python.exe");
    const auto script=resources.getChildFile("worker/run.py");
    if(!python.existsAsFile() || !script.existsAsFile()){message("Runtime missing. Install the full VST3 folder or select its Resources folder.");return;}
    const auto job=sessionFolder.getChildFile(juce::Uuid().toString());
    if(!job.createDirectory()){message("Cannot create an audio cache folder.");return;}
    const auto input=job.getChildFile("input.wav"),output=job.getChildFile("output.wav"),request=job.getChildFile("request.json"),statusFile=job.getChildFile("status.json");
    juce::AudioBuffer<float> source;
    double rate=sampleRate.load();
    {const juce::SpinLock::ScopedLockType lock(audioLock);source.setSize(1,recordedSamples.load());source.copyFrom(0,0,captured,0,0,source.getNumSamples());}
    if(!writeWave(input,source,source.getNumSamples(),rate)){message("Unable to save the recording for conversion.");return;}
    auto* object=new juce::DynamicObject();juce::var settings(object);
    object->setProperty("input",input.getFullPathName());object->setProperty("output",output.getFullPathName());object->setProperty("resources",resources.getFullPathName());object->setProperty("pitch",jobPitch);
    {const juce::ScopedLock lock(textLock);object->setProperty("model",modelFile.getFullPathName());}
    if(!request.replaceWithText(juce::JSON::toString(settings))){message("Unable to create the conversion request.");return;}
    juce::ChildProcess worker;
    if(!worker.start(juce::StringArray{python.getFullPathName(),"-B",script.getFullPathName(),request.getFullPathName()},juce::ChildProcess::wantStdOut|juce::ChildProcess::wantStdErr)){message("The voice engine could not start.");return;}
    const auto started=juce::Time::getMillisecondCounterHiRes();
    while(worker.isRunning())
    {
        if(threadShouldExit() || juce::Time::getMillisecondCounterHiRes()-started>30*60*1000)
        {worker.kill();worker.waitForProcessToFinish(2000);message("Transformation cancelled or timed out.");return;}
        const auto state=juce::JSON::parse(statusFile);
        if(auto* obj=state.getDynamicObject()){progress=(float)obj->getProperty("progress");message(obj->getProperty("message").toString());}
        wait(150);
    }
    const auto logs=worker.readAllProcessOutput();job.getChildFile("worker.log").replaceWithText(logs);
    const auto state=juce::JSON::parse(statusFile);
    if(worker.getExitCode()!=0 || state.getProperty("state",{}).toString()!="done")
    {message(state.getProperty("message","Voice engine failed. See the session's worker.log.").toString());return;}
    juce::AudioBuffer<float> result;
    if(!readWave(output,result,rate,source.getNumSamples()) || result.getNumSamples()!=source.getNumSamples())
    {message("The voice engine returned an invalid audio length.");return;}
    if(sampleRate.load()!=rate || recordedSamples.load()!=source.getNumSamples())
    {message("Audio settings changed during conversion. Please transform again.");return;}
    {const juce::SpinLock::ScopedLockType lock(audioLock);transformed=std::move(result);resultReady=true;}
    progress=1;message("Ready. Replay the recorded section in your DAW, or click Preview.");
}
bool MorphProcessor::exportAudio(const juce::File& file)
{
    if(busy || !hasAudio())return false;
    juce::AudioBuffer<float> audio;
    {const juce::SpinLock::ScopedLockType lock(audioLock);const int n=recordedSamples.load();audio.setSize(1,n);const float mix=parameters.getRawParameterValue("mix")->load();
     for(int i=0;i<n;++i){const float dry=captured.getSample(0,i);audio.setSample(0,i,resultReady?dry*(1-mix)+transformed.getSample(0,i)*mix:dry);}}
    const bool ok=writeWave(file,audio,audio.getNumSamples(),sampleRate.load());message(ok?"WAV exported.":"Unable to write this WAV file.");return ok;
}
void MorphProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto tree=parameters.copyState();
    {const juce::ScopedLock lock(textLock);tree.setProperty("model",modelFile.getFullPathName(),nullptr);tree.setProperty("resources",resourcesOverride.getFullPathName(),nullptr);}
    // Session audio is intentionally exported explicitly; preset state never embeds large recordings.
    if(auto xml=tree.createXml())copyXmlToBinary(*xml,destination);
}
void MorphProcessor::setStateInformation(const void* data,int size)
{
    if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(parameters.state.getType()))
    {auto tree=juce::ValueTree::fromXml(*xml);parameters.replaceState(tree);const juce::ScopedLock lock(textLock);modelFile=juce::File(tree.getProperty("model").toString());resourcesOverride=juce::File(tree.getProperty("resources").toString());}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MorphProcessor();}
