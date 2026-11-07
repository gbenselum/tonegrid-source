#pragma once
#include "Core/RecordAudioEngine.h"
#include "Core/ProcessingGraph.h"
#include "Modules/Record/Model/RecordSession.h"
#include "Modules/Record/YouTube/YouTubeReference.h"
#include "Modules/Record/Audio/OfflineGuitarRenderer.h"
#include "Modules/Record/UI/RecordView.h"
#include "RealtimeAllocationProbe.h"
#include "Record141Performance.h"
#include <chrono>
namespace record141Tests {
using namespace juce;using namespace tonegrid;
inline bool writeWav(const File& file,double rate,int channels,int frames,float level) {
 WavAudioFormat wav;auto stream=file.createOutputStream();std::unique_ptr<AudioFormatWriter> writer(stream?wav.createWriterFor(stream.get(),rate,(unsigned)channels,32,{},0):nullptr);if(!writer)return false;stream.release();
 AudioBuffer<float> audio(channels,frames);for(int c=0;c<channels;++c)for(int i=0;i<frames;++i)audio.setSample(c,i,level*float(std::sin(i*.067+c*.4)));return writer->writeFromAudioSampleBuffer(audio,0,frames);
}
struct GainRenderer:core::IRenderProcessor {
 float gain;explicit GainRenderer(float v):gain(v){}void process(AudioBuffer<float>& b)override{b.applyGain(gain);}
};
struct DelayedRenderer:core::IRenderProcessor {
 std::array<float,17> line{};int index=0;int latencySamples()const noexcept override{return 17;}
 void process(AudioBuffer<float>& b)override{for(int i=0;i<b.getNumSamples();++i){const float out=line[size_t(index)];line[size_t(index)]=b.getSample(0,i)*2;if(++index==17)index=0;b.setSample(0,i,out);b.setSample(1,i,out);}}
};
struct WaveReceiver:AudioFormatWriter::ThreadedWriter::IncomingDataReceiver {
 Thread::ThreadID audioThread=Thread::getCurrentThreadId();std::atomic<bool> inAudio{false},violation{false};std::atomic<int> blocks{0};
 void reset(int,double,int64)override{}
 void addBlock(int64,const AudioBuffer<float>&,int,int)override{++blocks;if(Thread::getCurrentThreadId()==audioThread&&inAudio.load())violation=true;}
};
struct InspectEncoder:core::IMp3Encoder {
 AudioBuffer<float> audio;double rate=0;
 Result encode(const File& wav,const File&,const std::atomic<bool>&)override {
  AudioFormatManager f;f.registerBasicFormats();std::unique_ptr<AudioFormatReader> r(f.createReaderFor(wav));if(!r)return Result::fail("Missing render WAV");rate=r->sampleRate;audio.setSize(2,(int)r->lengthInSamples);r->read(&audio,0,audio.getNumSamples(),0,true,true);return Result::ok();
 }
};
inline bool run(const File& resources,const File& encoder,const File& evidence,String& report) {
 bool ok=true;int checks=0;auto check=[&](bool pass,const String& text){++checks;ok&=pass;report+=String(pass?"PASS ":"FAIL ")+text+"\n";};
 const auto temp=File::getSpecialLocation(File::tempDirectory),root=temp.getChildFile("ToneGrid-record141-"+Uuid().toString());root.createDirectory();ScopeGuard cleanup{[&]{if(root.isAChildOf(temp))root.deleteRecursively();}};
 std::vector<core::Segment> clips{{"old.wav",0,6000,0}};core::eraseInterval(clips,1000,1500);
 check(clips.size()==2&&clips[0].length==1000&&clips[1].start==1500&&clips[1].sourceStart==1500&&clips[1].length==4500,"Delete creates silence without ripple or changing later source offsets");
 clips={{"old.wav",0,6000,0}};core::punch(clips,{"new.wav",2000,1200,0});
 check(clips.size()==3&&clips[0].start==0&&clips[0].length==2000&&clips[1].file=="new.wav"&&clips[1].start==2000&&clips[1].length==1200&&clips[2].start==3200&&clips[2].sourceStart==3200&&clips[2].length==2800,"Punch preserves old audio before 20 and after 32, replacing only 20-32");
 core::punch(clips,{"third.wav",2500,1000,0});check(clips.size()==4&&clips.back().start==3500&&clips.back().sourceStart==3500,"Repeated punches retain source ownership and resume the correct old suffix");
 record::RecordSession state;state.rate=48000;state.clips=clips;state.tone="<PedalRig version=\"17\" name=\"opaque canonical tone\"/>";state.videoId="dQw4w9WgXcQ";state.videoURL="https://www.youtube.com/watch?v="+state.videoId;state.youtubeStart=42.5;state.referenceStart=2500;state.cursor=3000;state.guitarVolume=.8f;state.backingMute=true;
 record::RecordSessionStore store(root);check(store.save(state).wasOk(),"Record session metadata saves atomically using relative source paths");record::RecordSession restored;
 check(store.load(restored).wasOk()&&restored.clips.size()==4&&restored.tone==state.tone&&restored.youtubeStart==42.5&&restored.referenceStart==2500&&restored.guitarVolume==.8f&&restored.backingMute,"Session reload retains edit map, canonical tone snapshot, reference offset and track controls");
 auto bad=nlohmann::json::parse(root.getChildFile("session.json").loadFileAsString().toStdString());bad["clips"][0]["file"]="../outside.wav";root.getChildFile("session.json").replaceWithText(String(bad.dump()));
 check(store.load(restored).failed()&&restored.tone==state.tone,"Malformed traversal manifest rejects atomically without deleting source files");store.save(state);
 check(!core::safeRelative("C:/private.wav")&&!core::safeRelative("x/../../private.wav")&&!core::safeRelative("x\\private.wav")&&core::safeRelative("guitar-take.wav"),"Recording paths are portable, relative and contained");
 for(const auto& url:StringArray{"https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=anything","https://youtu.be/dQw4w9WgXcQ?t=12","https://m.youtube.com/shorts/dQw4w9WgXcQ","https://youtube.com/embed/dQw4w9WgXcQ","https://www.youtube.com/live/dQw4w9WgXcQ"})check(record::youtubeId(url)=="dQw4w9WgXcQ","YouTube ID parsing: "+url);
 for(const auto& url:StringArray{"https://youtube.com.evil.test/watch?v=dQw4w9WgXcQ","file:///youtube.com/watch?v=dQw4w9WgXcQ","https://evil.test/?v=dQw4w9WgXcQ","javascript:alert(1)","https://youtube.com/watch?v=<script>","https://youtu.be/short"})check(record::youtubeId(url).isEmpty(),"Reject invalid/non-YouTube URL: "+url);
 WaveReceiver waveform;core::RecordAudioEngine engine;engine.setPreviewSink(&waveform);engine.deviceStarted(48000);engine.setEnabled(true);AudioBuffer<float> raw(2,512),monitor(2,512);int phase=0;
 auto feed=[&](int blocks,float gain=3.f,bool pace=true){for(int block=0;block<blocks;++block){for(int i=0;i<512;++i){raw.setSample(0,i,.1f*float(std::sin((phase+i)*.03)));raw.setSample(1,i,-.8f);}phase+=512;const auto* routed=engine.input(raw.getArrayOfReadPointers(),2,512);for(int c=0;c<2;++c)for(int i=0;i<512;++i)monitor.setSample(c,i,routed[c][i]*gain);engine.output(monitor.getArrayOfWritePointers(),2,512);if(pace&&block%4==0)Thread::sleep(1);}};
 const auto take=root.getChildFile("di.wav");check(engine.record(take,48000,48000).wasOk(),"DI capture starts a new mono 32-bit float WAV at device rate");
 raw.clear();raw.setSample(0,0,.125f);waveform.inAudio=true;realtimeProbe::begin();const auto* routed=engine.input(raw.getArrayOfReadPointers(),2,512);monitor.copyFrom(0,0,routed[0],512,4.f);monitor.copyFrom(1,0,routed[1],512,4.f);engine.output(monitor.getArrayOfWritePointers(),2,512);const auto allocations=realtimeProbe::end();waveform.inAudio=false;
 check(allocations==0&&monitor.getSample(0,0)==.5f,"Recording callback allocates nothing and monitors processed guitar, with no parallel raw DI");
 feed(40);const auto frames=engine.stop();AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(take));
 check(reader&&reader->numChannels==1&&reader->usesFloatingPointData&&reader->bitsPerSample==32&&reader->sampleRate==48000&&reader->lengthInSamples==frames&&frames==41*512,"DI file is mono float and sample count matches the audio counter exactly");
 check(waveform.blocks>0&&!waveform.violation,"Live waveform generation runs on writer/control workers, never in the audio callback");
 AudioBuffer<float> readback(1,512);if(reader)reader->read(&readback,0,512,0,true,false);check(readback.getSample(0,0)==.125f,"Stored DI is captured before input trim/gate/Pitch/pedals/amp/post effects");reader.reset();
 const auto originalHash=SHA256(take).toHexString();std::unique_ptr<core::TimelineAudio> audio;
 check(core::TimelineAudio::load(root,48000,{{"di.wav",0,frames,0}},"",audio).wasOk(),"Recorded DI prepares off the audio thread for timeline playback");engine.setTimeline(std::move(audio));engine.seek(512);engine.play();
 realtimeProbe::begin();feed(1,2,false);const auto playbackAlloc=realtimeProbe::end();const auto toneA=monitor.getMagnitude(0,512);engine.seek(512);engine.play();feed(1,4,false);const auto toneB=monitor.getMagnitude(0,512);engine.stop();
 check(playbackAlloc==0&&toneB>toneA*1.9f&&SHA256(take).toHexString()==originalHash,"Re-amping changes processed output while keeping stored DI byte-identical, with zero playback allocations");
 check(writeWav(root.getChildFile("backing.wav"),44100,2,44100,.15f),"Local stereo backing fixture is created at a different source rate");const auto backingHash=SHA256(root.getChildFile("backing.wav")).toHexString();
 check(core::TimelineAudio::load(root,48000,{{"di.wav",0,frames,0}},"backing.wav",audio).wasOk()&&audio->backingEnd==48000,"Backing track resamples non-destructively and starts at timeline 00:00");
 std::vector<core::Segment> trailing{{"di.wav",0,frames,0}};core::eraseInterval(trailing,frames/2,frames);std::unique_ptr<core::TimelineAudio> silentSuffix;
 check(core::TimelineAudio::load(root,48000,trailing,"",silentSuffix,frames).wasOk()&&silentSuffix->guitarEnd==frames&&silentSuffix->guitar(frames*.75)==0,"Deleting the last region preserves its silent timeline duration, without shortening playback/export");
 InspectEncoder guitar,mix,muted;GainRenderer processor(2);std::atomic<bool> cancelled{false};std::atomic<float> progress{0};
 auto render=[&](InspectEncoder& e,bool mixed,bool gm,bool bm){return record::renderExport(*audio,processor,e,root.getChildFile("inspect.mp3"),mixed,.8f,.7f,gm,bm,cancelled,progress);};
 check(render(guitar,false,false,false).wasOk()&&guitar.audio.getNumSamples()==frames,"Export Guitar renders only DI through processing and uses the guitar duration");
 DelayedRenderer delayed;InspectEncoder compensated;
 check(record::renderExport(*audio,delayed,compensated,root.getChildFile("latency.mp3"),false,.8f,.7f,false,false,cancelled,progress).wasOk()&&compensated.audio.getNumSamples()==frames,"Offline export compensates non-block-aligned processor latency and retains exact duration");
 float maximumDifference=0;for(int c=0;c<2;++c)for(int i=0;i<frames;++i)maximumDifference=jmax(maximumDifference,std::abs(guitar.audio.getSample(c,i)-compensated.audio.getSample(c,i)));
 check(maximumDifference<1e-6f,"Latency compensation aligns processed guitar sample-for-sample before mixing backing audio");
 check(render(mix,true,false,false).wasOk()&&mix.audio.getNumSamples()==48000&&mix.audio.getMagnitude(30000,500)>.01f,"Export Mix includes only the local backing and uses the full local timeline duration");
 check(render(muted,true,true,true).wasOk()&&muted.audio.getMagnitude(0,muted.audio.getNumSamples())==0,"Both exports respect track mute logically; muted tracks contribute silence");
 check(SHA256(take).toHexString()==originalHash&&SHA256(root.getChildFile("backing.wav")).toHexString()==backingHash,"Playback/export leave DI and imported backing bytes unchanged");
 core::LameMp3Encoder lame(encoder);const auto mp3=root.getChildFile(String::fromUTF8("été guitar mix.mp3"));
 check(record::renderExport(*audio,processor,lame,mp3,true,1,.5f,false,false,cancelled,progress).wasOk(),"Real pinned LAME executable produces an MP3 from the rendered mix");
 std::unique_ptr<AudioFormatReader> mp3reader(formats.createReaderFor(mp3));check(mp3reader&&mp3reader->lengthInSamples>=48000&&mp3reader->lengthInSamples<51000&&mp3reader->numChannels==2,"Exported MP3 decodes as valid stereo with the expected duration plus codec padding");mp3reader.reset();
 std::unique_ptr<core::TimelineAudio> importedMP3;check(core::TimelineAudio::load(root,48000,{},mp3.getFileName(),importedMP3).wasOk()&&importedMP3->backingEnd>=48000,"Local MP3 backing uses the format-independent timeline decoder");
 const auto sourceMP3Hash=SHA256(mp3).toHexString();check(record::renderExport(*importedMP3,processor,lame,mp3,true,1,1,false,false,cancelled,progress).failed()&&SHA256(mp3).toHexString()==sourceMP3Hash,"Export cannot overwrite an imported session source");
 cancelled=true;const auto oldExportHash=SHA256(mp3).toHexString();check(record::renderExport(*audio,processor,lame,mp3,true,1,1,false,false,cancelled,progress).failed()&&SHA256(mp3).toHexString()==oldExportHash&&SHA256(take).toHexString()==originalHash,"Cancellation preserves existing exports and every recording source");cancelled=false;
 core::LameMp3Encoder missing(root.getChildFile("not-installed"));check(missing.encode(take,mp3,cancelled).failed()&&SHA256(mp3).toHexString()==oldExportHash,"Encoder failure preserves the old destination and session source");
 auto tempFiles=root.findChildFiles(File::findFiles,false,"*.tmp");check(tempFiles.isEmpty(),"Temporary render/encode files are cleaned without touching recordings");
 engine.setTimeline(std::move(audio));engine.deviceStarted(44100);engine.seek(4800);engine.play();feed(10,1,false);const auto expected=4800+(int64)std::floor(5120.*48000/44100);
 check(engine.position()==expected,"Timeline position derives from device sample counts across sample-rate conversion");engine.stop();
 {
  core::RecordAudioEngine first;std::unique_ptr<core::TimelineAudio> empty;core::TimelineAudio::load(root,48000,{},"",empty);first.setTimeline(std::move(empty));first.deviceStarted(44100);first.setEnabled(true);first.record(root.getChildFile("first-44100.wav"),44100,44100,first.deviceGeneration());first.input(raw.getArrayOfReadPointers(),2,512);first.output(monitor.getArrayOfWritePointers(),2,512);
  check(first.position()==512&&first.stop()==512,"First 44.1 kHz take uses its new session rate despite a pre-existing empty 48 kHz cache");
  const auto oldGeneration=first.deviceGeneration();first.deviceStopped();first.deviceStarted(48000);const auto target=root.getChildFile("changed-device.wav");check(first.record(target,44100,44100,oldGeneration).failed()&&!target.exists(),"Device changes during asynchronous record preparation reject the stale start before creating a take");
 }
 bool rapid=true;for(int i=0;i<20;++i){engine.seek(0);const auto f=root.getChildFile("rapid-"+String(i)+".wav");rapid&=engine.record(f,48000,48000).wasOk();feed(1,1,false);rapid&=engine.stop()==512;}check(rapid,"Twenty rapid start/stop cycles finalize independent take files safely");
 engine.seek(0);engine.record(root.getChildFile("device-stop.wav"),48000,48000);feed(3);engine.deviceStopped();engine.deviceStarted(48000);const auto stopped=engine.stop();check(engine.deviceLost&&engine.state()==core::RecordAudioEngine::stopped&&stopped==1536,"Fast device stop/restart keeps the finalization request and preserves the captured part");engine.deviceStarted(48000);
 {core::RecordAudioEngine small(256);small.deviceStarted(48000);small.setEnabled(true);small.record(root.getChildFile("overflow.wav"),48000,48000);small.input(raw.getArrayOfReadPointers(),2,128);small.output(monitor.getArrayOfWritePointers(),2,128);small.input(raw.getArrayOfReadPointers(),2,512);small.output(monitor.getArrayOfWritePointers(),2,512);const auto saved=small.stop();check(small.failed&&saved==128,"Bounded recording FIFO rejects an oversized handoff and preserves the prior valid audio");}
 // 30 minutes of device time, exercised faster than realtime. Pace disk handoff.
 engine.setTimeline({});engine.seek(0);engine.deviceStarted(8000);engine.record(root.getChildFile("long.wav"),8000,8000);const auto began=Time::getMillisecondCounterHiRes();feed(28125,1,true);const bool stable=!engine.failed;const auto longFrames=engine.stop();
 check(stable&&longFrames==14400000,"Thirty-minute DI take survives sustained buffered writing without a fixed ten-minute limit");report+="MEASURE long_DI_virtual_seconds=1800 wall_ms="+String(Time::getMillisecondCounterHiRes()-began,2)+"\n";
 // Callback cost excludes disk work and uses a representative immutable timeline.
 core::TimelineAudio::load(root,48000,{{"di.wav",0,frames,0}},"backing.wav",audio);engine.setTimeline(std::move(audio));engine.deviceStarted(48000);engine.seek(0);engine.play();
 auto bench=[&](const String& label,bool playing){const auto t=Time::getMillisecondCounterHiRes();for(int i=0;i<1000;++i){if(playing&&engine.state()==core::RecordAudioEngine::stopped){engine.seek(0);engine.play();}feed(1,1,false);}report+="MEASURE "+label+"_us_per_512="+String((Time::getMillisecondCounterHiRes()-t),3)+"\n";};bench("DI_and_backing_callback_including_input_fixture",true);engine.stop();bench("idle_callback_including_input_fixture",false);
 // Real canonical processing instances, using reference resources and Temp personal data only.
 const auto starter=resources.getChildFile("StarterPresets/slot-001.tonexrig");auto xml=XmlDocument::parse(starter);String error;
 core::RenderResources rr{resources.getChildFile("Models"),resources.getChildFile("FactoryAmps"),resources.getChildFile("FactoryCabinetIR"),root.getChildFile("personal"),false,{}};
 std::unique_ptr<core::IRenderProcessor> realA,realB;
 const auto preparationBegan=Time::getMillisecondCounterHiRes();
 if(xml){realA=core::createRenderProcessor(xml->toString(),48000,rr,error);if(auto* amp=xml->getChildByName("AmpSource"))amp->setAttribute("gainDb",6.);realB=core::createRenderProcessor(xml->toString(),48000,rr,error);}
 report+="MEASURE two_render_instances_prepare_ms="+String(Time::getMillisecondCounterHiRes()-preparationBegan,3)+"\n";
 check(realA&&realB,"Core builds two independent render instances from canonical preset XML: "+error);
 if(realA&&realB){std::unique_ptr<core::TimelineAudio> recordedDI;core::TimelineAudio::load(root,48000,{{"di.wav",0,frames,0}},"",recordedDI);double difference=0,energy=0;AudioBuffer<float> a(2,512),b(2,512);const auto t=Time::getMillisecondCounterHiRes();for(int block=0;block<150;++block){for(int i=0;i<512;++i)for(int c=0;c<2;++c){const float v=recordedDI->guitar(double((block*512+i)%frames));a.setSample(c,i,v);b.setSample(c,i,v);}realA->process(a);realB->process(b);for(int i=0;i<512;++i){difference+=std::abs(a.getSample(0,i)-b.getSample(0,i));energy+=std::abs(a.getSample(0,i));}}check(difference>1e-5&&energy>1e-5&&SHA256(take).toHexString()==originalHash,"Actual canonical pedal/amp/IR/post chain re-amps unchanged DI differently under Tone A/B");report+="MEASURE two_full_tones_150_blocks_ms="+String(Time::getMillisecondCounterHiRes()-t,3)+"\n";}
 if(realA)check(record141Performance::run(root,*realA,report),"All seven software performance modes render finite audio without a recording handoff fault");

