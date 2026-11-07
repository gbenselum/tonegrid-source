#pragma once
#include "Modules/Record/Model/RecordSession.h"
#include "Modules/Record/YouTube/YouTubeReference.h"
#include "Shared/UI/UiStyle.h"
#include "Shared/UI/PopupWindow.h"
#include "Modules/Record/UI/TimelineWindow.h"
#include <ToneGridApplicationAssets.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace tonegrid::record {
inline juce::Colour accent(){return ui::tabBlue();}
class RecordLook final:public juce::LookAndFeel_V4 {
public:
 void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down)override {
  if(b.getComponentID()=="record-export-all"){g.setColour(accent().withAlpha((down?.7f:hover?1.f:.9f)*(b.isEnabled()?1.f:.4f)));g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(1),7);}
  else ui::paintControl(g,b.getLocalBounds().toFloat(),b.getToggleState(),hover,down,b.isEnabled(),b.hasKeyboardFocus(true));
 }
 void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool)override {g.setFont(ui::font(14));g.setColour(ui::ink().withAlpha(b.isEnabled()?1.f:.4f));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(5,1),juce::Justification::centred,1);}
 void fillTextEditorBackground(juce::Graphics& g,int w,int h,juce::TextEditor&)override {ui::paintControl(g,{0.f,0.f,float(w),float(h)},false,false,false,true,false);}
 void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&)override{}
 void drawTooltip(juce::Graphics& g,const juce::String& text,int w,int h)override {g.setColour(juce::Colour(0xff1c292f));g.fillRoundedRectangle(0,0,float(w),float(h),7);g.setColour(ui::ink());g.setFont(ui::font(14));g.drawFittedText(text,{12,8,w-24,h-16},juce::Justification::centredLeft,12);}
};
class HelpButton final:public juce::TextButton {
public:explicit HelpButton(const juce::String& name):TextButton("?"){setTitle(name+" help");}
 void paintButton(juce::Graphics& g,bool hover,bool down)override {auto r=getLocalBounds().toFloat().reduced(2);g.setColour(juce::Colour(hover?0xff263b49:0xff182a35));g.fillEllipse(r);g.setColour((hover||hasKeyboardFocus(true)?accent():ui::secondaryInk()).withAlpha(down?.7f:1.f));g.drawEllipse(r,.9f);g.setFont(ui::font(15,juce::Font::bold));g.drawText(tonegrid::l10n::translate("?"),getLocalBounds(),juce::Justification::centred);}
};
class ArmButton final:public juce::Button {
public:explicit ArmButton(int index):Button("Record onto Guitar "+juce::String(index+1)){setTitle(getName());tonegrid::l10n::tooltip(*this,"Choose Guitar "+juce::String(index+1)+" for your next recording. Other tracks play along. Render your editable guitar first to keep its sound.");}
 void paintButton(juce::Graphics& g,bool hover,bool down)override {auto r=getLocalBounds().toFloat().withSizeKeepingCentre(17,17);g.setColour(juce::Colour(getToggleState()?0xfff13b58:0xff523a43));g.fillEllipse(r);g.setColour(getToggleState()?juce::Colour(0xffff9eae):hover?ui::ink():juce::Colour(0xff91707a));g.drawEllipse(r,down?2.f:1.f);if(getToggleState()){g.setColour(juce::Colour(0xfff13b58).withAlpha(.18f));g.drawEllipse(r.expanded(3),2);}}
};
class TransportButton final:public juce::Button {
public:enum Kind{start,play,record,stop};TransportButton(Kind k):Button(k==start?"Go to Start":k==play?"Play/Pause":k==record?"Record":"Stop"),kind(k){tonegrid::l10n::tooltip(*this,k==play?"Play or pause at the cursor. Shortcut: Space.":k==record?"Record dry guitar onto the track with the red recording selector.":k==stop?"Stop and finish the take. During a render or export, cancel safely.":"Return to the beginning of the timeline.");setComponentID("record-"+getName().toLowerCase().replace(" ","-"));}
 void paintButton(juce::Graphics& g,bool hover,bool down)override {
  auto r=getLocalBounds().toFloat().reduced(2);g.setGradientFill(juce::ColourGradient(juce::Colour(hover?0xff293e49:0xff1a2b34),0,0,juce::Colour(0xff0a171f),0,r.getBottom(),false));g.fillEllipse(r);g.setColour(kind==record?juce::Colour(0xffbe4054):juce::Colour(0xff536f7d));g.drawEllipse(r,1);
  const auto c=r.getCentre();juce::Path p;g.setColour((kind==record?juce::Colour(0xfff13b58):down?accent():juce::Colour(0xffd5e6f0)).withAlpha(isEnabled()?1.f:.4f));
  if(kind==record)g.fillEllipse(c.x-10,c.y-10,20,20);
  else if(kind==stop)g.fillRoundedRectangle(c.x-7,c.y-7,14,14,1);
  else if(kind==play&&getToggleState()){g.fillRoundedRectangle(c.x-7,c.y-9,4,18,1);g.fillRoundedRectangle(c.x+3,c.y-9,4,18,1);}
  else if(kind==play){p.addTriangle(c.x-6,c.y-9,c.x-6,c.y+9,c.x+10,c.y);g.fillPath(p);}
  else {g.fillRect(c.x-8,c.y-8,2.f,16.f);p.addTriangle(c.x+7,c.y-8,c.x+7,c.y+8,c.x-4,c.y);g.fillPath(p);}
 }
private:Kind kind;
};
class TrackHeader final:public juce::Button {
public:explicit TrackHeader(int i):Button("Select Guitar "+juce::String(i+1)){setMouseCursor(juce::MouseCursor::PointingHandCursor);setTooltip("Click anywhere in this guitar panel to select it. Render keeps its sound; Unfreeze lets you change its preset again.");}
 void paintButton(juce::Graphics& g,bool hover,bool)override{auto r=getLocalBounds().toFloat().reduced(.75f);if(hover){g.setColour(accent().withAlpha(.065f));g.fillRoundedRectangle(r,8);}g.setColour((getToggleState()?accent():hover?ui::secondaryInk():ui::outline()).withAlpha(isEnabled()?1.f:.5f));g.drawRoundedRectangle(r,8,getToggleState()?1.4f:.8f);}
};
class TimelineTool final:public juce::Button {
public:enum Kind{in,out,fit};explicit TimelineTool(Kind k):Button(k==in?"Zoom in":k==out?"Zoom out":"Fit song"),kind(k){tonegrid::l10n::tooltip(*this,k==fit?"Show the whole song from 00:00.":k==in?"Zoom in around the visible playhead, or the centre of this view.":"Zoom out to see more of the song.");setMouseCursor(juce::MouseCursor::NormalCursor);}
 void paintButton(juce::Graphics& g,bool hover,bool down)override{auto r=getLocalBounds().toFloat().reduced(1);g.setColour(juce::Colour(hover?0xff243946:0xff101e27));g.fillRoundedRectangle(r,4);g.setColour(down?accent():ui::ink());const auto c=r.getCentre();if(kind==fit){g.drawLine(c.x-8,c.y-6,c.x-8,c.y+6,1.3f);g.drawLine(c.x+8,c.y-6,c.x+8,c.y+6,1.3f);g.drawLine(c.x-5,c.y,c.x+5,c.y,1.3f);juce::Path p;p.addTriangle(c.x-7,c.y,c.x-3,c.y-3,c.x-3,c.y+3);p.addTriangle(c.x+7,c.y,c.x+3,c.y-3,c.x+3,c.y+3);g.fillPath(p);}else{g.drawEllipse(c.x-8,c.y-8,12,12,1.3f);g.drawLine(c.x+2,c.y+2,c.x+8,c.y+8,1.6f);g.drawLine(c.x-5,c.y-2,c.x+1,c.y-2,1.2f);if(kind==in)g.drawLine(c.x-2,c.y-5,c.x-2,c.y+1,1.2f);}}
private:Kind kind;
};
class TimelineView final:public juce::Component,public juce::SettableTooltipClient,private juce::ChangeListener,private juce::ScrollBar::Listener {
public:
 static constexpr int rowTop=34,rowPitch=122,rowHeight=110;
 TimelineView():cache(24){formats.registerBasicFormats();setWantsKeyboardFocus(true);for(auto* b:{&zoomIn,&zoomOut,&fitSong})addAndMakeVisible(b);zoomIn.onClick=[this]{zoom(.5);};zoomOut.onClick=[this]{zoom(2);};fitSong.onClick=[this]{window.fit(extent());syncNavigation();repaint();};addAndMakeVisible(horizontal);horizontal.setAutoHide(false);horizontal.setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff718491));horizontal.addListener(this);tonegrid::l10n::tooltip(*this,"Drag the ruler to select time. Drag the waveforms to move the view.");}
 ~TimelineView()override{horizontal.removeListener(this);for(auto& x:thumbnails)x.second->removeChangeListener(this);}
 void update(const RecordSession& value,const juce::File& folder){
  state=value;juce::StringArray files;for(int i=0;i<2;++i){const auto& t=track(state,i);for(const auto& c:t.clips)files.addIfNotAlreadyThere(c.file);if(t.rendered.isNotEmpty())files.addIfNotAlreadyThere(t.rendered);}if(state.backing.isNotEmpty())files.addIfNotAlreadyThere(state.backing);
  for(const auto& name:files)if(!thumbnails.count(name)){auto thumb=std::make_unique<juce::AudioThumbnail>(64,formats,cache);thumb->addChangeListener(this);thumb->setSource(new juce::FileInputSource(folder.getChildFile(name)));thumbnails.emplace(name,std::move(thumb));}syncNavigation();repaint();
 }
 void setPosition(juce::int64 value,bool follow){const auto old=window.first;if(follow)window.follow(value/state.rate);if(position==value&&old==window.first)return;position=value;syncNavigation();repaint();}
 void revealPosition(juce::int64 value){position=value;window.reveal(value/state.rate);syncNavigation();repaint();}
 void resumeFollowing(){window.manual=false;}
 double firstVisibleSeconds()const{return window.first;}double visibleSeconds()const{return window.seconds;}
 void panTo(double seconds){window.pan(seconds,extent());syncNavigation();repaint();}
 void zoom(double factor){const auto at=position/state.rate;const auto anchor=at>=window.first&&at<window.first+window.seconds?at:window.first+window.seconds*.5;window.zoom(factor,anchor,extent());syncNavigation();repaint();}
 void resized()override{horizontal.setBounds(0,getHeight()-16,getWidth(),16);zoomIn.setBounds(getWidth()-94,2,28,28);zoomOut.setBounds(getWidth()-63,2,28,28);fitSong.setBounds(getWidth()-32,2,28,28);syncNavigation();}
 juce::AudioThumbnail* beginTake(const RecordSession& value){state=value;auto thumbnail=std::make_unique<juce::AudioThumbnail>(64,formats,cache);thumbnail->addChangeListener(this);auto* result=thumbnail.get();thumbnails.emplace(value.pendingTake,std::move(thumbnail));return result;}
 void drawWave(juce::Graphics& g,juce::Rectangle<float> area,const juce::String& file,double sourceFrom,double sourceTo,juce::Colour colour){
  auto it=thumbnails.find(file);if(it==thumbnails.end()||area.getWidth()<=0||sourceTo<=sourceFrom)return;auto& t=*it->second;
  const float scale=.85f/juce::jmax(.01f,t.getApproximatePeak());const int channels=juce::jmax(1,t.getNumChannels());
  for(int ch=0;ch<channels;++ch){auto r=area.withHeight(area.getHeight()/channels).translated(0,ch*area.getHeight()/channels).reduced(0,3);juce::Path envelope;const int count=juce::jmax(1,(int)std::ceil(r.getWidth()));std::vector<float> lowerValues((size_t)count+1);
   for(int x=0;x<=count;++x){float low=0,high=0;const auto a=sourceFrom+(sourceTo-sourceFrom)*x/(count+1),b=sourceFrom+(sourceTo-sourceFrom)*(x+1)/(count+1);t.getApproximateMinMax(a,b,ch,low,high);const auto px=r.getX()+r.getWidth()*x/count;const auto hi=r.getCentreY()-juce::jlimit(-1.f,1.f,high*scale)*r.getHeight()*.5f,lo=r.getCentreY()-juce::jlimit(-1.f,1.f,low*scale)*r.getHeight()*.5f;
    lowerValues[(size_t)x]=lo;if(x==0)envelope.startNewSubPath(px,hi);else envelope.lineTo(px,hi);
   }
   // Reverse the lower boundary to preserve measured peaks, without invented smoothing.
   for(int x=count;x>=0;--x)envelope.lineTo(r.getX()+r.getWidth()*x/count,lowerValues[(size_t)x]);
   envelope.closeSubPath();g.setColour(colour.withAlpha(.65f));g.fillPath(envelope);g.setColour(colour.withAlpha(.9f));g.strokePath(envelope,juce::PathStrokeType(.65f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
  }
 }
 void paint(juce::Graphics& g)override {
  const auto width=getWidth();const auto first=window.first*state.rate;const float pixels=float(width/window.seconds);g.fillAll(juce::Colour(0xff0a151c));
  if(selectionEnd>selectionStart){g.setColour(accent().withAlpha(.22f));g.fillRect(juce::Rectangle<float>(float((selectionStart-first)/state.rate)*pixels,0,float((selectionEnd-selectionStart)/state.rate)*pixels,rowTop));}
  double major=86400;for(double step:{.05,.1,.25,.5,1.,2.,5.,10.,15.,30.,60.,120.,300.,600.,1800.,3600.,10800.,21600.,43200.,86400.})if(step>=window.seconds/juce::jmax(1.,width/95.)){major=step;break;}
  const double minor=major/5;const auto begin=(juce::int64)std::floor(window.first/minor),end=(juce::int64)std::ceil((window.first+window.seconds)/minor);
  for(auto i=begin;i<=end;++i){const auto sec=i*minor;const float x=float(sec-window.first)*pixels;g.setColour(juce::Colour(i%5?0xff152630:0xff253944));g.drawVerticalLine((int)x,0.f,float(getHeight()-16));if(i%5==0&&x<width-105){g.setColour(juce::Colour(0xff9ab2c1));g.setFont(ui::font(12));g.drawText(timeText(sec,major<1),(int)x+4,0,90,24,juce::Justification::centredLeft);}}
  g.setColour(ui::outline());g.drawHorizontalLine(rowTop-1,0,float(width));
  const auto xFor=[&](double frame){return float((frame-first)/state.rate)*pixels;};
  for(int lane=0;lane<3;++lane){auto area=juce::Rectangle<float>(1,float(rowTop+lane*rowPitch),float(width-2),rowHeight);g.setColour(lane<2?accent().withAlpha(.10f):juce::Colour(0xff1d2a32));g.fillRoundedRectangle(area,6);
   if(lane<2){const auto& t=track(state,lane);auto clips=t.clips;if(t.rendered.isNotEmpty())clips={{t.rendered,0,t.guitarLength,0}};if(state.pendingTake.isNotEmpty()&&state.pendingTrack==lane)clips.push_back({state.pendingTake,state.pendingStart,std::max<juce::int64>(1,position-state.pendingStart),0});
    for(const auto& c:clips){const double a=juce::jmax(double(c.start),double(first)),b=juce::jmin(double(c.start+c.length),double(first)+state.rate*window.seconds);if(b<=a)continue;drawWave(g,{xFor(a),area.getY()+5,xFor(b)-xFor(a),area.getHeight()-10},c.file,(c.sourceStart+a-c.start)/state.rate,(c.sourceStart+b-c.start)/state.rate,accent());}
    if(clips.empty()){g.setColour(ui::metadataInk());g.setFont(ui::font(13));g.drawText(l10n::format("record.empty_help",{{"number",juce::String(lane+1)}}),area.toNearestInt(),juce::Justification::centred);}
   }else drawWave(g,area.reduced(0,4),state.backing,first/state.rate,first/state.rate+window.seconds,juce::Colour(0xff9bb2c2));
   if(selectionEnd>selectionStart){g.setColour(accent().withAlpha(.16f));g.fillRect(juce::Rectangle<float>(xFor(selectionStart),area.getY(),xFor(selectionEnd)-xFor(selectionStart),area.getHeight()));}
  }
  const float x=xFor(position);g.setColour(accent());g.drawLine(x,28,x,float(getHeight()-16),1.5f);juce::Path triangle;triangle.addTriangle(x-6,23,x+6,23,x,33);g.fillPath(triangle);
 }
 void mouseMove(const juce::MouseEvent& e)override{setMouseCursor(e.y<rowTop?juce::MouseCursor::IBeamCursor:juce::MouseCursor::DraggingHandCursor);setTooltip(e.y<rowTop?"Click to place the cursor. Drag along this ruler to select a time range for export or Delete.":"Drag left or right to move through the song. A click places the cursor; dragging keeps your time selection.");}
 void mouseDown(const juce::MouseEvent& e)override{if(!editable)return;grabKeyboardFocus();anchor=at(e.x);mouseAnchor=e.x;panOrigin=window.first;selecting=e.y<rowTop;panning=!selecting;panMoved=false;if(selecting){selectionStart=selectionEnd=anchor;if(onSeek)onSeek(anchor);if(onSelection)onSelection();}else selectedTrack=juce::jlimit(0,1,(e.y-rowTop)/rowPitch);repaint();}
 void mouseDrag(const juce::MouseEvent& e)override{if(!editable)return;if(selecting){selectionStart=juce::jmin(anchor,at(e.x));selectionEnd=juce::jmax(anchor,at(e.x));if(onSelection)onSelection();}else if(panning&&std::abs(e.x-mouseAnchor)>3){panMoved=true;window.pan(panOrigin-(e.x-mouseAnchor)*window.seconds/getWidth(),extent());syncNavigation();}repaint();}
 void mouseUp(const juce::MouseEvent&)override{if(panning&&!panMoved&&editable&&onSeek)onSeek(anchor);selecting=panning=false;}
 void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel)override{if(e.mods.isCtrlDown()||e.mods.isCommandDown())window.zoom(std::pow(2.,-wheel.deltaY*2),window.first+e.x*window.seconds/getWidth(),extent());else window.pan(window.first-(wheel.deltaX!=0?wheel.deltaX:wheel.deltaY)*window.seconds*.5,extent());syncNavigation();repaint();}
 bool keyPressed(const juce::KeyPress& k)override{if(editable&&(k.getKeyCode()==juce::KeyPress::deleteKey||k.getKeyCode()==juce::KeyPress::backspaceKey)){removeSelection();return true;}return false;}
 void removeSelection(){if(editable&&selectionEnd>selectionStart&&onDelete){onDelete(selectionStart,selectionEnd);selectionEnd=selectionStart;if(onSelection)onSelection();repaint();}}
 void setSelection(juce::int64 a,juce::int64 b){selectionStart=std::max<juce::int64>(0,a);selectionEnd=juce::jmax(selectionStart,b);if(onSelection)onSelection();repaint();}
 bool hasSelection()const{return selectionEnd>selectionStart;}juce::int64 selectionFrom()const{return selectionStart;}juce::int64 selectionTo()const{return selectionEnd;}
 static juce::String timeText(double sec,bool millis=true){const int whole=(int)sec;return juce::String(whole/60).paddedLeft('0',2)+":"+juce::String(whole%60).paddedLeft('0',2)+(millis?"."+juce::String((int)(sec*1000)%1000).paddedLeft('0',3):juce::String{});}
 TimelineTool zoomIn{TimelineTool::in},zoomOut{TimelineTool::out},fitSong{TimelineTool::fit};
 std::function<void(juce::int64)> onSeek;std::function<void(juce::int64,juce::int64)> onDelete;std::function<void()> onSelection;bool editable=true;int selectedTrack=0;
private:
 juce::int64 at(int x)const{return std::llround((window.first+juce::jlimit(0,getWidth(),x)*window.seconds/getWidth())*state.rate);}
 double extent()const{double end=juce::jmax(state.guitarLength,state.second.guitarLength)/state.rate;for(int i=0;i<2;++i)for(const auto& c:track(state,i).clips)end=juce::jmax(end,(c.start+c.length)/state.rate);auto backing=thumbnails.find(state.backing);if(backing!=thumbnails.end())end=juce::jmax(end,backing->second->getTotalLength());return juce::jmax(end,position/state.rate);}
 void syncNavigation(){const auto total=window.limit(extent());window.first=juce::jlimit(0.,juce::jmax(0.,total-window.seconds),window.first);horizontal.setRangeLimits(0,total);horizontal.setCurrentRange(window.first,window.seconds,juce::dontSendNotification);horizontal.setSingleStepSize(juce::jmax(.1,window.seconds*.1));}
 void scrollBarMoved(juce::ScrollBar*,double start)override{window.pan(start,extent());syncNavigation();repaint();}
 void changeListenerCallback(juce::ChangeBroadcaster*)override{syncNavigation();repaint();}
 TimelineWindow window;juce::ScrollBar horizontal{false};double panOrigin=0;int mouseAnchor=0;bool panning=false,panMoved=false;
 RecordSession state;juce::AudioFormatManager formats;juce::AudioThumbnailCache cache;std::map<juce::String,std::unique_ptr<juce::AudioThumbnail>> thumbnails;
 juce::int64 position=0,anchor=0,selectionStart=0,selectionEnd=0;bool selecting=false;
};
class RecordView final:public juce::Component,private juce::KeyListener {
public:
 RecordView(juce::File profile,bool testing):youtube(std::move(profile),testing){
  exportMix.setComponentID("record-export-all");
  setLookAndFeel(&look);setWantsKeyboardFocus(true);content.addAndMakeVisible(firstHeader);content.addAndMakeVisible(secondHeader);
  headstock=juce::Drawable::createFromImageData(ToneGridApplicationAssets::record_headstock_svg,ToneGridApplicationAssets::record_headstock_svgSize);
  for(auto* b:{&goStart,&play,&record,&stop})content.addAndMakeVisible(b);
  for(auto* b:{&exportGuitar,&exportMix,&loadAudio,&removeBacking,&deleteSelection,&deleteSecond,&loadYoutube,&removeYoutube,&renderFirst,&renderSecond})content.addAndMakeVisible(b);
  for(auto* b:{&armFirst,&armSecond})content.addAndMakeVisible(b);
  for(auto* b:{&guitarMute,&secondMute,&backingMute}){content.addAndMakeVisible(b);b->setClickingTogglesState(true);tonegrid::l10n::tooltip(*b,"Mute this track during playback and export.");}
  for(auto* s:{&guitarVolume,&secondVolume,&backingVolume}){content.addAndMakeVisible(s);s->setRange(0,2,.001);s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->setColour(juce::Slider::trackColourId,accent());s->setColour(juce::Slider::thumbColourId,accent());s->setValue(s==&backingVolume?.7:1);tonegrid::l10n::tooltip(*s,"Track volume for playback and export. Double-click to reset.");s->setDoubleClickReturnValue(true,s==&backingVolume?.7:1);}
  for(auto* b:{&guitarHelp,&secondHelp,&backingHelp,&youtubeHelp})content.addAndMakeVisible(b);
  const juce::String guitarGuide="Choose a preset and record.\n\nClick this guitar panel to select it. Play your recording and try different presets. Render applies the preset so this guitar keeps its sound while you work on the other guitar.\n\nTo change its preset later, select it and click Unfreeze. Your original recording is always kept.";
  help(guitarHelp,guitarGuide);help(secondHelp,guitarGuide);
  help(backingHelp,"Play along with a local backing track.\n\nLoad an MP3, WAV, FLAC or AIFF. It starts at 00:00 and is copied into this session. Set its volume or mute it. Remove clears the lane without changing your original file. Local backing can be included in exports.");
  help(youtubeHelp,"A video reference for practising.\n\nPaste a YouTube link and click Load. Play or pause in ToneGrid or in the video. Moving either timeline keeps them together. ToneGrid waits for the video before starting. If the video pauses or buffers during a take, the take is saved; press Record to continue. Small timing differences can remain. YouTube sound is never recorded or exported. With exclusive ASIO, try shared audio or local backing.");
  content.addAndMakeVisible(youtubeURL);tonegrid::l10n::tooltip(youtubeURL,"Paste a YouTube link, then click Load. Video timing is approximate; its audio is excluded from exports.");youtubeURL.setFont(ui::font(14));youtubeURL.setColour(juce::TextEditor::textColourId,ui::ink());youtubeURL.setIndents(9,8);tonegrid::l10n::placeholder(youtubeURL,"Paste YouTube link here...",juce::Colour(0xff9ab2c1));
  content.addAndMakeVisible(youtube);content.addAndMakeVisible(timeline);content.addAndMakeVisible(time);content.addAndMakeVisible(status);
  time.setFont(ui::font(29));tonegrid::l10n::text(time,"00:00.000",juce::dontSendNotification);time.setColour(juce::Label::textColourId,ui::ink());status.setColour(juce::Label::textColourId,ui::secondaryInk());status.setFont(ui::font(12));
  viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,false);addAndMakeVisible(viewport);content.owner=this;
  deleteSelection.onClick=[this]{timeline.selectedTrack=0;timeline.removeSelection();};deleteSecond.onClick=[this]{timeline.selectedTrack=1;timeline.removeSelection();};
  l10n::formattedTooltip(deleteSelection,"record.delete_help",{{"number","1"}});l10n::formattedTooltip(deleteSecond,"record.delete_help",{{"number","2"}});
  tonegrid::l10n::tooltip(loadAudio,"Copy a local MP3, WAV, FLAC or AIFF into this session as your backing track.");tonegrid::l10n::tooltip(removeBacking,"Remove the local backing from this session. Your original file is preserved.");
  tonegrid::l10n::tooltip(removeYoutube,"Stop the video and remove its link from this session. Guitar and local backing stay intact.");
  tonegrid::l10n::tooltip(exportMix,"Export the complete recording as an MP3, mixing both guitars and local backing. Uses track volume and mute settings. YouTube is excluded.");timeline.onSelection=[this]{refreshExport();};
  addKeys(*this);
 }
 ~RecordView()override{dismissHelp();viewport.setViewedComponent(nullptr,false);setLookAndFeel(nullptr);}
 void dismissHelp(){if(helpBox)helpBox->closeButtonPressed();}
 void help(HelpButton& button,const juce::String& text){l10n::tooltip(button,text);button.onClick=[this,&button,text]{dismissHelp();class Card final:public popup::View{public:juce::Label copy;tonegrid::l10n::TextButton ok{"OK"};explicit Card(const juce::String& text){addAndMakeVisible(copy);l10n::text(copy,text);copy.setFont(ui::font(15));copy.setJustificationType(juce::Justification::topLeft);addAndMakeVisible(ok);ok.onClick=[this]{if(auto* w=findParentComponentOfClass<juce::DialogWindow>())w->closeButtonPressed();};setSize(520,popupContentHeight(520));}int popupMinimumWidth()const override{return 320;}int popupContentHeight(int width)const override{juce::AttributedString value;value.append(copy.getText(),ui::font(15),ui::ink());juce::TextLayout layout;layout.createLayout(value,float(width));return juce::roundToInt(layout.getHeight())+66;}void resized()override{copy.setBounds(0,0,getWidth(),getHeight()-58);ok.setBounds(getWidth()-120,getHeight()-38,120,38);}};helpBox=popup::launch(button.getTitle(),*new Card(text),*this,true);};}
 bool keyPressed(const juce::KeyPress& k,juce::Component* origin)override{if(k.getKeyCode()!=juce::KeyPress::spaceKey||juce::Component::getCurrentlyModalComponent()!=nullptr)return false;for(auto* p=origin;p;p=p->getParentComponent())if(dynamic_cast<juce::TextEditor*>(p)||p==&youtube)return false;if(onSpace)onSpace();return true;}
 bool keyPressed(const juce::KeyPress& k)override{return keyPressed(k,this);}
 void resized()override{viewport.setBounds(getLocalBounds());content.setBounds(0,0,juce::jmax(920,getWidth()-14),juce::jmax(770,getHeight()));layout();}
 void layout(){
  const int w=content.getWidth(),left=320;int x=juce::jlimit(24,400,w-900);for(auto* b:{&goStart,&play,&record,&stop}){b->setBounds(x,16,48,48);x+=62;}
  time.setBounds(x+8,22,185,36);exportMix.setBounds(w-175,18,156,44);exportGuitar.setBounds(w-383,18,196,44);timeline.setBounds(left,88,w-left-20,412);
  for(int i=0;i<2;++i){const int y=122+i*122;auto& arm=i?armSecond:armFirst;auto& mute=i?secondMute:guitarMute;auto& del=i?deleteSecond:deleteSelection;auto& freeze=i?renderSecond:renderFirst;auto& vol=i?secondVolume:guitarVolume;auto& info=i?secondHelp:guitarHelp;
   (i?secondHeader:firstHeader).setBounds(26,y,282,110);info.setBounds(276,y+10,24,24);arm.setBounds(39,y+44,30,30);mute.setBounds(78,y+44,32,30);del.setBounds(118,y+44,88,30);freeze.setBounds(214,y+44,82,30);vol.setBounds(39,y+82,257,24);
  }
  backingHelp.setBounds(276,376,24,24);loadAudio.setBounds(42,410,106,30);backingMute.setBounds(156,410,32,30);removeBacking.setBounds(196,410,100,30);backingVolume.setBounds(39,448,257,24);
  const int top=518;youtubeHelp.setBounds(276,top+10,24,24);youtubeURL.setBounds(38,top+58,258,34);loadYoutube.setBounds(38,top+104,123,32);removeYoutube.setBounds(173,top+104,123,32);
  const int note=w>=1250?220:0,playerWidth=juce::jmax(200,w-left-note-40);youtube.setBounds(left+8,top+12,playerWidth,210);status.setBounds(20,content.getHeight()-27,w-40,22);
 }
 void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff071219));}
 void revealReference(){viewport.setViewPosition(0,juce::jmax(0,youtube.getBottom()-viewport.getHeight()+12));}
 bool referenceVisible()const{return youtube.isFullScreen()||viewport.getViewArea().contains(youtube.getBounds());}
 void setState(const RecordSession& s,const juce::File& folder){state=s;timeline.update(s,folder);guitarMute.setToggleState(s.guitarMute,juce::dontSendNotification);secondMute.setToggleState(s.second.guitarMute,juce::dontSendNotification);backingMute.setToggleState(s.backingMute,juce::dontSendNotification);guitarVolume.setValue(s.guitarVolume,juce::dontSendNotification);secondVolume.setValue(s.second.guitarVolume,juce::dontSendNotification);backingVolume.setValue(s.backingVolume,juce::dontSendNotification);
  timeline.selectedTrack=s.activeTrack;firstHeader.setToggleState(s.activeTrack==0,juce::dontSendNotification);secondHeader.setToggleState(s.activeTrack==1,juce::dontSendNotification);
  armFirst.setToggleState(s.activeTrack==0&&s.editableTrack==0,juce::dontSendNotification);armSecond.setToggleState(s.activeTrack==1&&s.editableTrack==1,juce::dontSendNotification);
  tonegrid::l10n::button(renderFirst,s.rendered.isNotEmpty()?"Unfreeze":"Render");tonegrid::l10n::button(renderSecond,s.second.rendered.isNotEmpty()?"Unfreeze":"Render");
  for(auto* b:{&renderFirst,&renderSecond})tonegrid::l10n::tooltip(*b,(b==&renderFirst?s.rendered:s.second.rendered).isEmpty()?"Keep this track's current sound. Later preset changes will not affect it. Your original dry recording is preserved.":"Make this track editable using its saved tone. Change the preset, then Render again. The other guitar keeps its sound.");
  if(!youtubeURL.hasKeyboardFocus(false))youtubeURL.setText(s.videoURL,false);content.repaint();refreshExport();
 }
 void setBusy(bool busy,bool capturing,bool playing){blocked=busy||capturing||playing;firstHeader.setEnabled(!blocked);secondHeader.setEnabled(!blocked);for(auto* s:{&guitarVolume,&secondVolume,&backingVolume})s->setEnabled(!busy);for(auto* b:{&guitarMute,&secondMute,&backingMute})b->setEnabled(!busy);for(auto* b:{&armFirst,&armSecond})b->setEnabled(!blocked);
  renderFirst.setEnabled(!blocked&&!state.clips.empty());renderSecond.setEnabled(!blocked&&!state.second.clips.empty());deleteSelection.setEnabled(!blocked&&state.editableTrack==0);deleteSecond.setEnabled(!blocked&&state.editableTrack==1);
  loadYoutube.setEnabled(!busy&&!capturing);removeYoutube.setEnabled(!busy&&!capturing&&state.videoId.isNotEmpty());youtubeURL.setEnabled(!busy&&!capturing);
  goStart.setEnabled(!busy&&!capturing);play.setEnabled(!busy&&!capturing);play.setToggleState(playing,juce::dontSendNotification);record.setEnabled(!busy&&!capturing&&state.editableTrack==state.activeTrack);
  loadAudio.setEnabled(!blocked);removeBacking.setEnabled(!blocked&&state.backing.isNotEmpty());timeline.editable=!busy&&!capturing;exportMix.setEnabled(!blocked);refreshExport();
 }
 void refreshExport(){exportGuitar.setEnabled(!blocked&&timeline.hasSelection());tonegrid::l10n::tooltip(exportGuitar,timeline.hasSelection()?"Export the highlighted time range as an MP3, mixing both guitars and local backing. Uses track volume and mute settings. YouTube is excluded.":"Drag across the timeline to select a time range first.");}
 std::function<void()> onSpace;
 TransportButton goStart{TransportButton::start},play{TransportButton::play},record{TransportButton::record},stop{TransportButton::stop};
 tonegrid::l10n::TextButton exportGuitar{"Export time selection"},exportMix{"Export all"},loadAudio{"Load Audio"},removeBacking{"Remove"},deleteSelection{"Delete"},deleteSecond{"Delete"},loadYoutube{"Load"},removeYoutube{"Remove"},guitarMute{"M"},secondMute{"M"},backingMute{"M"},renderFirst{"Render"},renderSecond{"Render"};
 TrackHeader firstHeader{0},secondHeader{1};
 ArmButton armFirst{0},armSecond{1};HelpButton guitarHelp{"Guitar 1"},secondHelp{"Guitar 2"},backingHelp{"Backingtrack"},youtubeHelp{"YouTube Reference"};
 juce::Slider guitarVolume,secondVolume,backingVolume;juce::TextEditor youtubeURL;YouTubeWebView youtube;TimelineView timeline;juce::Label time,status;
