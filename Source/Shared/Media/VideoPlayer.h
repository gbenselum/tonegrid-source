#pragma once
#include "Shared/Media/VideoContent.h"
#include "Shared/UI/UiStyle.h"
#include "Shared/Media/VideoFullscreen.h"
#if JUCE_WEB_BROWSER
#include <juce_gui_extra/juce_gui_extra.h>
#if JUCE_MAC
#include "Platform/VideoBrowserMac.h"
#endif
#endif
namespace tonegrid::media
{
inline juce::String embedAddress(const juce::String& address)
{return safeTrackUrl(address)?"https://www.youtube.com/embed/"+videoId(address)+"?autoplay=1&playsinline=1&rel=0&widget_referrer=https%3A%2F%2Fwww.tonegrid.net%2F":juce::String{};}

inline juce::String playerDocument(const Track& track,const juce::String& origin)
{
    const auto url=embedAddress(track.url)+"&enablejsapi=1&origin="+juce::URL::addEscapeChars(origin,true);
    const auto html=juce::String("<!doctype html><html><head><meta name='referrer' content='strict-origin-when-cross-origin'><style>html,body{margin:0;width:100%;height:100%;background:#000;overflow:hidden}iframe{width:100%;height:100%;border:0}</style></head><body><iframe id='video' src='")+url+"' allow='autoplay; encrypted-media; picture-in-picture; fullscreen' allowfullscreen></iframe><script>var player,state=-1,error=0;document.addEventListener('fullscreenchange',function(){window.__JUCE__.backend.emitEvent('tonegridFullscreen',!!document.fullscreenElement)});function onYouTubeIframeAPIReady(){player=new YT.Player('video',{events:{onReady:function(e){e.target.playVideo()},onStateChange:function(e){state=e.data},onError:function(e){error=e.data}}})};var tag=document.createElement('script');tag.src='https://www.youtube.com/iframe_api';document.head.appendChild(tag);</script></body></html>";
    return html;
}
inline constexpr int playerWidth=356,playerHeight=200,accordionHeight=232;
class Player final:public juce::Component,private juce::ComponentListener,private juce::KeyListener
{
#if JUCE_WEB_BROWSER
    class Browser final:public juce::WebBrowserComponent
    {
    public:
        Browser(Player& value,const Options& options):WebBrowserComponent(options),owner(value){}
        bool pageAboutToLoad(const juce::String& address)override
        {
            if(address=="about:blank"||address.startsWith("https://com.personalaudiolab.tonexstateprobe/")||address.startsWith("https://www.youtube.com/embed/")||address.startsWith(WebBrowserComponent::getResourceProviderRoot()))return true;
            if(address.startsWith("https://consent.youtube.com/")||address.startsWith("https://consent.google.com/"))return true;return false;
        }
        bool pageLoadHadNetworkError(const juce::String& reason)override{owner.setFullScreen(false);owner.networkError=reason;owner.error="Could not load YouTube. Check your connection or open the video in your browser.";owner.requested=false;setVisible(false);owner.repaint();return false;}
        void newWindowAttemptingToLoad(const juce::String& address)override{if(safeTrackUrl(address)&&owner.openExternal)owner.openExternal(address);}
        void pageFinishedLoading(const juce::String& address)override{if(address.startsWith("https://com.personalaudiolab.tonexstateprobe/")||address.startsWith("https://www.youtube.com/embed/")||address.startsWith(WebBrowserComponent::getResourceProviderRoot()))owner.loaded=true;}
    private:Player& owner;
    };
#endif
public:
    Player(){setComponentID("jam-youtube-player");setOpaque(true);}
    ~Player()override{stop();}
    void configure(const juce::File& folder,bool test=false){profile=folder;testing=test;}
    void start(const Track& track)
    {stop();current=track;requested=safeTrackUrl(track.url);pending=requested;error.clear();networkError.clear();repaint();}
    void stop()
    {
        ++session;
        setFullScreen(false);
        requested=pending=loaded=false;
#if JUCE_WEB_BROWSER
        // Destroy the previous view instead of racing Stop/about:blank against the
        // next navigation. Its delayed cancellation callbacks cannot affect a new
        // playback session, and closing the native controller also stops its audio.
        browser.reset();
#endif
        repaint();
    }
    void suspend(){if(requested){stop();error="Playback stopped. Scroll to the player or enlarge the window, then click Play.";}}
    void showPlayer(bool fullyVisible)
    {
        if(isFullScreen())return;
        if(!fullyVisible){suspend();return;}if(!pending||!requested)return;pending=false;
        if(testing){loaded=true;repaint();return;}
#if JUCE_WEB_BROWSER
        const auto generation=session;
        auto options=juce::WebBrowserComponent::Options{}.withKeepPageLoadedWhenBrowserIsHidden()
            .withNativeIntegrationEnabled()
            .withEventListener("tonegridFullscreen",[safe=juce::Component::SafePointer<Player>(this),generation](juce::var active)
            {
                if(!active.isBool())return;
                juce::MessageManager::callAsync([safe,generation,full=bool(active)]{if(safe&&safe->session==generation&&safe->requested)safe->setFullScreen(full);});
            });
#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
        options=options.withResourceProvider([this](const juce::String&)->std::optional<juce::WebBrowserComponent::Resource>
        {
            const auto origin=juce::WebBrowserComponent::getResourceProviderRoot().trimCharactersAtEnd("/");
            const auto html=playerDocument(current,origin);
            std::vector<std::byte> data(size_t(html.getNumBytesAsUTF8()));std::memcpy(data.data(),html.toRawUTF8(),data.size());return juce::WebBrowserComponent::Resource{std::move(data),"text/html"};
        });
#endif
#if JUCE_WINDOWS
        if(profile.createDirectory().failed()){error="Cannot create the YouTube browser profile.";requested=false;repaint();return;}
        options=options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2).withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder(profile).withStatusBarDisabled().withBackgroundColour(juce::Colours::black));
#endif
        if(!juce::WebBrowserComponent::areOptionsSupported(options)){error="Microsoft Edge WebView2 is required for the inline player. Open on YouTube is still available.";requested=false;repaint();return;}
        if(!browser)browser=std::make_unique<Browser>(*this,options);addAndMakeVisible(*browser);browser->setBounds(getLocalBounds());
        // Desktop embeds need a stable client referrer; no access tokens or account API is used.
        const juce::StringArray headers{"Referer: https://com.personalaudiolab.tonexstateprobe/"};
#if JUCE_MAC
        // A juce:// document cannot supply the HTTPS client referrer required by
        // YouTube. WKWebView supports a local HTML document with an HTTPS base.
        const juce::String base="https://com.personalaudiolab.tonexstateprobe/";
        if(!tonegrid::platform::loadVideoDocument(*browser,playerDocument(current,base.trimCharactersAtEnd("/")),base))
        {error="Could not initialize the native video player.";requested=false;browser.reset();repaint();return;}
#elif JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
        browser->goToURL(juce::WebBrowserComponent::getResourceProviderRoot()+"player/"+current.id,&headers);
#else
        browser->goToURL(embedAddress(current.url),&headers);
#endif
#else
        loaded=true;
#endif
        repaint();
    }
    bool isRequested()const{return requested;}
    bool isLoaded()const{return loaded;}
    juce::String currentId()const{return current.id;}
    juce::String lastNetworkError()const{return networkError;}
    bool isFullScreen()const{
#if JUCE_MAC && JUCE_WEB_BROWSER
        if(browser&&platform::browserHasNativeFullscreen(*browser))return true;
#endif
        return fullscreenHost!=nullptr;
    }
    void visibilityChanged()override{if(!testing&&!isShowing())suspend();}
    void resized()override
    {
#if JUCE_WEB_BROWSER
        if(browser&&!isFullScreen())browser->setBounds(getLocalBounds());
#endif
    }
    void paint(juce::Graphics& g)override
    {
        g.fillAll(juce::Colours::black);g.setColour(ui::secondaryInk());g.setFont(ui::font(13));g.drawFittedText(error.isNotEmpty()?error:testing&&requested?"YouTube player preview\n"+juce::String(getWidth())+" x "+juce::String(getHeight()):"Click Play to start the video",getLocalBounds().reduced(14),juce::Justification::centred,4);
    }
    std::function<void(const juce::String&)> openExternal;
