#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Platform/BrowserPlatform.h"
#include "Shared/Media/VideoFullscreen.h"
#include "Modules/Record/YouTube/YouTubeBridgeScript.h"
#include "Shared/Localization/Language.h"
#if JUCE_MAC && JUCE_WEB_BROWSER
#include "Platform/VideoBrowserMac.h"
#endif
namespace tonegrid::record {
inline juce::String youtubeId(const juce::String& raw) {
 auto text=raw.trim();if(text.startsWithIgnoreCase("www."))text="https://"+text;
 const juce::URL url(text);const auto scheme=url.getScheme().toLowerCase(),host=url.getDomain().toLowerCase();
 if(scheme!="https"&&scheme!="http")return {};juce::String id;
 auto path=url.getSubPath().trimCharactersAtStart("/");
 if(host=="youtu.be")id=path.upToFirstOccurrenceOf("/",false,false);
 else if(host=="youtube.com"||host=="www.youtube.com"||host=="m.youtube.com"||host=="music.youtube.com") {
  if(path=="watch"){for(int i=0;i<url.getParameterNames().size();++i)if(url.getParameterNames()[i]=="v")id=url.getParameterValues()[i];}
  if(path.startsWith("embed/")||path.startsWith("shorts/")||path.startsWith("live/"))id=path.fromFirstOccurrenceOf("/",false,false).upToFirstOccurrenceOf("/",false,false);
 }
 return id.length()==11&&id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")?id:juce::String{};
}
inline juce::String youtubeDocument(const juce::String& video,const juce::String& origin) {
    const auto address="https://www.youtube.com/embed/"+video+"?enablejsapi=1&playsinline=1&fs=1&origin="+juce::URL::addEscapeChars(origin,true);
    return juce::String("<!doctype html><html><head><meta name='referrer' content='strict-origin-when-cross-origin'><style>html,body{margin:0;width:100%;height:100%;background:#03090c;overflow:hidden}iframe{border:0;width:100%;height:100%}</style></head><body><iframe id='yt' src='")+address+"' allow='autoplay; encrypted-media; picture-in-picture; fullscreen' allowfullscreen></iframe><script>"+youtubeBridgeScript()+"</script></body></html>";
}
inline constexpr const char* macVideoBase="https://com.personalaudiolab.tonexstateprobe/";
class YouTubeWebView final:public juce::Component,private juce::ComponentListener,private juce::KeyListener {
public:
 explicit YouTubeWebView(juce::File profile,bool fake=false):profileFolder(std::move(profile)),testing(fake){}
 ~YouTubeWebView()override{onStatus={};pause();}
 void load(const juce::String& value){
  const auto next=youtubeId(value);if(next.isEmpty()){error="Enter a valid YouTube video link.";repaint();return;}
  if(requested&&id==next&&error.isEmpty())return;
  pause();id=next;error.clear();requested=true;lastSequence=0;++command;
  if(testing){repaint();return;}
#if JUCE_WEB_BROWSER && JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
  const auto generation=session;const auto video=id;
  auto options=platform::browserOptions(profileFolder).withNativeIntegrationEnabled()
   .withEventListener("recordVideoStatus",[safe=juce::Component::SafePointer<YouTubeWebView>(this),generation](juce::var value){
    juce::MessageManager::callAsync([safe,generation,value]{if(safe&&safe->session==generation&&safe->requested)safe->receive(value);});
   })
   .withEventListener("recordVideoFullscreen",[safe=juce::Component::SafePointer<YouTubeWebView>(this),generation](juce::var value){
    if(!value.isBool())return;juce::MessageManager::callAsync([safe,generation,full=bool(value)]{if(safe&&safe->session==generation&&safe->requested)safe->setFullScreen(full);});
   })
   .withResourceProvider([video](const juce::String& path)->std::optional<juce::WebBrowserComponent::Resource>{
    if(path!="/record.html")return std::nullopt;
    const auto origin=juce::WebBrowserComponent::getResourceProviderRoot().trimCharactersAtEnd("/");
    const auto html=youtubeDocument(video,origin);
    std::vector<std::byte> bytes((size_t)html.getNumBytesAsUTF8());std::memcpy(bytes.data(),html.toRawUTF8(),bytes.size());return juce::WebBrowserComponent::Resource{std::move(bytes),"text/html"};
   });
  if(profileFolder.createDirectory().failed()||!juce::WebBrowserComponent::areOptionsSupported(options)){requested=false;error="The embedded player is unavailable. Check your WebView runtime.";repaint();return;}
  browser=std::make_unique<Browser>(*this,options);addAndMakeVisible(*browser);browser->setBounds(getLocalBounds());
#if JUCE_MAC
  // A custom juce:// scheme cannot supply YouTube's required HTTPS referrer.
  // Keep the bridge/state machine intact and load it in the existing WKWebView.
  if(!platform::loadVideoDocument(*browser,youtubeDocument(video,juce::String(macVideoBase).trimCharactersAtEnd("/")),macVideoBase))
  {browser.reset();requested=false;error="Could not initialize the native YouTube player.";}
#else
  const juce::StringArray headers{"Referer: https://com.personalaudiolab.tonegridir/"};browser->goToURL(juce::WebBrowserComponent::getResourceProviderRoot()+"record.html",&headers);
#endif
#else
  requested=false;error="The embedded YouTube player is unavailable in this build.";
#endif
  repaint();
 }
 bool hasPlayer()const{return requested;}
 int seek(double seconds,bool playing=false){
  if(!std::isfinite(seconds))return command;requestedSeconds=juce::jmax(0.,seconds);transportPlaying=playing;++command;
#if JUCE_WEB_BROWSER
  sendCommand();
#endif
  if(testing&&automaticFake){VideoStatus value;value.command=command;value.sequence=++fakeSequence;value.seconds=requestedSeconds;value.duration=300;value.state=playing?1:2;value.reason=playing?"started":"positioned";deliver(value);}
  return command;
 }
 void play(){seek(requestedSeconds,true);}
 double requestedPosition()const{return requestedSeconds;}
 int commandId()const{return command;}
 unsigned generation()const{return session;}
 void pauseTransport(){transportPlaying=false;++command;
#if JUCE_WEB_BROWSER
  if(browser)browser->evaluateJavascript("window.tgRecord&&tgRecord.pause("+juce::String(command)+")");
#endif
 }
 void pause(){++session;setFullScreen(false);requested=false;transportPlaying=false;
#if JUCE_WEB_BROWSER
  browser.reset();
#endif
  repaint();
 }
 void clear(){pause();id.clear();error.clear();requestedSeconds=0;repaint();}
 void time(std::function<void(double)> done){inspect([done=std::move(done)](juce::var value){done(double(value["time"]));});}
 void inspect(std::function<void(juce::var)> done){
#if JUCE_WEB_BROWSER
  if(browser){const auto generation=session;browser->evaluateJavascript("window.tgRecord?JSON.stringify(tgRecord.inspect()):'{}'",[safe=juce::Component::SafePointer<YouTubeWebView>(this),generation,done=std::move(done)](juce::WebBrowserComponent::EvaluationResult result){if(safe&&safe->session==generation){if(const auto* value=result.getResult())done(juce::JSON::parse(value->toString()));else done({});}});return;}
#endif
  done({});
 }
 bool isFullScreen()const{
#if JUCE_MAC && JUCE_WEB_BROWSER
        if(browser&&platform::browserHasNativeFullscreen(*browser))return true;
#endif
        return fullscreenHost!=nullptr;
    }
 void resized()override{
#if JUCE_WEB_BROWSER
  if(browser&&!isFullScreen())browser->setBounds(getLocalBounds());
#endif
 }
 void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff050d12));g.setColour(juce::Colour(0xffafc2cd));g.setFont(14);g.drawFittedText(error.isNotEmpty()?error:id.isNotEmpty()?l10n::tr("record.reference")+"  -  "+id:l10n::tr("record.empty_reference"),getLocalBounds().reduced(18),juce::Justification::centred,3);}
 std::function<void(const VideoStatus&)> onStatus;
 void setAutomaticFake(bool value){if(testing)automaticFake=value;}
 void statusForTest(VideoStatus value){if(testing){value.command=command;value.sequence=++fakeSequence;deliver(value);}}
 void sendFullscreenEventForTest(bool active){
#if JUCE_WEB_BROWSER
  if(browser)browser->evaluateJavascript(juce::String("window.__JUCE__.backend.emitEvent('recordVideoFullscreen',")+(active?"true":"false")+")");
#else
  juce::ignoreUnused(active);
#endif
 }
 void escapeFullscreenForTest(){keyPressed(juce::KeyPress(juce::KeyPress::escapeKey),this);}
 void evaluateForTest(const juce::String& script){
#if JUCE_WEB_BROWSER
  if(browser)browser->evaluateJavascript(script);
#else
  juce::ignoreUnused(script);
#endif
 }
