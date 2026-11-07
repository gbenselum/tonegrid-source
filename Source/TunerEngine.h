#pragma once
#include "OutputPeak.h"
#include "Core/AudioInsert.h"
#include "SessionRecorder.h"
#include "LoopRecorder.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace tonegrid::tuning
{
struct Reading {double frequency=0,confidence=0,levelDb=-120;};
// YIN analysis adapted from Tone3000's MIT TunerDetector (see bundled license).
// Unlike its shared sample ring, this SPSC FIFO gives each sample one owner.
// The audio callback only copies into preallocated storage; analysis is off-thread.
class Engine final
{
public:
    Engine():worker([this]{run();}){}
    ~Engine(){stopping.store(true);worker.join();}
    Engine(const Engine&)=delete;Engine& operator=(const Engine&)=delete;
    void prepare(double sr) noexcept {sampleRate.store(sr>0?sr:48000);epoch.fetch_add(1);}
    void enable(bool value) noexcept {enabled.store(value);epoch.fetch_add(1);}
    bool active() const noexcept{return enabled.load();}
    void reset() noexcept{epoch.fetch_add(1);}
    void push(const float* samples,int count) noexcept
    {
        if(!samples||!enabled.load())return;
        int a,n,b,m;fifo.prepareToWrite(count,a,n,b,m);
        for(int i=0;i<n;++i)queue[size_t(a+i)]=samples[i];
        for(int i=0;i<m;++i)queue[size_t(b+i)]=samples[n+i];
        fifo.finishedWrite(n+m); // A full queue drops new samples, never blocks audio.
    }
    Reading reading() const {std::lock_guard<std::mutex> guard(resultMutex);return result;}
    // Pure analysis is also used by deterministic note/noise/accuracy tests.
    static Reading analyse(const std::array<float,2048>& window,double rate)
    {
        Reading out;double power=0,mean=0;
        for(float x:window){if(!std::isfinite(x))return out;mean+=x;power+=double(x)*x;}
        mean/=window.size();power=std::max(0.0,power/window.size()-mean*mean);
        out.levelDb=power>1e-18?10*std::log10(power):-120;
        if(out.levelDb< -55||!std::isfinite(rate)||rate<8000)return out;
        auto runYin=[](const float* samples,int length,double effRate,double minHz,double maxHz)->Reading
        {
            Reading r;
            const int maximum=std::min(length/2-1,int(effRate/minHz)),minimum=std::max(2,int(effRate/maxHz));
            if(maximum<=minimum)return r;
            const int integration=length-maximum;std::array<double,1024> yin{},raw{};yin[0]=1;double cumulative=0;
            for(int lag=1;lag<=maximum;++lag)
            {
                double difference=0;for(int i=0;i<integration;++i){const double d=double(samples[size_t(i)])-samples[size_t(i+lag)];difference+=d*d;}
                raw[size_t(lag)]=difference;cumulative+=difference;yin[size_t(lag)]=cumulative>0?difference*lag/cumulative:1;
            }
            int best=-1;
            for(int lag=minimum;lag<=maximum;++lag)if(yin[size_t(lag)]<.15)
            {while(lag<maximum&&yin[size_t(lag+1)]<yin[size_t(lag)])++lag;best=lag;break;}
            if(best<0){double value=.3;for(int lag=minimum;lag<=maximum;++lag)if(yin[size_t(lag)]<value){value=yin[size_t(lag)];best=lag;}}
            if(best<0)return r;
            double refined=best;
            // Refine the raw difference minimum; normalisation biases high-note cents.
            if(best>minimum&&best<maximum){const auto a=raw[size_t(best-1)],b=raw[size_t(best)],c=raw[size_t(best+1)];const double denominator=a-2*b+c;if(std::abs(denominator)>1e-12)refined+=.5*(a-c)/denominator;}
            r.frequency=effRate/refined;r.confidence=std::clamp(1-yin[size_t(best)],0.0,1.0);return r;
        };
        const auto primary=runYin(window.data(),2048,rate,28.0,1500.0);
        out.frequency=primary.frequency;out.confidence=primary.confidence;
        // At 48 kHz a 2048-sample window is 42.67 ms long, so half-window lag caps at 46.9 Hz,
        // missing 4-string E1 (41.20 Hz, 24.27 ms period) and 5-string B0 (30.87 Hz, 32.40 ms period).
        // When no pitch was locked or the primary lock is below 65 Hz, run a 4x-decimated
        // variable-overlap YIN pass (512 samples at rate/4, lags up to 84% of window -> down to 28 Hz).
        if(rate>=32000.0&&(out.confidence<.85||out.frequency<65.0))
        {
            std::array<float,512> decimated{};
            for(size_t i=0;i<512;++i)
                decimated[i]=.25f*(window[4*i]+window[4*i+1]+window[4*i+2]+window[4*i+3]);
            const double effRate=rate*.25;
            const int maximum=std::min(int(decimated.size()*84/100),int(effRate/27.5));
            const int minimum=std::max(2,int(effRate/120.0));
            if(maximum>minimum)
            {
                std::array<double,512> yin{},raw{};yin[0]=1;double cumulative=0;
                for(int lag=1;lag<=maximum;++lag)
                {
                    const int overlap=int(decimated.size())-lag;
                    double difference=0;
                    for(int i=0;i<overlap;++i){const double d=double(decimated[size_t(i)])-decimated[size_t(i+lag)];difference+=d*d;}
                    difference/=double(overlap);
                    raw[size_t(lag)]=difference;cumulative+=difference;
                    yin[size_t(lag)]=cumulative>0?difference*lag/cumulative:1;
                }
                int best=-1;
                for(int lag=minimum;lag<=maximum;++lag)if(yin[size_t(lag)]<.15)
                {while(lag<maximum&&yin[size_t(lag+1)]<yin[size_t(lag)])++lag;best=lag;break;}
                if(best<0){double value=.3;for(int lag=minimum;lag<=maximum;++lag)if(yin[size_t(lag)]<value){value=yin[size_t(lag)];best=lag;}}
                if(best>=minimum)
                {
                    double refined=best;
                    if(best>minimum&&best<maximum){const auto a=raw[size_t(best-1)],b=raw[size_t(best)],c=raw[size_t(best+1)];const double denominator=a-2*b+c;if(std::abs(denominator)>1e-12)refined+=.5*(a-c)/denominator;}
                    const double freq=effRate/refined;
                    const double conf=std::clamp(1-yin[size_t(best)],0.0,1.0);
                    if(freq>=27.5&&freq<=65.0&&conf>out.confidence-.02)
                    {out.frequency=freq;out.confidence=conf;}
                }
            }
        }
        return out;
    }
private:
    void publish(Reading value){std::lock_guard<std::mutex> guard(resultMutex);result=value;}
    void run()
    {
        std::array<float,2048> ring{},window{};int position=0,received=0,group=0;float sum=0;unsigned lastEpoch=0;
        while(!stopping.load())
        {
            const auto revision=epoch.load();const double sr=sampleRate.load();const int decimation=juce::jlimit(1,16,juce::roundToInt(sr/24000.0));
            if(revision!=lastEpoch){lastEpoch=revision;position=received=group=0;sum=0;publish({});const int old=fifo.getNumReady();if(old)fifo.finishedRead(old);}
            const int count=fifo.getNumReady();int a,n,b,m;fifo.prepareToRead(count,a,n,b,m);
            auto consume=[&](int start,int length){for(int i=0;i<length;++i){float x=queue[size_t(start+i)];sum+=std::isfinite(x)?x:0;if(++group==decimation){ring[size_t(position)]=sum/float(decimation);position=(position+1)%2048;received=std::min(received+1,2048);group=0;sum=0;}}};
            consume(a,n);consume(b,m);fifo.finishedRead(n+m);
            if(enabled.load()&&received==2048&&count>0){for(int i=0;i<2048;++i)window[size_t(i)]=ring[size_t((position+i)%2048)];auto value=analyse(window,sr/decimation);if(epoch.load()==revision)publish(value);}
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    static_assert(std::atomic<double>::is_always_lock_free);
    juce::AbstractFifo fifo{32768};std::array<float,32768> queue{};
    std::atomic<bool> enabled{false},stopping{false};std::atomic<unsigned> epoch{1};std::atomic<double> sampleRate{48000};
    mutable std::mutex resultMutex;Reading result;std::thread worker;
};
// Native input tap works with NAM and VST3, before either graph or input gate.
class AudioTap final:public juce::AudioIODeviceCallback
{
public:
    AudioTap(juce::AudioProcessorPlayer& p,Engine& e):player(p),engine(e){}
    void setInsert(tonegrid::core::IAudioInsert* value) noexcept{insert.store(value);}
    void setLooper(tonegrid::looping::Engine* value) noexcept{looper=value;}
    void setRecorder(tonegrid::recording::SessionRecorder* value) noexcept{recorder=value;}
    float takeOutputPeak() noexcept{return outputPeak.take();}
    float takeRawInputPeak() noexcept{return rawInputPeak.take();}
    bool inputConnected() const noexcept{return connected.load(std::memory_order_relaxed);}
    void mute(bool value) noexcept{muted.store(value);}
    bool isMuted() const noexcept{return muted.load();}
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {connected.store(true,std::memory_order_relaxed);step=float(1.0/(device->getCurrentSampleRate()*.005));level=muted.load()?0.f:1.f;engine.prepare(device->getCurrentSampleRate());if(auto* route=insert.load())route->deviceStarted(device->getCurrentSampleRate());if(looper)looper->prepare(device->getCurrentSampleRate());player.audioDeviceAboutToStart(device);}
    // Keep the popup's enabled/mute choices across device reconfiguration.
    // prepare() invalidates the old analysis on the next device start.
    void audioDeviceStopped() override{connected.store(false,std::memory_order_relaxed);rawInputPeak.reset();if(auto* route=insert.load())route->deviceStopped();player.audioDeviceStopped();outputPeak.reset();engine.reset();if(recorder)recorder->deviceStopped();if(looper)looper->deviceStopped();}
    void audioDeviceIOCallbackWithContext(const float* const* input,int inputs,float* const* output,int outputs,int samples,const juce::AudioIODeviceCallbackContext& context) override
    {
        if(inputs>0){rawInputPeak.push(input,1,samples);engine.push(input[0],samples);}

        if(auto* route=insert.load();route&&route->enabled())
        {
            // Bounded slices feed the selected raw device input to the DI insert.
            // The existing AudioProcessorPlayer still supplies all guitar DSP.
            std::array<float*,64> out{};std::array<const float*,64> in{};
            for(int offset=0;offset<samples;offset+=512)
            {
                const int frames=juce::jmin(512,samples-offset);
                for(int c=0;c<juce::jmin(inputs,64);++c)in[size_t(c)]=input[c]?input[c]+offset:nullptr;
                for(int c=0;c<juce::jmin(outputs,64);++c)out[size_t(c)]=output[c]?output[c]+offset:nullptr;
                const auto* routed=route->input(in.data(),juce::jmin(inputs,64),frames);
                player.audioDeviceIOCallbackWithContext(routed,2,out.data(),juce::jmin(outputs,64),frames,context);
                const float target=muted.load()?0.f:1.f;
                if(target!=1||level!=1)for(int i=0;i<frames;++i){level=target>level?std::min(target,level+step):std::max(target,level-step);for(int c=0;c<juce::jmin(outputs,64);++c)if(out[size_t(c)])out[size_t(c)][i]*=level;}
                route->output(out.data(),juce::jmin(outputs,64),frames);
            }
            outputPeak.push(output,outputs,samples);return;
        }
        player.audioDeviceIOCallbackWithContext(input,inputs,output,outputs,samples,context);
        if(looper)looper->process(output,outputs,samples);
        const float target=muted.load()?0.f:1.f;
        if(target!=1||level!=1)
            for(int i=0;i<samples;++i){level=target>level?std::min(target,level+step):std::max(target,level-step);for(int c=0;c<outputs;++c)if(output[c])output[c][i]*=level;}
        if(recorder)recorder->process(output,outputs,samples,muted.load());
        outputPeak.push(output,outputs,samples);
    }
private:
    std::atomic<tonegrid::core::IAudioInsert*> insert{nullptr};
    tonegrid::OutputPeak outputPeak,rawInputPeak;std::atomic<bool> connected{false};
    juce::AudioProcessorPlayer& player;Engine& engine;tonegrid::recording::SessionRecorder* recorder=nullptr;tonegrid::looping::Engine* looper=nullptr;std::atomic<bool> muted{false};float level=1,step=1.f/240;
};
}