#if JUCE_WEB_BROWSER
    void sendFullscreenEventForTest(bool active)
    {if(browser)browser->evaluateJavascript(juce::String("window.__JUCE__.backend.emitEvent('tonegridFullscreen',")+(active?"true":"false")+")");}
    void escapeFullscreenForTest(){keyPressed(juce::KeyPress(juce::KeyPress::escapeKey),this);}
    void inspect(std::function<void(juce::var)> callback)
    {if(browser)browser->evaluateJavascript("JSON.stringify({url:location.href,width:innerWidth,height:innerHeight,ready:document.readyState,text:document.body.innerText.substring(0,600),error:window.error||0,videoId:window.player&&player.getVideoData?player.getVideoData().video_id:'',videos:window.player&&player.getCurrentTime?[{paused:player.getPlayerState()!=1,time:player.getCurrentTime()}]:[]})",[callback](juce::WebBrowserComponent::EvaluationResult result){if(auto* value=result.getResult())callback(*value);else callback({});});}
#else
    void sendFullscreenEventForTest(bool active){juce::ignoreUnused(active);}
    void escapeFullscreenForTest(){keyPressed(juce::KeyPress(juce::KeyPress::escapeKey),this);}
    void inspect(std::function<void(juce::var)> callback){if(callback)callback({});}
