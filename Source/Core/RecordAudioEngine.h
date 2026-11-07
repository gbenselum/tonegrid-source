#pragma once
#include "Core/AudioInsert.h"
#include "Core/AudioTimeline.h"
namespace tonegrid::core {
// Control side is serialized by RecordController. Audio sees only bounded buffers,
// atomics and immutable audio. Disk writes run on writerThread, never input/output.
class RecordAudioEngine final:public IAudioInsert {
public:
 enum Mode {stopped,playing,recording};
 explicit RecordAudioEngine(int capacity=262144):writerThread("ToneGrid DI writer"),writerCapacity(juce::jlimit(128,1048576,capacity)){writerThread.startThread();}
 ~RecordAudioEngine()override{stop();writerThread.stopThread(-1);}
 bool enabled()const noexcept override{return active.load();}
 void setEnabled(bool value){if(!value)quiesce();active.store(value);}
 void deviceStarted(double r)noexcept override{deviceRate.store(r);revision.fetch_add(1);}
 void deviceStopped()noexcept override{revision.fetch_add(1);mode.store(stopped);deviceLost=true;}
 juce::uint64 deviceGeneration()const noexcept{return revision.load();}
 void quiesce(){mode.store(stopped);while(readers.load()!=0)juce::Thread::yield();}
 void setTimeline(std::unique_ptr<TimelineAudio> value){quiesce();audio=std::move(value);}
 const TimelineAudio* timeline()const{return audio.get();}
 void seek(juce::int64 frame){quiesce();cursor.store(std::max<juce::int64>(0,frame));}
 bool play(){if(!audio||writer)return false;origin=cursor.load();elapsed=0;mode.store(playing);return true;}
 juce::Result record(const juce::File& file,double rate,double timelineRate,juce::uint64 expectedRevision=std::numeric_limits<juce::uint64>::max()) {
  stop();if(!std::isfinite(rate)||rate<8000||rate>192000||!std::isfinite(timelineRate)||timelineRate<8000||timelineRate>192000)return juce::Result::fail("Choose a supported audio device before recording.");
  const bool guarded=expectedRevision!=std::numeric_limits<juce::uint64>::max();
  if(guarded&&(revision.load()!=expectedRevision||std::abs(deviceRate.load()-rate)>.01))return juce::Result::fail("The audio device changed. Press Record again when it is ready.");
  deviceLost=false;
  if(file.exists())return juce::Result::fail("A take file already exists.");
  auto stream=file.createOutputStream();juce::WavAudioFormat wav;
  auto* format=stream?wav.createWriterFor(stream.get(),rate,1,32,{},0):nullptr;
  if(!format)return juce::Result::fail("Cannot create the DI recording.");stream.release();
  currentFile=file;writer=std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(format,writerThread,writerCapacity);writer->setFlushInterval((int)rate);writer->setDataReceiver(previewSink);
  if(!guarded)deviceRate=rate;sessionRate=timelineRate;origin=cursor.load();elapsed=0;captured=0;failed=false;mode.store(recording);
  if(guarded&&revision.load()!=expectedRevision){stop();return juce::Result::fail("The audio device changed while preparing the take. Press Record again.");}
  return juce::Result::ok();
 }
 juce::int64 stop(){quiesce();const bool finishing=writer!=nullptr;writer.reset();if(finishing){juce::WavAudioFormat wav;auto stream=currentFile.createInputStream();std::unique_ptr<juce::AudioFormatReader> r(stream?wav.createReaderFor(stream.release(),true):nullptr);if(!r||r->lengthInSamples!=captured.load()||currentFile.getSize()<captured.load()*4){failed=true;captured=r?juce::jmin(captured.load(),r->lengthInSamples):0;}}return captured.load();}
 Mode state()const noexcept{return mode.load();}
 juce::int64 position()const noexcept{return cursor.load();}
 void setSyncHeld(bool value)noexcept{syncHeld=value;}
 bool isSyncHeld()const noexcept{return syncHeld.load();}
 void setReferenceEnd(juce::int64 frame)noexcept{referenceEnd=std::max<juce::int64>(0,frame);}
 // Only align playback or a still-empty armed take. A running take is never
 // shifted, stretched or rewritten to follow an external media clock.
 bool alignForReference(juce::int64 frame){const auto previous=mode.load();if(previous==stopped)return false;quiesce();if(previous==recording&&captured.load()!=0){mode=previous;return false;}origin=std::max<juce::int64>(0,frame);elapsed=0;cursor=origin;mode=previous;return true;}
 double currentDeviceRate()const noexcept{return deviceRate.load();}
 void setPlaybackLatency(int frames)noexcept{playbackLatency=juce::jmax(0,frames);}
 void setPreviewSink(juce::AudioFormatWriter::ThreadedWriter::IncomingDataReceiver* sink){quiesce();previewSink=sink;}
 void controls(float guitar,float backing,bool guitarMuted,bool backingMuted)noexcept{guitarGain=guitarMuted?0:guitar;backingGain=backingMuted?0:backing;}
 void renderedControls(float first,float second)noexcept{renderedGains[0]=first;renderedGains[1]=second;}
 const float* const* input(const float* const* raw,int channels,int frames)noexcept override {
  readers.fetch_add(1);blockMode=syncHeld.load()?stopped:mode.load();blockFrames=frames;blockStart=blockMode!=stopped?double(origin)+double(elapsed)*rate()/deviceRate.load():0;
  if(frames>capacity){failed=true;mode=stopped;frames=capacity;}
  for(int i=0;i<frames;++i) {
   float v=channels>0&&raw&&raw[0]?raw[0][i]:0;if(!std::isfinite(v))v=0;
   di[(size_t)i]=v;
   mono[(size_t)i]=blockMode==playing&&audio?audio->guitar(blockStart+i*rate()/deviceRate.load()):v;
  }
  if(blockMode==recording&&writer){const float* source=di.data();if(writer->write(&source,frames))captured.fetch_add(frames);else{failed=true;mode=stopped;blockMode=stopped;}}
  return pointers.data();
 }
 void output(float* const* out,int channels,int frames)noexcept override {
  const float gg=guitarGain.load(),bg=backingGain.load();const double step=blockMode!=stopped?rate()/deviceRate.load():1;
  for(int c=0;c<channels;++c)if(out[c])for(int i=0;i<frames;++i) {
   const float backing=(blockMode!=stopped&&audio&&audio->backing.isNotEmpty())?audio->sampleBacking(blockStart+i*step-(blockMode==playing?playbackLatency.load()*step:0),c)*bg:0;
   float frozen=0;if(blockMode!=stopped&&audio)for(const auto& lane:audio->rendered)frozen+=audio->sample(lane,blockStart+i*step-(blockMode==playing?playbackLatency.load()*step:0),c)*renderedGains[(size_t)juce::jlimit(0,1,lane.track)].load();
   const float v=out[c][i]*gg+backing+frozen;out[c][i]=std::isfinite(v)?v:0;
  }
  if(blockMode!=stopped){elapsed+=frames;cursor=origin+(juce::int64)std::floor(elapsed*step);if(blockMode==playing&&audio&&cursor.load()>=juce::jmax(referenceEnd.load(),juce::jmax(audio->guitarEnd,audio->backingEnd))+std::ceil(playbackLatency.load()*step))mode=stopped;}
  readers.fetch_sub(1);
 }
 static constexpr int capacity=512;
 std::atomic<bool> failed{false},deviceLost{false};
private:
 double rate()const noexcept{return blockMode==recording?sessionRate:audio?audio->rate:sessionRate;}
 juce::TimeSliceThread writerThread;juce::File currentFile;const int writerCapacity;std::atomic<int> playbackLatency{0};juce::AudioFormatWriter::ThreadedWriter::IncomingDataReceiver* previewSink=nullptr;
 std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
 std::unique_ptr<TimelineAudio> audio;
 std::atomic<bool> active{false};std::atomic<Mode> mode{stopped};std::atomic<int> readers{0};
 std::atomic<bool> syncHeld{false};std::atomic<juce::int64> referenceEnd{0};
 std::atomic<juce::int64> cursor{0},captured{0};std::atomic<double> deviceRate{48000};std::atomic<juce::uint64> revision{0};
 std::atomic<float> guitarGain{1},backingGain{1};double sessionRate=48000;
 std::array<std::atomic<float>,2> renderedGains{{1.f,1.f}};
 juce::int64 origin=0,elapsed=0;Mode blockMode=stopped;int blockFrames=0;double blockStart=0;
 std::array<float,capacity> mono{},di{};std::array<const float*,2> pointers{mono.data(),mono.data()};
};
}