 // Two-guitar update: persistent dry/frozen states, separate mixing and exact range output.
 {
  record::RecordSession pair;pair.clips={{"di.wav",0,frames,0}};pair.guitarLength=frames;pair.tone="rhythm-tone";
  std::unique_ptr<core::TimelineAudio> dry;core::TimelineAudio::load(root,48000,pair.clips,"",dry,frames);
  GainRenderer rhythm(2);std::atomic<bool> abort{false};std::atomic<float> percent{0};
  const auto frozen=root.getChildFile("frozen-rhythm.wav");
  check(record::renderWave(*dry,rhythm,frozen,false,1,0,false,true,abort,percent).wasOk(),"Render produces a separate processed stereo file at unity track volume");
  check(SHA256(take).toHexString()==originalHash,"Render preserves the exact original dry take bytes");
  pair.rendered=frozen.getFileName();pair.editableTrack=pair.activeTrack=1;pair.second.clips={{"di.wav",0,frames,0}};pair.second.guitarLength=frames;pair.second.tone="solo-tone";pair.pendingTrack=1;
  auto pairFolder=root.getChildFile("pair");pairFolder.createDirectory();take.copyFileTo(pairFolder.getChildFile("di.wav"));frozen.copyFileTo(pairFolder.getChildFile(frozen.getFileName()));
  record::RecordSessionStore pairStore(pairFolder);record::RecordSession restored;
  check(pairStore.save(pair).wasOk()&&pairStore.load(restored).wasOk()&&restored.editableTrack==1&&restored.rendered==frozen.getFileName()&&restored.second.tone=="solo-tone"&&restored.clips.size()==1,"Schema 2 restores independent tones, rendered rhythm, editable solo and both dry sources");
  std::unique_ptr<core::TimelineAudio> combined;check(record::loadSessionAudio(pairFolder,pair,combined).wasOk()&&combined->rendered.size()==1,"Playback prepares one editable DI chain plus the other rendered guitar");
  if(combined){
   const auto hash=SHA256(frozen).toHexString();GainRenderer solo(3);InspectEncoder output;
   const auto result=record::renderExport(*combined,solo,output,root.getChildFile("two-guitars.mp3"),true,.4f,0,false,true,abort,percent,0,0,{.6f,1});
   double errorSum=0;for(int i=512;i<juce::jmin<int>(frames-512,output.audio.getNumSamples());++i)errorSum+=std::abs(output.audio.getSample(0,i)-(combined->guitar(i)*3*.4f+combined->sample(pair.rendered,i,0)*.6f));
   check(result.wasOk()&&output.audio.getNumSamples()==frames&&errorSum<.001,"Both guitars mix independently with exact per-track volume, without processing the rendered guitar twice");
   core::RecordAudioEngine pairEngine;std::unique_ptr<core::TimelineAudio> pairAudio;record::loadSessionAudio(pairFolder,pair,pairAudio);pairEngine.setTimeline(std::move(pairAudio));pairEngine.deviceStarted(48000);pairEngine.setEnabled(true);pairEngine.controls(.4f,0,false,true);pairEngine.renderedControls(.6f,0);pairEngine.seek(512);pairEngine.play();AudioBuffer<float> input(2,512),heard(2,512);input.clear();
   realtimeProbe::begin();const auto* di=pairEngine.input(input.getArrayOfReadPointers(),2,512);for(int c=0;c<2;++c)heard.copyFrom(c,0,di[c],512,3.f);pairEngine.output(heard.getArrayOfWritePointers(),2,512);const auto pairAllocations=realtimeProbe::end();double monitorError=0;for(int i=0;i<512;++i)monitorError+=std::abs(heard.getSample(0,i)-output.audio.getSample(0,i+512));pairEngine.stop();
   check(pairAllocations==0&&monitorError<.001,"Editable solo plus rendered rhythm playback allocates nothing and matches the independent export mix");
   GainRenderer changed(7);InspectEncoder changedOutput;record::renderExport(*combined,changed,changedOutput,root.getChildFile("changed.mp3"),true,1,0,false,true,abort,percent,0,0,{1,1});
   check(SHA256(frozen).toHexString()==hash&&SHA256(take).toHexString()==originalHash,"Changing the solo tone cannot alter the rendered rhythm or original DI");
   GainRenderer rangeTone(3);InspectEncoder range;const auto from=std::min<juce::int64>(512,frames/4),to=std::min<juce::int64>(2048,frames/2);
   auto selected=record::renderExport(*combined,rangeTone,range,root.getChildFile("range.mp3"),true,.4f,0,false,true,abort,percent,from,to,{.6f,1});double rangeError=0;
   for(int i=0;i<range.audio.getNumSamples();++i)rangeError+=std::abs(range.audio.getSample(0,i)-output.audio.getSample(0,int(from)+i));
   check(selected.wasOk()&&range.audio.getNumSamples()==to-from&&rangeError<.001,"Export time selection crops the complete mix exactly while warming effects from the timeline start");
   GainRenderer muteTone(3);InspectEncoder muted;record::renderExport(*combined,muteTone,muted,root.getChildFile("muted-pair.mp3"),true,1,0,true,true,abort,percent,0,0,{0,0});
   check(muted.audio.getMagnitude(0,muted.audio.getNumSamples())==0,"Track mutes apply to editable and rendered guitars in exports");
  }
  const auto dryCount=pair.clips.size();record::commitTake(pair,{"di.wav",1000,100,0});
  check(pair.clips.size()==dryCount&&pair.rendered.isNotEmpty()&&pair.second.clips.size()>=2,"A solo punch changes only Guitar 2 while Guitar 1 stays rendered and intact");
  auto firstJson=nlohmann::json::parse(pairFolder.getChildFile("session.json").loadFileAsString().toStdString());firstJson["schemaVersion"]=1;firstJson["rendered"]="";firstJson.erase("secondGuitar");firstJson.erase("activeTrack");firstJson.erase("editableTrack");firstJson["pendingTake"]["track"]=0;
  pairFolder.getChildFile("session.json").replaceWithText(String(firstJson.dump()));record::RecordSession migrated;
  check(pairStore.load(migrated).wasOk()&&migrated.clips.size()==1&&migrated.second.clips.empty()&&migrated.editableTrack==0,"Original schema-1 sessions migrate into editable Guitar 1 without losing dry audio");
  record::YouTubeWebView player(root.getChildFile("seek-web"),true);player.load("https://youtu.be/dQw4w9WgXcQ");player.seek(12.651);check(std::abs(player.requestedPosition()-12.651)<.000001,"YouTube bridge preserves fractional seek requests without rounding the audio timeline");
  const auto generation=player.generation();player.pause();player.load("https://youtu.be/dQw4w9WgXcQ");check(player.hasPlayer()&&player.generation()>generation,"Returning to RECORD creates a fresh video session and rejects callbacks from its previous browser");player.clear();check(!player.hasPlayer()&&player.requestedPosition()==0,"Remove tears down the YouTube reference and clears its seek state");
 }
 {
  core::RecordAudioEngine gated;gated.deviceStarted(48000);gated.setEnabled(true);std::unique_ptr<core::TimelineAudio> data;core::TimelineAudio::load(root,48000,{},"",data);gated.setTimeline(std::move(data));gated.setReferenceEnd(48000*180);gated.setSyncHeld(true);gated.play();
  AudioBuffer<float> raw(1,512),out(2,512);raw.clear();raw.setSample(0,0,.2f);
  const auto advance=[&](int blocks){for(int n=0;n<blocks;++n){const auto* input=gated.input(raw.getArrayOfReadPointers(),1,512);for(int c=0;c<2;++c)out.copyFrom(c,0,input[0],512);gated.output(out.getArrayOfWritePointers(),2,512);}};
  realtimeProbe::begin();advance(8);const auto allocations=realtimeProbe::end();
  check(gated.state()==core::RecordAudioEngine::playing&&gated.position()==0&&allocations==0,"YouTube readiness hold keeps the sample clock still without realtime allocations");
  gated.alignForReference(12*48000);gated.setSyncHeld(false);advance(4);check(gated.position()==12*48000+2048&&gated.state()==core::RecordAudioEngine::playing,"A video-only reference advances correctly and does not stop because guitar/backing tracks are empty");
  gated.stop();gated.seek(0);gated.setSyncHeld(true);const auto file=root.getChildFile("reference-armed.wav");gated.record(file,48000,48000);advance(8);
  check(gated.position()==0&&gated.alignForReference(6000),"An armed take records no samples before video readiness and can align its initial timeline position");
  gated.setSyncHeld(false);advance(4);const bool rejected=!gated.alignForReference(0);const auto captured=gated.stop();
  check(rejected&&captured==2048&&gated.position()==8048,"Once capture begins, video resynchronization cannot shift or overwrite recorded samples");
  record::VideoDriftGuard drift;check(!drift.observe(.3,0)&&!drift.observe(.3,20)&&drift.observe(.3,40)&&!drift.observe(.3,60)&&!drift.observe(.3,80)&&!drift.observe(.3,100),"Drift correction needs sustained evidence and has a cooldown instead of seeking repeatedly");
 }

