#pragma once
#include "AmpUi.h"
#include "NamAmpState.h"
#include "CompactAmpLayout.h"
#include "Shared/UI/UiStyle.h"
#include "Content/Artwork/AmpPreviewImage.h"
#include <BinaryData.h>
#include <ToneGridApplicationAssets.h>

namespace tonegrid::ampui
{
inline void paintSettingsFrame(juce::Graphics& g,juce::Rectangle<float> r)
{ui::paintControl(g,r,false,false,false,true);}
inline void paintGear(juce::Graphics& g,juce::Rectangle<float> bounds,juce::Colour colour)
{using namespace juce;auto r=bounds.reduced(bounds.getWidth()*.30f);g.setColour(colour);g.drawEllipse(r,1.4f);g.drawEllipse(r.reduced(r.getWidth()*.3f),1.4f);for(int i=0;i<8;++i){const auto a=i*MathConstants<float>::twoPi/8;g.drawLine(r.getCentreX()+std::sin(a)*r.getWidth()*.42f,r.getCentreY()+std::cos(a)*r.getHeight()*.42f,r.getCentreX()+std::sin(a)*r.getWidth()*.65f,r.getCentreY()+std::cos(a)*r.getHeight()*.65f,1.5f);}}
class Look final:public juce::LookAndFeel_V4
{
public:
    juce::Font getTextButtonFont(juce::TextButton& b,int) override
    {return ui::font(b.getComponentID()=="amp-normalize"?11.f:ui::controlSize,b.getComponentID().startsWith("amp-source")?ui::emphasisWeight:ui::regularWeight);}
    juce::Font getComboBoxFont(juce::ComboBox&) override{return ui::font(11.f);}
    void drawComboBox(juce::Graphics& g,int w,int h,bool down,int,int,int,int,juce::ComboBox& box) override
    {
        ui::paintControl(g,{0.f,0.f,float(w),float(h)},false,box.isMouseOver(),down,box.isEnabled(),box.hasKeyboardFocus(true));
        g.setColour(ui::ink().withMultipliedAlpha(box.isEnabled()?1.f:ui::disabledOpacity));
        juce::Path arrow;arrow.startNewSubPath(float(w)-17,float(h)*.43f);arrow.lineTo(float(w)-12,float(h)*.58f);arrow.lineTo(float(w)-7,float(h)*.43f);g.strokePath(arrow,juce::PathStrokeType(1.3f));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider& s) override
    {
        using namespace juce;auto r=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h).withSizeKeepingCentre((float)jmin(w,h)-jmin(10.f,jmin(w,h)*.07f),(float)jmin(w,h)-jmin(10.f,jmin(w,h)*.07f));
        g.setColour(Colours::black.withAlpha(.75f));g.fillEllipse(r.translated(2,5));g.setColour(Colour(0xff030609));g.fillEllipse(r);
        Path arc;arc.addCentredArc(r.getCentreX(),r.getCentreY(),r.getWidth()*.52f,r.getHeight()*.52f,0,start,start+pos*(end-start),true);
        g.setColour(s.isEnabled()?Colour(0xff89979e):Colour(0xff36444c));g.strokePath(arc,PathStrokeType(3.f));
        // Keep the same metal cap and pointer while scaling bevels with the shorter rack.
        r=r.reduced(jmin(9.f,r.getWidth()*.08f));g.setGradientFill(ColourGradient(Colour(0xffe5e7e7),r.getX(),r.getY(),Colour(0xff565859),r.getRight(),r.getBottom(),false));g.fillEllipse(r);
        const auto cx=r.getCentreX(),cy=r.getCentreY(),radius=r.getWidth()*.5f;
        for(int i=0;i<90;++i){const auto a=i*MathConstants<float>::twoPi/90;g.setColour(Colours::white.withAlpha(.035f+.10f*std::abs(std::cos(a*2))));g.drawLine(cx,cy,cx+radius*std::sin(a),cy+radius*std::cos(a),.7f);}
        g.setColour(Colour(0xffb6babb));g.drawEllipse(r,1);Path needle;needle.addRoundedRectangle(-1.2f,-radius+4,2.4f,radius*.42f,1);
        g.setColour(Colours::white);g.fillPath(needle,AffineTransform::rotation(start+pos*(end-start)).translated(cx,cy));
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down) override
    {
        using namespace juce;auto r=b.getLocalBounds().toFloat().reduced(1);const bool on=b.getToggleState()&&b.isEnabled();
        if(b.getComponentID()=="instrument-mode")
        {
            g.setColour(Colour(0xff081218));
            g.fillRoundedRectangle(r, 6.f);
            auto segW = r.getWidth() * 0.5f;
            auto activeSeg = on ? r.withLeft(r.getX() + segW).reduced(1.5f) : r.withWidth(segW).reduced(1.5f);
            const auto accent = on ? Colour(0xff195670) : Colour(0xff1d3d4f);
            const auto border = on ? Colour(0xff29b6f6) : Colour(0xff4ea6c9);
            g.setColour(accent.withMultipliedAlpha(down ? 0.85f : (hover ? 1.1f : 1.0f)));
            g.fillRoundedRectangle(activeSeg, 4.5f);
            g.setColour(border.withAlpha(0.85f));
            g.drawRoundedRectangle(activeSeg, 4.5f, 1.1f);
            g.setColour(Colour(0xff263a47));
            g.drawRoundedRectangle(r, 6.f, 1.f);
            return;
        }
        if(b.getComponentID().endsWith("-settings")){ui::paintControl(g,b.getLocalBounds().toFloat(),false,hover,down,b.isEnabled(),b.hasKeyboardFocus(true));return;}
        if(b.getComponentID().endsWith("-toggle"))
        {g.setColour(on?(b.isColourSpecified(TextButton::buttonOnColourId)?b.findColour(TextButton::buttonOnColourId):ui::switchOn()):ui::switchOff());g.fillRoundedRectangle(r,r.getHeight()*.5f);auto ball=r.reduced(4).withWidth(r.getHeight()-8);if(on)ball.setX(r.getRight()-r.getHeight()+4);g.setColour(Colours::white);g.fillEllipse(ball);return;}
        ui::paintControl(g,b.getLocalBounds().toFloat(),on&&b.getComponentID().startsWith("amp-source"),hover,down,b.isEnabled(),b.hasKeyboardFocus(true));
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool over,bool down) override
    {
        using namespace juce;const auto id=b.getComponentID();g.setColour(ui::ink().withMultipliedAlpha(b.isEnabled()?1.f:ui::disabledOpacity));
        if(id=="instrument-mode")
        {
            const bool bass = b.getToggleState();
            const auto bounds = b.getLocalBounds();
            const int half = bounds.getWidth() / 2;
            g.setFont(ui::font(11.5f, true));
            g.setColour(bass ? ui::secondaryInk().withAlpha(0.65f) : Colours::white);
            g.drawText("GUITAR", bounds.withWidth(half), Justification::centred, false);
            g.setColour(bass ? Colours::white : ui::secondaryInk().withAlpha(0.65f));
            g.drawText("BASS", bounds.withTrimmedLeft(half), Justification::centred, false);
            return;
        }
        if(id.endsWith("-toggle"))return;
        if(id=="tuner")
        {auto r=b.getLocalBounds().toFloat().reduced(7);Path fork;fork.startNewSubPath(r.getX()+3,r.getY());fork.lineTo(r.getX()+3,r.getCentreY());fork.quadraticTo(r.getCentreX(),r.getBottom(),r.getRight()-3,r.getCentreY());fork.lineTo(r.getRight()-3,r.getY());fork.startNewSubPath(r.getCentreX(),r.getCentreY()+4);fork.lineTo(r.getCentreX(),r.getBottom()+2);g.strokePath(fork,PathStrokeType(1.8f,PathStrokeType::curved,PathStrokeType::rounded));return;}
        if(id=="nam-folder")
        {const auto r=folderGlyphBounds(b.getLocalBounds());Path folder;
            folder.startNewSubPath(1,19);folder.lineTo(1,3);folder.lineTo(9,3);folder.lineTo(12,6);folder.lineTo(23,6);folder.lineTo(23,19);folder.closeSubPath();
            folder.startNewSubPath(1,8);folder.lineTo(23,8);
            folder.applyTransform(AffineTransform::scale(r.getWidth()/24.f,r.getHeight()/20.f).translated(r.getX(),r.getY()));
            g.strokePath(folder,PathStrokeType(1.4f,PathStrokeType::curved,PathStrokeType::rounded));return;}
        if(id.endsWith("-settings"))
        {paintGear(g,b.getLocalBounds().toFloat(),ui::ink().withMultipliedAlpha(b.isEnabled()?1.f:ui::disabledOpacity));return;}
        g.setFont(getTextButtonFont(b,b.getHeight()));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(5,1),Justification::centred,1);
        juce::ignoreUnused(over,down);
    }
};
class Thumbnail final:public juce::Component
{
public:
    void setLeftAligned(bool value){leftAligned=value;repaint();}
    juce::Rectangle<float> artworkBounds()const
    {auto r=getLocalBounds().toFloat().reduced(3);if(leftAligned)r=getLocalBounds().toFloat().withTrimmedBottom(6);return r;}
    void setImage(juce::Image image){artwork.set(std::move(image));repaint();}
    void setTone3000(bool source){tone3000=source;repaint();}
    bool hasTone3000Mark() const{return tone3000;}
    bool usesFallback() const{return !artwork.valid();}
    void paint(juce::Graphics& g) override
    {
        using namespace juce;auto r=artworkBounds();
        if(artwork.valid()){artwork.draw(g,r,leftAligned?RectanglePlacement::xLeft|RectanglePlacement::yMid:RectanglePlacement::centred);paintSourceMark(g,r);return;}
        auto head=r.withTrimmedBottom(r.getHeight()*.10f);g.setColour(Colour(0xff000204));g.fillRoundedRectangle(head.translated(2,4),5);g.setGradientFill(ColourGradient(Colour(0xff69716f),head.getX(),head.getY(),Colour(0xff101719),head.getRight(),head.getBottom(),false));g.fillRoundedRectangle(head,5);
        auto face=head.reduced(4);g.setColour(Colour(0xff172022));g.fillRoundedRectangle(face,3);g.setColour(Colours::white.withAlpha(.04f));for(int y=(int)face.getY();y<face.getBottom();y+=3)g.drawHorizontalLine(y,face.getX(),face.getRight());
        g.setColour(Colour(0xffdae5e9));g.setFont(FontOptions("Segoe UI",r.getHeight()*.12f,Font::bold));g.drawText("TONEGRID",face.withTrimmedTop(face.getHeight()*.30f).withHeight(face.getHeight()*.3f),Justification::centred);
        auto controls=face.withTrimmedTop(face.getHeight()*.76f).reduced(6,2);g.setGradientFill(ColourGradient(Colour(0xffb2a17d),controls.getX(),controls.getY(),Colour(0xff514c3d),controls.getX(),controls.getBottom(),false));g.fillRect(controls);
        for(int i=0;i<8;++i){g.setColour(Colour(0xff10181a));const auto d=controls.getHeight()*.45f;g.fillEllipse(controls.getX()+controls.getWidth()*(i+1)/9-d*.5f,controls.getCentreY()-d*.5f,d,d);}
        paintSourceMark(g,r);
    }
private:
    void paintSourceMark(juce::Graphics& g,juce::Rectangle<float> r)
    {
        if(!tone3000)return;const auto logo=juce::ImageCache::getFromMemory(ToneGridApplicationAssets::Tone3000_png,ToneGridApplicationAssets::Tone3000_pngSize);
        const auto w=juce::jmin(100.f,r.getWidth()*.62f),h=w*.2f;if(logo.isValid())g.drawImage(logo,r.getRight()-w,r.getBottom()-h,w,h,0,80,200,40);
    }
    PreviewImage artwork;bool tone3000=false,leftAligned=false;
};
inline void paintChassis(juce::Graphics& g,juce::Rectangle<int> panel,bool native=true)
{
    using namespace juce;const auto box=[&](float x,float y,float w,float h){return panelBox(panel,x,y,w,h).toFloat();};auto r=panel.toFloat().reduced(1);
    g.setColour(Colours::black.withAlpha(.4f));g.fillRoundedRectangle(r.translated(0,2),ui::panelRadius);
    g.setGradientFill(ColourGradient(ui::field(),r.getX(),r.getY(),ui::panel(),r.getX(),r.getBottom(),false));g.fillRoundedRectangle(r,ui::panelRadius);
    g.setColour(ui::outline());g.drawRoundedRectangle(r,ui::panelRadius,ui::stroke);
    g.setColour(ui::outline().withAlpha(.35f));g.drawRoundedRectangle(r.reduced(3),ui::panelRadius-3,ui::stroke);
    const auto reference=ImageCache::getFromMemory(BinaryData::ampreference_png,BinaryData::ampreference_pngSize);
    // Preserve the original two left screw images, without the heavy side-rail texture.
    // Preserve circular hardware by retaining the original top/bottom slice scale.
    // The right edge is the neutral rounded frame used by the approved reference.
    if(reference.isValid())
    {
        Graphics::ScopedSaveState saved(g);Path frame;frame.setUsingNonZeroWinding(false);frame.addEllipse(box(8,8,32,52));frame.addEllipse(box(8,128,32,52));g.reduceClipRegion(frame);
        const float srcScale=reference.getWidth()/2048.f,edge=r.getWidth()*44/2016;
        g.drawImage(reference,r.getX(),r.getY(),r.getWidth(),edge,19*srcScale,239*srcScale,2016*srcScale,44*srcScale);
        g.drawImage(reference,r.getX(),r.getY()+edge,r.getWidth(),r.getHeight()-2*edge,19*srcScale,283*srcScale,2016*srcScale,192*srcScale);
        g.drawImage(reference,r.getX(),r.getBottom()-edge,r.getWidth(),edge,19*srcScale,475*srcScale,2016*srcScale,44*srcScale);
    }
    for(float x:{252.f,1056.f,1496.f}){auto line=box(x,27,1,122);g.setColour(ui::separator().withAlpha(ui::separatorOpacity));g.fillRect(line);}
    const float textScale=panel.getWidth()/2016.f;g.setColour(ui::secondaryInk());g.setFont(ui::sectionFont());g.drawText("AMP / SOURCE",box(64,10,180,26),Justification::centredLeft);
    if(native){g.drawText("GAIN",gainBounds(panel).withY(panel.getY()+2).withHeight(18).toFloat(),Justification::centred);g.drawText("VOLUME",volumeBounds(panel).withY(panel.getY()+2).withHeight(18).toFloat(),Justification::centred);}
    const StringArray names{"3D SOUND","MOD","DELAY","REVERB"};
    for(int i=0;i<4;++i)
    {
        auto fx=box(postFxStart+i*postFxStep,37,112,106);
        g.setColour(ui::secondaryInk());g.setFont(ui::font(ui::microSize));g.drawText(names[i],fx.withHeight(38*textScale),Justification::centred);
    }
}
}
