#pragma once
#include "Core/ToneService.h"
#include "Core/AudioTimeline.h"
#include "Core/Mp3Encoder.h"
#include <chrono>
namespace tonegrid::record {
// One renderer for non-destructive track freezing and full/range MP3 exports.
// Start at zero to warm effects, then write only the requested interval.
inline juce::Result renderWave(const core::TimelineAudio& timeline,core::IRenderProcessor& processor,const juce::File& target,bool mix,float guitar,float backing,bool muteGuitar,bool muteBacking,const std::atomic<bool>& cancel,std::atomic<float>& progress,juce::int64 from=0,juce::int64 to=0,std::array<float,2> frozenGains={1,1}) {
 using namespace juce;const auto length=mix?jmax(timeline.guitarEnd,timeline.backingEnd):timeline.guitarEnd;
 if(to<=0)to=length;to=jmin(to,length);if(from<0||to<=from)return Result::fail("There is no audio in this time range.");
 for(const auto& file:timeline.files)if(target==timeline.sourceFolder.getChildFile(file.first))return Result::fail("Choose another export name. This file is a recording-session source.");
 const int latency=processor.latencySamples();if(latency<0||latency>timeline.rate*10)return Result::fail("The plugin reports unsupported export latency.");
 TemporaryFile staged(target);WavAudioFormat wav;auto stream=staged.getFile().createOutputStream();
 std::unique_ptr<AudioFormatWriter> writer(stream?wav.createWriterFor(stream.get(),timeline.rate,2,32,{},0):nullptr);if(!writer)return Result::fail("Cannot create the temporary render audio.");stream.release();
 AudioBuffer<float> audio(2,512);const auto started=std::chrono::steady_clock::now();
 for(int64 offset=0;offset<to+latency;offset+=512){
  if(cancel)return Result::fail("Render cancelled. Your recordings are intact.");const int frames=(int)std::min<juce::int64>(512,to+latency-offset);audio.setSize(2,frames,false,false,true);
  for(int i=0;i<frames;++i){const auto v=timeline.guitar(double(offset+i));audio.setSample(0,i,v);audio.setSample(1,i,v);}processor.process(audio);
  const int first=(int)jlimit<int64>(0,frames,from+latency-offset);
  if(first<frames){
   for(int c=0;c<2;++c)for(int i=first;i<frames;++i){const auto at=double(offset+i-latency);double v=(muteGuitar?0:double(audio.getSample(c,i))*guitar)+(mix&&!muteBacking?double(timeline.sampleBacking(at,c))*backing:0);
    for(const auto& lane:timeline.rendered)v+=double(timeline.sample(lane,at,c))*frozenGains[(size_t)jlimit(0,1,lane.track)];
    if(!std::isfinite(v))return Result::fail("The tone produced invalid audio. Render cancelled.");audio.setSample(c,i,float(v));
   }
   if(!writer->writeFromAudioSampleBuffer(audio,first,frames-first))return Result::fail("Render disk write failed. Recordings were preserved.");
  }
  progress=float(double(offset+frames)/(to+latency))*.85f;
  if(processor.realtimeOnly()){const auto due=started+std::chrono::duration<double>(double(offset+frames)/timeline.rate);while(std::chrono::steady_clock::now()<due&&!cancel)Thread::sleep(1);}
 }
 writer.reset();if(cancel)return Result::fail("Render cancelled. Your recordings are intact.");
 if(!staged.overwriteTargetFileWithTemporary())return Result::fail("Cannot publish the render. Original recordings were preserved.");return Result::ok();
}
inline juce::Result renderExport(const core::TimelineAudio& timeline,core::IRenderProcessor& processor,core::IMp3Encoder& encoder,const juce::File& target,bool mix,float guitar,float backing,bool muteGuitar,bool muteBacking,const std::atomic<bool>& cancel,std::atomic<float>& progress,juce::int64 from=0,juce::int64 to=0,std::array<float,2> frozenGains={1,1}) {
 for(const auto& file:timeline.files)if(target==timeline.sourceFolder.getChildFile(file.first))return juce::Result::fail("Choose another export name. This file is a recording-session source.");
 juce::TemporaryFile intermediate(target.withFileExtension("tonegrid-render.wav"));auto result=renderWave(timeline,processor,intermediate.getFile(),mix,guitar,backing,muteGuitar,muteBacking,cancel,progress,from,to,frozenGains);
 if(result.wasOk())result=encoder.encode(intermediate.getFile(),target,cancel);progress=1;return result;
}
struct SilentRenderer final:core::IRenderProcessor {void process(juce::AudioBuffer<float>& b)override{b.clear();}};
}