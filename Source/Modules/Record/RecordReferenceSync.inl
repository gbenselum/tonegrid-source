// RECORD's control-thread adapter. The realtime engine owns only a generic
// transport hold and sample clock, and has no browser or YouTube dependency.
void RecordController::cancelReference(){referenceWanted=referenceAwaiting=referenceBuffering=false;referenceDeadline=0;videoDrift.reset();view.youtube.pauseTransport();engine.setSyncHeld(false);}
juce::int64 RecordController::videoFrame(double seconds)const{return std::max<juce::int64>(0,session.referenceStart+std::llround((seconds-session.youtubeStart)*session.rate));}
void RecordController::ensureReference(){
 if(!active||session.videoId.isEmpty()||!view.referenceVisible())return;
 if(!view.youtube.hasPlayer()){view.youtube.load(session.videoURL);view.youtube.seek(juce::jmax(0.,session.youtubeStart+(engine.position()-session.referenceStart)/session.rate),false);}
}
void RecordController::syncReference(bool playing){
 if(session.videoId.isEmpty()){engine.setSyncHeld(false);return;}
 if(playing){view.revealReference();ensureReference();referenceWanted=referenceAwaiting=true;referenceBuffering=false;videoDrift.reset();referenceDeadline=juce::Time::getMillisecondCounterHiRes()+12000;engine.setSyncHeld(true);engine.setReferenceEnd(std::llround(86400*session.rate));setMessage("Synchronizing with YouTube...");}
 else{referenceWanted=referenceAwaiting=referenceBuffering=false;referenceDeadline=0;engine.setSyncHeld(false);}
 const double seconds=juce::jmax(0.,session.youtubeStart+(engine.position()-session.referenceStart)/session.rate);
 view.youtube.seek(seconds,playing);
}
void RecordController::referenceInterrupted(const juce::String& message){
 const bool taking=recordPending;engine.setSyncHeld(true);stop();setMessage(message+(taking?" Your take is being saved. Press Record to continue.":" Press Play to resume."));
}
void RecordController::videoStatus(const VideoStatus& status){
 if(!active||shuttingDown||!loaded||session.videoId.isEmpty())return;
 if(status.duration>0)engine.setReferenceEnd(videoFrame(status.duration));
 if(status.error!=0){if(referenceWanted||recordPending)referenceInterrupted("YouTube could not play (code "+juce::String(status.error)+"). Reload the reference.");else setMessage("YouTube could not play (code "+juce::String(status.error)+"). Reload the reference.");return;}
 if(status.blocked){if(referenceWanted){engine.setSyncHeld(true);referenceDeadline=0;setMessage("Click Play inside the YouTube video to start. Your recording waits until the video is ready.");}return;}
 if(std::abs(status.rate-1.)>.001){if(referenceWanted)referenceInterrupted("Use normal (1x) YouTube speed with RECORD.");return;}
 if(status.reason=="seek"){
  const auto target=videoFrame(status.seconds);const bool resume=referenceWanted||status.state==1;
  engine.setSyncHeld(true);
  if(recordPending||busy){pendingVideoSeek=target;pendingVideoPlay=false;referenceInterrupted("YouTube moved to a new position.");return;}
  cancelReference();engine.seek(target);session.cursor=target;dirty=true;view.timeline.revealPosition(target);
  if(resume)preparePlayback(true);else syncReference(false);return;
 }
 if(referenceWanted){
  if(status.starting)return;
  if(status.state==0){referenceInterrupted("The YouTube reference reached its end.");return;}
  if(status.state==2){referenceInterrupted("YouTube paused.");return;}
  if(status.state==3){
   if(recordPending&&!referenceAwaiting){referenceInterrupted("YouTube is buffering.");return;}
   engine.setSyncHeld(true);referenceBuffering=true;referenceDeadline=juce::Time::getMillisecondCounterHiRes()+12000;return;
  }
  if(status.state!=1)return;
  const auto frame=videoFrame(status.seconds);
  if(referenceAwaiting||referenceBuffering){
   if(engine.state()==core::RecordAudioEngine::stopped){cancelReference();return;}
   const bool initialTake=recordPending&&engine.alignForReference(frame);
   if(!recordPending)engine.alignForReference(frame);
   if(initialTake){recordingStart=frame;session.pendingStart=frame;session.cursor=frame;dirty=true;
    if(!busy){const auto copy=session;busy=true;job=std::async(std::launch::async,[this,copy]{Job result;result.result=store.save(copy);return result;});}
   }else if(recordPending&&std::abs(double(frame-engine.position())/session.rate)>.15){referenceInterrupted("The video could not stay synchronized.");return;}
   referenceAwaiting=referenceBuffering=false;referenceDeadline=0;engine.setSyncHeld(false);videoDrift.reset();setMessage(recordPending?"Recording Guitar "+juce::String(session.activeTrack+1)+" with the YouTube reference":"Playing with the YouTube reference");return;
  }
  const double error=double(frame-engine.position())/session.rate;
  if(videoDrift.observe(error,juce::Time::getMillisecondCounterHiRes())){
   if(recordPending){referenceInterrupted("The video lost synchronization.");return;}
   // Follow the observed media time without re-seeking YouTube on every poll.
   // This handles actual drift; routine Play/Seek uses the readiness handshake.
   engine.setSyncHeld(true);engine.alignForReference(frame);engine.setSyncHeld(false);
  }
 }else if(status.state==1&&!status.starting&&status.reason!="command"&&status.reason!="positioned"){
  if(recordPending)return;
  const auto target=videoFrame(status.seconds);view.youtube.pauseTransport();
  if(busy){pendingVideoSeek=target;pendingVideoPlay=true;return;}
  engine.seek(target);session.cursor=target;preparePlayback(true);
 }
}