 {
  record::TimelineWindow w;w.first=60;w.zoom(.5,90,200);check(w.seconds==30&&w.first==75,"Zoom preserves the anchored song position instead of jumping to another section");w.zoom(2,90,200);check(w.seconds==60&&w.first==60,"Zoom out restores the visible interval around the same anchor");w.pan(-100,200);check(w.first==0,"Hand navigation clamps at song start");w.fit(185);check(w.first==0&&w.seconds==187,"Fit song includes the complete timeline from 00:00");w.first=50;w.manual=true;w.follow(0);const bool stayed=w.first==50;w.reveal(0);check(stayed&&w.first==0&&!w.manual,"Manual navigation is not pulled back by playback; Go to Start restores 00:00 explicitly");
  record::TimelineView nav;nav.setSize(1000,412);record::RecordSession state;state.guitarLength=180*48000;nav.update(state,root);int seeks=0;nav.onSeek=[&](int64){++seeks;};nav.setSelection(48000,144000);
  const auto event=[&](float x,float y,Point<float> down){return MouseEvent(Desktop::getInstance().getMainMouseSource(),{x,y},ModifierKeys(ModifierKeys::leftButtonModifier),1,0,0,0,0,&nav,&nav,Time::getCurrentTime(),down,Time::getCurrentTime(),1,true);};
  nav.mouseDown(event(400,70,{400,70}));nav.mouseDrag(event(200,70,{400,70}));nav.mouseUp(event(200,70,{400,70}));
  check(nav.firstVisibleSeconds()==12&&nav.selectionFrom()==48000&&nav.selectionTo()==144000&&seeks==0,"Dragging the waveform pans the view without seeking, editing or losing the time selection");
  nav.mouseDown(event(100,10,{100,10}));nav.mouseDrag(event(300,10,{100,10}));nav.mouseUp(event(300,10,{100,10}));check(nav.selectionFrom()==18*48000&&nav.selectionTo()==30*48000&&seeks==1,"Dragging the dedicated ruler selects the correct absolute time range after panning");
  nav.panTo(55);nav.revealPosition(0);nav.setPosition(0,true);check(nav.firstVisibleSeconds()==0,"An unchanged cursor at zero still brings the start back into view");
  nav.zoomIn.onClick();const auto small=nav.visibleSeconds();nav.zoomOut.onClick();check(small==30&&nav.visibleSeconds()==60,"Zoom in and out icons operate the timeline viewport");nav.fitSong.onClick();check(nav.firstVisibleSeconds()==0&&nav.visibleSeconds()==182,"Fit song button reveals both retained guitar extent and song start");
  auto icon=Drawable::createFromImageData(ToneGridApplicationAssets::record_headstock_svg,ToneGridApplicationAssets::record_headstock_svgSize);Image transparent(Image::ARGB,256,512,true);bool white=true;int pixels=0;
  if(icon){{Graphics g(transparent);icon->drawWithin(g,transparent.getBounds().toFloat().reduced(12),RectanglePlacement::centred,1);}for(int y=0;y<512;++y)for(int x=0;x<256;++x){const auto c=transparent.getPixelAt(x,y);if(c.getAlpha()>8){++pixels;white&=c.getRed()>245&&c.getGreen()>245&&c.getBlue()>245;}}}
  check(icon&&white&&pixels>10000&&transparent.getPixelAt(0,0).getAlpha()==0,"Original headstock artwork renders in white with a transparent background and clean margins");
  evidence.createDirectory();{FileOutputStream out(evidence.getChildFile("headstock-white.png"));PNGImageFormat().writeImageToStream(transparent,out);}
  Image sheet(Image::ARGB,512,300,true);{Graphics g(sheet);g.fillAll(Colour(0xff14252f));g.drawImageWithin(transparent,16,14,128,256,RectanglePlacement::centred);g.setColour(Colours::white);g.setFont(ui::font(14));int x=180;for(int size:{32,40,48,64}){if(icon)icon->drawWithin(g,{float(x),90,float(size/2),float(size)},RectanglePlacement::centred,1);g.drawText(String(size)+" px",x-8,164,68,24,Justification::centredLeft);x+=78;}}{FileOutputStream out(evidence.getChildFile("headstock-size-check.png"));PNGImageFormat().writeImageToStream(sheet,out);}
 }
 record::RecordView view(root.getChildFile("web"),true);view.setSize(1660,650);state.clips={{"di.wav",0,frames,0}};state.backing="backing.wav";view.setState(state,root);evidence.createDirectory();MemoryOutputStream png;PNGImageFormat().writeImageToStream(view.createComponentSnapshot(view.getLocalBounds()),png);evidence.getChildFile("record-workspace.png").replaceWithData(png.getData(),png.getDataSize());
 check(view.exportGuitar.getButtonText()=="Export time selection"&&view.exportMix.getButtonText()=="Export all"&&view.timeline.getWidth()>800,"RECORD workspace provides two guitars, backing, YouTube and the agreed range/all exports");
 report+=String(checks)+" Record141 checks; "+(ok?"ALL PASSED":"FAILURES")+"\n";return ok;
}
}