#endif
private:
    void setFullScreen(bool active)
    {
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
        if(active)
        {
            auto* host=getTopLevelComponent();auto* peer=host->getPeer();if(!browser||!peer)return;
            fullscreenHost=host;restoreBounds=host->getBounds();restoreFullscreen=peer->isFullScreen();
            host->addComponentListener(this);host->addKeyListener(this);
            // Keep the same browser, native peer and playback session. A different
            // top-level window would leave WebView2 attached to its original HWND.
            host->addAndMakeVisible(*browser);if(!fullscreenFrame.enter(peer->getNativeHandle()))peer->setFullScreen(true);browser->setBounds(host->getLocalBounds());browser->toFront(true);
        }
        else if(auto* host=fullscreenHost.getComponent())
        {
            host->removeComponentListener(this);host->removeKeyListener(this);fullscreenHost=nullptr;
            if(browser){browser->evaluateJavascript("if(document.fullscreenElement)document.exitFullscreen().catch(function(){})");addAndMakeVisible(*browser);browser->setBounds(getLocalBounds());}
            if(!fullscreenFrame.leave())if(auto* peer=host->getPeer())peer->setFullScreen(restoreFullscreen);
            if(!restoreFullscreen)host->setBounds(restoreBounds);
        }
#else
        juce::ignoreUnused(active);
#endif
    }
    void componentMovedOrResized(juce::Component& host,bool,bool)override
    {
#if JUCE_WEB_BROWSER
        if(fullscreenHost==&host&&browser)browser->setBounds(host.getLocalBounds());
#endif
    }
    void componentBeingDeleted(juce::Component& host)override{if(fullscreenHost==&host)fullscreenHost=nullptr;}
    bool keyPressed(const juce::KeyPress& key,juce::Component*)override
    {if(key.getKeyCode()!=juce::KeyPress::escapeKey||!isFullScreen())return false;setFullScreen(false);return true;}
    juce::File profile;Track current;juce::String error,networkError;bool testing=false,requested=false,pending=false,loaded=false;
    juce::Component::SafePointer<juce::Component> fullscreenHost;juce::Rectangle<int> restoreBounds;bool restoreFullscreen=false;unsigned session=0;
    platform::VideoFullscreen fullscreenFrame;
#if JUCE_WEB_BROWSER
    std::unique_ptr<Browser> browser;
#endif
};
}
