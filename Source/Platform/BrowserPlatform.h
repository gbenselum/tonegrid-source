#pragma once
#include <juce_core/juce_core.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if !JUCE_WEB_BROWSER
namespace juce
{
class WebBrowserComponent : public Component
{
public:
    class Options
    {
    public:
        enum class Backend { defaultBackend, ie, webview2 };
        Options withKeepPageLoadedWhenBrowserIsHidden() const noexcept { return *this; }
        Options withBackend(Backend) const noexcept { return *this; }
    };
    explicit WebBrowserComponent(const Options& = {}) {}
    static bool areOptionsSupported(const Options&) noexcept { return false; }
    void goToURL(const String&, const StringArray* = nullptr, const MemoryBlock* = nullptr) {}
    void stop() {}
    virtual bool pageAboutToLoad(const String&) { return true; }
    virtual void pageFinishedLoading(const String&) {}
    virtual void windowCloseRequest() {}
    virtual void newWindowAttemptingToLoad(const String&) {}
    virtual bool pageLoadHadNetworkError(const String&) { return false; }
};
}
#endif

namespace tonegrid::platform
{
inline juce::String browserErrorDetail(const juce::String& error)
{
    if(!error.startsWith("Error code: "))return {};
    const auto code=juce::String(error.fromFirstOccurrenceOf("Error code: ",false,false).getIntValue());
#if JUCE_WINDOWS
    return " (WebView2 code "+code+")";
#else
    return " (browser code "+code+")";
#endif
}
inline juce::String browserUnavailableMessage()
{
#if JUCE_WINDOWS
    return "Microsoft Edge WebView2 Runtime is required for Tone3000. The local library remains available.";
#else
    return "The embedded browser is unavailable. Retry or return to your local NAM library.";
#endif
}
#if JUCE_MAC && JUCE_WEB_BROWSER
bool loadEncodedBrowserUrl(juce::WebBrowserComponent&, const juce::String&);
#endif
// The address is already encoded by the OAuth builder or browser navigation.
inline bool navigateEncodedBrowserUrl(juce::WebBrowserComponent& browser, const juce::String& address)
{
#if JUCE_MAC && JUCE_WEB_BROWSER
    return loadEncodedBrowserUrl(browser,address);
#else
    browser.goToURL(address);return true;
#endif
}
inline juce::WebBrowserComponent::Options browserOptions(const juce::File& profile)
{
    using Browser=juce::WebBrowserComponent;
    auto options=Browser::Options{}.withKeepPageLoadedWhenBrowserIsHidden();
#if JUCE_WINDOWS && JUCE_WEB_BROWSER
    return options.withBackend(Browser::Options::Backend::webview2)
        .withWinWebView2Options(Browser::Options::WinWebView2{}.withUserDataFolder(profile).withStatusBarDisabled().withBuiltInErrorPageDisabled());
#else
    juce::ignoreUnused(profile);
    // On macOS JUCE uses WKWebView and its native website data store.
    return options.withBackend(Browser::Options::Backend::defaultBackend);
#endif
}
}