private:
 void deliver(const VideoStatus& value){if(!requested||value.command!=command||value.sequence<=lastSequence)return;lastSequence=value.sequence;const auto callback=onStatus;if(callback)callback(value);}
 void receive(const juce::var& value){
  if(!value.isObject())return;VideoStatus s;s.command=int(value["command"]);s.sequence=int(value["sequence"]);s.state=int(value["state"]);s.error=int(value["error"]);s.seconds=double(value["time"]);s.duration=double(value["duration"]);s.rate=double(value["rate"]);s.starting=bool(value["starting"]);s.blocked=bool(value["blocked"]);s.reason=value["reason"].toString();
  if(!std::isfinite(s.seconds)||s.seconds<0||!std::isfinite(s.duration)||s.duration<0||!std::isfinite(s.rate)||s.rate<=0||s.rate>4||s.seconds>86400||s.duration>86400)return;if(s.error!=0)error="YouTube player error "+juce::String(s.error);deliver(s);
 }
#if JUCE_WEB_BROWSER
 void sendCommand(){if(browser)browser->evaluateJavascript("window.tgRecord&&tgRecord.command("+juce::String(command)+","+juce::String(requestedSeconds,6)+","+juce::String(transportPlaying?"true":"false")+")");}
 class Browser final:public juce::WebBrowserComponent {
 public:Browser(YouTubeWebView& owner,const Options& o):WebBrowserComponent(o),view(owner),generation(owner.session){}
 void pageFinishedLoading(const juce::String& url)override{
#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
  if(generation==view.session&&view.requested&&(url==getResourceProviderRoot()+"record.html"
#if JUCE_MAC
   ||url==macVideoBase
#endif
  ))view.sendCommand();
#else
  juce::ignoreUnused(url);
#endif
 }
 bool pageAboutToLoad(const juce::String& url)override{
  if(url=="about:blank")return true;
#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
  if((url==getResourceProviderRoot()+"record.html"
#if JUCE_MAC
   ||url==macVideoBase
#endif
  ))return true;
#endif
  juce::URL parsed(url);return parsed.getScheme()=="https"&&((parsed.getDomain()=="www.youtube.com"&&parsed.getSubPath()=="embed/"+view.id)||parsed.getDomain()=="consent.youtube.com"||parsed.getDomain()=="consent.google.com");
 }
 void newWindowAttemptingToLoad(const juce::String& url)override{if(youtubeId(url).isNotEmpty())juce::URL(url).launchInDefaultBrowser();}
 bool pageLoadHadNetworkError(const juce::String&)override{if(generation!=view.session||!view.requested)return false;view.setFullScreen(false);view.error="YouTube could not load. Check the connection, then reload the reference.";VideoStatus value;value.command=view.command;value.sequence=++view.lastSequence;value.error=-1;value.reason="error";if(view.onStatus)view.onStatus(value);view.repaint();return false;}
 private:YouTubeWebView& view;unsigned generation;
 };
 std::unique_ptr<Browser> browser;