private:
 void addKeys(juce::Component& c){c.addKeyListener(this);for(int i=0;i<c.getNumChildComponents();++i)addKeys(*c.getChildComponent(i));}
 void guitarIcon(juce::Graphics& g,float x,float y){if(headstock)headstock->drawWithin(g,{x+4,y-2,28,40},juce::RectanglePlacement::centred,1.f);}
 class Content:public juce::Component {
 public:RecordView* owner=nullptr;void paint(juce::Graphics& g)override {
  if(!owner)return;g.setGradientFill(juce::ColourGradient(juce::Colour(0xff10222c),0,0,juce::Colour(0xff061017),float(getWidth()),float(getHeight()),false));g.fillAll();
  const auto panel=[&](juce::Rectangle<float> r){g.setColour(juce::Colour(0xff14252f));g.fillRoundedRectangle(r,8);g.setColour(juce::Colour(0xff344e5b));g.drawRoundedRectangle(r,8,1);};panel({12,7,float(getWidth()-24),70});
  for(int i=0;i<3;++i)panel({26,float(122+i*122),282,110});panel({26,518,float(getWidth()-52),222});
  for(int i=0;i<2;++i){const int y=122+i*122;owner->guitarIcon(g,42.f,float(y+9));g.setColour(ui::ink());g.setFont(ui::font(18,ui::emphasisWeight));g.drawText(l10n::format("record.guitar",{{"number",juce::String(i+1)}}),83,y+11,98,25,juce::Justification::centredLeft);const auto& t=track(owner->state,i);g.setColour(t.rendered.isNotEmpty()?ui::secondaryInk():accent());g.setFont(ui::font(12));g.drawText(t.rendered.isNotEmpty()?tonegrid::l10n::translate("Rendered"):owner->state.editableTrack==i?tonegrid::l10n::translate("Editable"):tonegrid::l10n::translate("Empty"),184,y+13,86,22,juce::Justification::centredRight);}
  g.setColour(ui::ink());g.setFont(ui::font(18,ui::emphasisWeight));g.drawText(juce::String("Backingtrack"),76,377,190,26,juce::Justification::centredLeft);g.drawText(tonegrid::l10n::translate("YouTube Reference"),76,529,194,28,juce::Justification::centredLeft);
  juce::Path note;note.startNewSubPath(49,396);note.lineTo(49,380);note.lineTo(65,376);note.lineTo(65,392);note.addEllipse(41,393,8,6);note.addEllipse(57,389,8,6);g.setColour(juce::Colour(0xffb9d1e1));g.strokePath(note,juce::PathStrokeType(1.6f));
  g.fillRoundedRectangle(41,534,25,18,4);juce::Path video;video.addTriangle(50,538,50,548,59,543);g.setColour(juce::Colour(0xff10222c));g.fillPath(video);
  if(getWidth()>=1250){g.setColour(ui::secondaryInk());g.setFont(ui::font(14));g.drawFittedText(tonegrid::l10n::translate("YouTube is a practice reference.\nTimeline seeking is approximate.\nIts audio is never recorded or exported."),getWidth()-228,542,192,155,juce::Justification::centredLeft,7);}
 }
 }content;
 RecordLook look;juce::Viewport viewport;RecordSession state;bool blocked=false;juce::Component::SafePointer<juce::DialogWindow> helpBox;std::unique_ptr<juce::Drawable> headstock;
};
}
