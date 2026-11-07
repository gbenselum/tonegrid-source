#pragma once
#include "Modules/Record/RecordModule.h"
class RecordVideoBrowserProbe final:public juce::Component,private juce::Timer {
 class Window final:public juce::DocumentWindow {
 public:Window():DocumentWindow("ToneGrid RECORD video check",juce::Colours::black,DocumentWindow::closeButton){setUsingNativeTitleBar(true);}void closeButtonPressed()override{juce::JUCEApplication::getInstance()->systemRequestedQuit();}
 };
public:
 RecordVideoBrowserProbe(juce::File destination,std::function<void(bool)> complete):output(std::move(destination)),finished(std::move(complete)){
  using namespace juce;root=File::getSpecialLocation(File::tempDirectory).getChildFile("ToneGrid-record-video-"+Uuid().toString());
  tonegrid::core::ToneService tone;tone.capture=[] {return String("<PedalRig version=\"17\"/>");};tone.restore=[](const String&){return Result::ok();};tone.available=[] {return true;};tone.deviceRate=[] {return 48000.;};
  module=std::make_unique<tonegrid::record::RecordModule>(std::move(tone),root.getChildFile("session"),File{},root.getChildFile("WebView2"));addAndMakeVisible(module->view());setSize(1200,790);window.setContentNonOwned(this,true);window.centreWithSize(1200,820);window.setVisible(true);original=window.getBounds();module->view().setBounds(getLocalBounds());module->recordController().audio().deviceStarted(48000);module->activated();lastFeed=began=Time::getMillisecondCounterHiRes();stageBegan=began;startTimer(10);
 }
 ~RecordVideoBrowserProbe()override{stopTimer();module->shutdown();window.clearContentComponent();}
 void resized()override{if(module)module->view().setBounds(getLocalBounds());}
private:
 void check(bool value,const juce::String& text){ok&=value;history+=(value?"PASS ":"FAIL ")+text+"\n";}
 void next(int value){stage=value;stageBegan=juce::Time::getMillisecondCounterHiRes();}
 void feed(){
  using namespace juce;auto& audio=module->recordController().audio();const double now=Time::getMillisecondCounterHiRes();const auto frames=juce::int64(std::floor((now-lastFeed)*48));if(frames<=0)return;lastFeed+=frames/48.;
  for(juce::int64 at=0;at<frames;at+=512){const int n=int(std::min<juce::int64>(512,frames-at));AudioBuffer<float> raw(1,n),out(2,n);for(int i=0;i<n;++i)raw.setSample(0,i,.01f*std::sin(float(i+at)*.1f));const auto* input=audio.input(raw.getArrayOfReadPointers(),1,n);for(int c=0;c<2;++c)out.copyFrom(c,0,input[0],n);audio.output(out.getArrayOfWritePointers(),2,n);}
 }
 void timerCallback()override{
  using namespace juce;if(done)return;feed();const double now=Time::getMillisecondCounterHiRes();auto& controller=module->recordController();auto& view=module->recordView();
  if(now-began>180000||now-stageBegan>25000){history+="TIMEOUT stage "+String(stage)+" "+view.status.getText()+"\n";finish(false);return;}
  if(stage==0){if(controller.isBusy())return;view.youtubeURL.setText("https://www.youtube.com/watch?v=M7lc1UVf-VE",false);view.loadYoutube.onClick();next(1);return;}
  if(pending||++ticks%10!=0)return;pending=true;
  view.youtube.inspect([safe=Component::SafePointer<RecordVideoBrowserProbe>(this)](var value){if(!safe||safe->done)return;safe->pending=false;safe->inspect(value);});
 }
 void inspect(const juce::var& value){
  using namespace juce;auto& controller=module->recordController();auto& audio=controller.audio();auto& view=module->recordView();const double now=Time::getMillisecondCounterHiRes(),time=double(value["time"]);const int state=int(value["state"]);last=JSON::toString(value,true);
  if(int(value["error"])!=0){history+="YouTube error "+last+"\n";finish(false);return;}
  if(bool(value["blocked"])){history+="AUTOPLAY BLOCKED: trusted click required; "+last+"\n";finish(false);return;}
  const bool running=audio.state()==tonegrid::core::RecordAudioEngine::playing&&!audio.isSyncHeld()&&state==1;
  if(running&&!bool(value["starting"])&&now-stageBegan>600){const double error=double(audio.position())/controller.model().rate-time;worst=juce::jmax(worst,std::abs(error));sum+=std::abs(error);++samples;}
  switch(stage){
   case 1:if(bool(value["ready"])&&!bool(value["starting"])&&state==2&&!controller.isBusy()){view.play.onClick();next(2);}break;
   case 2:if(running&&now-stageBegan>10000){check(std::abs(double(audio.position())/48000-time)<.18,"Start at zero follows observed video time over ten seconds");view.youtube.evaluateForTest("player.pauseVideo()");next(3);}break;
   case 3:if(audio.state()==tonegrid::core::RecordAudioEngine::stopped&&!controller.isBusy()&&state==2){check(std::abs(double(audio.position())/48000-time)<.18,"Pausing directly in YouTube stops the audio timeline");view.timeline.onSeek(juce::int64(std::llround(12.651*48000)));next(4);}break;
   case 4:if(!bool(value["starting"])&&state==2&&std::abs(time-12.651)<.35){check(audio.position()==juce::int64(std::llround(12.651*48000)),"Paused fractional timeline selection preserves its exact audio cursor");view.play.onClick();next(5);}break;
   case 5:if(running&&now-stageBegan>1400){check(std::abs(double(audio.position())/48000-time)<.18,"Resume after a fractional seek stays synchronized");view.youtube.evaluateForTest("player.seekTo(25.275,true)");next(6);}break;
   case 6:if(running&&time>24&&now-stageBegan>1400){check(std::abs(double(audio.position())/48000-time)<.18,"Seeking inside YouTube repositions and resumes the audio timeline");view.goStart.onClick();next(7);}break;
   case 7:if(running&&time<3&&now-stageBegan>700){check(std::abs(double(audio.position())/48000-time)<.18,"Go to Start resets both clocks after multiple pause/seek cycles");generation=view.youtube.generation();view.youtube.sendFullscreenEventForTest(true);next(8);}break;
   case 8:if(view.youtube.isFullScreen()&&running){const auto* display=Desktop::getInstance().getDisplays().getDisplayForRect(window.getScreenBounds());check(display&&window.getScreenBounds()==display->safeAreaInsets.subtractedFrom(display->totalArea)&&int(value["width"])==window.getWidth()&&int(value["height"])==window.getHeight()&&view.youtube.generation()==generation,"Fullscreen uses the same live browser and covers the monitor");view.youtube.escapeFullscreenForTest();next(9);}break;
   case 9:if(!view.youtube.isFullScreen()&&running&&window.getBounds()==original){check(true,"Escape restores the original window and continues video playback");module->deactivated();module->view().setVisible(false);check(!view.youtube.hasPlayer()&&!audio.enabled(),"Leaving RECORD releases the player and stops hidden audio");module->view().setVisible(true);module->activated();next(10);}break;
   case 10:if(bool(value["ready"])&&!bool(value["starting"])&&state==2&&!controller.isBusy()){check(view.youtube.generation()>generation,"Returning to RECORD creates a new playable browser session");view.play.onClick();next(11);}break;
   case 11:if(running&&now-stageBegan>1400){check(std::abs(double(audio.position())/48000-time)<.18,"Playback remains synchronized after returning to RECORD");if(++returns<3){generation=view.youtube.generation();module->deactivated();module->view().setVisible(false);module->view().setVisible(true);module->activated();next(10);}else{view.stop.onClick();next(12);}}break;
   case 12:if(!controller.isBusy()&&audio.state()==tonegrid::core::RecordAudioEngine::stopped){view.record.onClick();next(13);}break;
   case 13:if(!controller.isBusy()&&audio.state()==tonegrid::core::RecordAudioEngine::recording&&!audio.isSyncHeld()&&now-stageBegan>1400){view.youtube.evaluateForTest("player.pauseVideo()");next(14);}break;
   case 14:if(!controller.isBusy()&&audio.state()==tonegrid::core::RecordAudioEngine::stopped){check(!controller.model().clips.empty()&&controller.model().pendingTake.isEmpty(),"Pausing a video during recording finalizes a recoverable take");view.play.onClick();view.stop.onClick();next(15);}break;
   case 15:if(!controller.isBusy()&&now-stageBegan>1000){check(audio.state()==tonegrid::core::RecordAudioEngine::stopped&&state==2,"Stop during asynchronous playback preparation cannot restart from a delayed ready message");view.play.onClick();module->deactivated();module->view().setVisible(false);next(16);}break;
   case 16:if(!controller.isBusy()&&now-stageBegan>1000){module->view().setVisible(true);module->activated();next(17);}break;
   case 17:if(bool(value["ready"])&&!bool(value["starting"])&&!controller.isBusy()&&now-stageBegan>1000){check(audio.state()==tonegrid::core::RecordAudioEngine::stopped&&state==2,"Leaving during playback preparation returns to a paused playable session");check(worst<.25&&samples>15,"Measured API/sample-clock error stayed below 250 ms across native playback controls");finish(ok);}break;
  }
 }
 void finish(bool result){if(done)return;done=true;stopTimer();module->deactivated();window.setVisible(false);output.getParentDirectory().createDirectory();output.replaceWithText(juce::String(result?"PASS":"FAIL")+" RECORD real YouTube synchronization/fullscreen/lifecycle\n"+history+"MEASURE samples="+juce::String(samples)+" mean_abs_ms="+juce::String(samples?1000*sum/samples:0,3)+" worst_abs_ms="+juce::String(1000*worst,3)+"\nLast "+last+"\n");if(finished)finished(result);}
 juce::File root,output;std::function<void(bool)> finished;Window window;std::unique_ptr<tonegrid::record::RecordModule> module;juce::Rectangle<int> original;juce::String history,last;
 int stage=0,ticks=0,returns=0,samples=0;unsigned generation=0;double began=0,stageBegan=0,lastFeed=0,sum=0,worst=0;bool pending=false,done=false,ok=true;
};