#endif
 void setFullScreen(bool active){
#if JUCE_WEB_BROWSER
  // A trusted iframe fullscreen request is already managed by WebKit.
        // Moving its containing NSView into another fullscreen window here
        // races that transition and corrupts restoration/clipping.
#if JUCE_MAC
        if(!fullscreenHost&&browser&&platform::browserHasNativeFullscreen(*browser))
        {
            if(!active)browser->evaluateJavascript("if(document.fullscreenElement)document.exitFullscreen().catch(function(){})");
            return;
        }
#endif
        if(active==isFullScreen())return;
  if(active){auto* host=getTopLevelComponent();auto* peer=host->getPeer();if(!browser||!peer)return;fullscreenHost=host;restoreBounds=host->getBounds();restoreFullscreen=peer->isFullScreen();host->addComponentListener(this);host->addKeyListener(this);
   host->addAndMakeVisible(*browser);if(!fullscreenFrame.enter(peer->getNativeHandle()))peer->setFullScreen(true);browser->setBounds(host->getLocalBounds());browser->toFront(true);
  }else if(auto* host=fullscreenHost.getComponent()){
   host->removeComponentListener(this);host->removeKeyListener(this);fullscreenHost=nullptr;
   if(browser){browser->evaluateJavascript("if(document.fullscreenElement)document.exitFullscreen().catch(function(){})");addAndMakeVisible(*browser);browser->setBounds(getLocalBounds());}
   if(!fullscreenFrame.leave())if(auto* peer=host->getPeer())peer->setFullScreen(restoreFullscreen);if(!restoreFullscreen)host->setBounds(restoreBounds);
  }
#else
  juce::ignoreUnused(active);
#endif
 }
 void componentMovedOrResized(juce::Component& host,bool,bool)override{
#if JUCE_WEB_BROWSER
  if(fullscreenHost==&host&&browser)browser->setBounds(host.getLocalBounds());
#else
  juce::ignoreUnused(host);
#endif
 }
 void componentBeingDeleted(juce::Component& host)override{if(fullscreenHost==&host)fullscreenHost=nullptr;}
 bool keyPressed(const juce::KeyPress& key,juce::Component*)override{if(key.getKeyCode()!=juce::KeyPress::escapeKey||!isFullScreen())return false;setFullScreen(false);return true;}
 juce::File profileFolder;juce::String id,error;bool testing=false,requested=false,transportPlaying=false,automaticFake=true;double requestedSeconds=0;
 unsigned session=0;int command=0,lastSequence=0,fakeSequence=0;
 juce::Component::SafePointer<juce::Component> fullscreenHost;juce::Rectangle<int> restoreBounds;bool restoreFullscreen=false;platform::VideoFullscreen fullscreenFrame;
};
}
