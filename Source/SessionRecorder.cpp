#include "SessionRecorder.h"
#include "Platform/AppPaths.h"
#include "Platform/PlatformFiles.h"

namespace tonegrid::recording
{
using namespace juce;
SessionRecorder::SessionRecorder(File encoder,double limit)
    :Thread("ToneGrid recording"),encoderFile(std::move(encoder)),
     sessionFolder(tonegrid::paths::temporaryRoot().getChildFile("ToneGrid-recording-"+Uuid().toString())),
     limitSeconds(jlimit(.01,maximumSeconds,limit))
{startThread();}
SessionRecorder::~SessionRecorder()
{
    signalThreadShouldExit();notify();waitForThreadToExit(-1);
    if(sessionFolder.isAChildOf(tonegrid::paths::temporaryRoot()))sessionFolder.deleteRecursively();
}
bool SessionRecorder::request(Command value)
{
    std::lock_guard<std::mutex> lock(controlMutex);
    if(pending.exchange(true))return false;
    command=std::move(value);notify();return true;
}
bool SessionRecorder::record(double rate)
{
    const auto current=state();
    return std::isfinite(rate)&&rate>=8000&&rate<=192000&&
        (current==Mode::empty||current==Mode::ready||current==Mode::playing)&&request({Action::record,rate,{}});
}
bool SessionRecorder::play(double rate)
{return state()==Mode::ready&&std::isfinite(rate)&&rate>=8000&&rate<=192000&&request({Action::play,rate,{}});}
bool SessionRecorder::stop()
{return (state()==Mode::recording||state()==Mode::playing)&&request({Action::stop,48000,{}});}
bool SessionRecorder::exportMp3(const File& target)
{return state()==Mode::ready&&target.hasFileExtension("mp3")&&request({Action::exportFile,48000,target});}
void SessionRecorder::message(const String& text)
{std::lock_guard<std::mutex> lock(controlMutex);notification=text;}
String SessionRecorder::takeMessage()
{std::lock_guard<std::mutex> lock(controlMutex);auto text=notification;notification.clear();return text;}
File SessionRecorder::completedTake() const
{return available()&&state()==Mode::ready?sessionFolder.getChildFile("take.wav"):File{};}
void SessionRecorder::suspendAudio(Mode next)
{
    // Sequential consistency pairs the mode change with readers entering process().
    // New callbacks see the suspended mode; the worker waits for old callbacks only.
    mode.store(next);
    while(readers.load()!=0)Thread::yield();
}
void SessionRecorder::process(float* const* output,int channels,int samples,bool muted) noexcept
{
    readers.fetch_add(1);
    const auto current=mode.load();
    if(output&&channels>0&&samples>0)
    {
        if(current==Mode::recording)
        {
            const int count=static_cast<int>(std::min<juce::int64>(samples,captureLimit-captured.load()));
            if(count>0)
            {
                int a=0,n=0,b=0,m=0;fifo.prepareToWrite(count,a,n,b,m);
                if(n+m!=count){overflow.store(true);finishRequested.store(true);}
                else
                {
                    for(int c=0;c<2;++c)
                    {
                        const auto* input=output[jmin(c,channels-1)];
                        if(input){FloatVectorOperations::copy(ring.getWritePointer(c,a),input,n);if(m)FloatVectorOperations::copy(ring.getWritePointer(c,b),input+n,m);}
                        else {FloatVectorOperations::clear(ring.getWritePointer(c,a),n);if(m)FloatVectorOperations::clear(ring.getWritePointer(c,b),m);}
                    }
                    fifo.finishedWrite(count);captured.fetch_add(count);
                }
            }
            if(captured.load()>=captureLimit)finishRequested.store(true);
        }
        else if(current==Mode::playing)
        {
            for(int c=0;c<channels;++c)if(output[c])FloatVectorOperations::clear(output[c],samples);
            int a=0,n=0,b=0,m=0;fifo.prepareToRead(samples,a,n,b,m);
            if(!muted)for(int c=0;c<jmin(2,channels);++c)if(output[c])
            {FloatVectorOperations::copy(output[c],ring.getReadPointer(c,a),n);if(m)FloatVectorOperations::copy(output[c]+n,ring.getReadPointer(c,b),m);}
            fifo.finishedRead(n+m);played.fetch_add(n+m);
            if(played.load()>=playLimit)finishRequested.store(true);
            else if(n+m<samples){overflow.store(true);finishRequested.store(true);}
        }
    }
    readers.fetch_sub(1);
}
void SessionRecorder::beginRecording(double rate)
{
    if(state()==Mode::playing)endPlayback();
    suspendAudio(Mode::preparing);
    if(sessionFolder.createDirectory().failed()){message("Cannot create the temporary recording file.");mode.store(restingState());return;}
    const auto candidate=sessionFolder.getChildFile("new.wav");
    candidate.deleteFile();auto stream=candidate.createOutputStream();WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> next(stream?wav.createWriterFor(stream.get(),rate,2,24,{},0):nullptr);
    if(next)stream.release();
    if(!next){candidate.deleteFile();message("Cannot start recording; the previous take was kept.");mode.store(restingState());return;}
    take.deleteFile();hasTake.store(false);writer=std::move(next);take=candidate;
    fifo.reset();captured.store(0);takeRate.store(rate);captureLimit=std::llround(rate*limitSeconds);storageFailed=false;
    finishRequested.store(false);overflow.store(false);mode.store(Mode::recording);
}
bool SessionRecorder::drainRecording()
{
    int a=0,n=0,b=0,m=0;fifo.prepareToRead(jmin(blockSize,fifo.getNumReady()),a,n,b,m);
    const float* first[]{ring.getReadPointer(0,a),ring.getReadPointer(1,a)};
    const float* second[]{ring.getReadPointer(0,b),ring.getReadPointer(1,b)};
    const bool ok=(!n||writer->writeFromFloatArrays(first,2,n))&&(!m||writer->writeFromFloatArrays(second,2,m));
    fifo.finishedRead(n+m);return ok;
}
void SessionRecorder::finishRecording()
{
    suspendAudio(Mode::finishing);bool ok=!storageFailed;
    while(fifo.getNumReady()>0)ok=drainRecording()&&ok;
    writer.reset();
    const auto completed=sessionFolder.getChildFile("take.wav");
    if(ok&&captured.load()>0&&take.moveFileTo(completed)){take=completed;hasTake.store(true);}
    else {take.deleteFile();hasTake.store(false);}
    if(!ok)message("Recording could not be saved; check your temporary disk space.");
    else if(overflow.load())message("Recording stopped because storage could not keep up. The recorded portion is available.");
    else if(captured.load()>=captureLimit)message("Recording stopped at the 10-minute limit.");
    finishRequested.store(false);mode.store(restingState());
}
void SessionRecorder::fillPlayback()
{
    while(produced<playLimit&&fifo.getFreeSpace()>=blockSize)
    {
        const int count=static_cast<int>(std::min<juce::int64>(blockSize,playLimit-produced));
        scratch.clear();resampler->getNextAudioBlock({&scratch,0,count});
        int a=0,n=0,b=0,m=0;fifo.prepareToWrite(count,a,n,b,m);
        for(int c=0;c<2;++c)
        {ring.copyFrom(c,a,scratch,c,0,n);if(m)ring.copyFrom(c,b,scratch,c,n,m);}
        fifo.finishedWrite(n+m);produced+=n+m;
    }
}
void SessionRecorder::beginPlayback(double rate)
{
    suspendAudio(Mode::preparing);WavAudioFormat wav;
    auto stream=take.createInputStream();auto* reader=stream?wav.createReaderFor(stream.release(),true):nullptr;
    if(!reader){message("Cannot read the last recording.");mode.store(restingState());return;}
    playLimit=std::llround(reader->lengthInSamples*rate/reader->sampleRate);
    readerSource=std::make_unique<AudioFormatReaderSource>(reader,true);
    resampler=std::make_unique<ResamplingAudioSource>(readerSource.get(),false,2);
    resampler->setResamplingRatio(reader->sampleRate/rate);resampler->prepareToPlay(blockSize,rate);
    fifo.reset();played.store(0);produced=0;finishRequested.store(false);overflow.store(false);fillPlayback();mode.store(Mode::playing);
}
void SessionRecorder::endPlayback()
{
    suspendAudio(Mode::finishing);resampler.reset();readerSource.reset();fifo.reset();finishRequested.store(false);
    if(overflow.load())message("Playback stopped because the audio buffer ran out. Your recording is intact.");
    mode.store(restingState());
}
void SessionRecorder::encode(const File& destination)
{
    suspendAudio(Mode::exporting);
    if(!encoderFile.existsAsFile()){message(tonegrid::platform::missingEncoderMessage());mode.store(restingState());return;}
    TemporaryFile staged(destination);ChildProcess child;
    const StringArray arguments{encoderFile.getFullPathName(),"--quiet","--cbr","-b","256",take.getFullPathName(),staged.getFile().getFullPathName()};
    bool ok=child.start(arguments,0);const auto deadline=Time::getMillisecondCounterHiRes()+120000;
    while(ok&&child.isRunning())
    {
        if(threadShouldExit()||Time::getMillisecondCounterHiRes()>deadline){child.kill();ok=false;break;}
        child.waitForProcessToFinish(30);
    }
    ok=ok&&child.getExitCode()==0&&staged.getFile().getSize()>0;
    if(ok)
    {
        ok=tonegrid::platform::commitExport(staged.getFile(),destination);
    }
    if(!threadShouldExit())message(ok?"MP3 exported: "+destination.getFileName():"MP3 export failed. Your recording is still available.");
    mode.store(restingState());
}
void SessionRecorder::run()
{
    while(!threadShouldExit())
    {
        Command next;
        {std::lock_guard<std::mutex> lock(controlMutex);next=std::move(command);command={};}
        if(next.action!=Action::none)
        {
            if(next.action==Action::record)beginRecording(next.rate);
            else if(next.action==Action::play)beginPlayback(next.rate);
            else if(next.action==Action::exportFile)encode(next.target);
            else if(state()==Mode::recording)finishRecording();
            else if(state()==Mode::playing)endPlayback();
            pending.store(false);
        }
        if(state()==Mode::recording)
        {
            if(finishRequested.load())finishRecording();
            else if(fifo.getNumReady()>0&&!drainRecording()){storageFailed=true;finishRequested.store(true);}
        }
        else if(state()==Mode::playing)
        {if(finishRequested.load())endPlayback();else fillPlayback();}
        // A completed/empty session consumes no polling CPU. Commands and shutdown
        // wake the worker; active audio uses short waits to drain/prefill the FIFO.
        wait(state()==Mode::recording||state()==Mode::playing?3:-1);
    }
    if(state()==Mode::recording)finishRecording();
    suspendAudio(Mode::closed);resampler.reset();readerSource.reset();writer.reset();
}
}
