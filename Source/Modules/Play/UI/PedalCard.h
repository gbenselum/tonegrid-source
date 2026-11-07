#pragma once
#include "Shared/UI/UiMotion138.h"
#include "MidiBindings.h"
#include "Effects/Pedalboard/PedalCommands.h"
#include "Shared/UI/UiStyle.h"
#include "CalibrationStatus.h"
#include "Modules/Play/UI/PedalAssetSkin.h"
#include <BinaryData.h>
namespace tonegrid
{
class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        using namespace juce;
        setColour(ComboBox::backgroundColourId,ui::background());setColour(ComboBox::outlineColourId,ui::outline());
        setColour(TextButton::buttonColourId,ui::field());setColour(TextButton::textColourOffId,ui::ink());
        setColour(TextButton::textColourOnId,ui::ink());setColour(Label::textColourId,ui::ink());setColour(ComboBox::textColourId,ui::ink());
        setColour(Slider::textBoxTextColourId,ui::ink());setColour(Slider::textBoxBackgroundColourId,Colours::transparentBlack);
        setColour(Slider::textBoxOutlineColourId,Colours::transparentBlack);
        setColour(PopupMenu::backgroundColourId,Colour(0xff10222c));setColour(TextEditor::backgroundColourId,Colour(0xff091b23));
        setColour(TextEditor::outlineColourId,ui::outline());setColour(Slider::textBoxOutlineColourId,Colours::transparentBlack);
    }
    juce::Font getComboBoxFont(juce::ComboBox&)override{return ui::font();}
    juce::Font getPopupMenuFont()override{return ui::font(ui::primarySize);}
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox& box)override
    {ui::paintControl(g,{0.f,0.f,float(w),float(h)},false,box.isMouseOver(),false,box.isEnabled(),box.hasKeyboardFocus(true));ui::paintChevron(g,box.getLocalBounds().toFloat(),ui::secondaryInk().withMultipliedAlpha(box.isEnabled()?1.f:ui::disabledOpacity));}
    juce::Font getTextButtonFont(juce::TextButton& b,int)override{return ui::font(b.getComponentID()=="pedal-midi"?juce::jlimit(9.f,11.f,b.getHeight()*.44f):ui::controlSize);}
    void positionComboBoxText(juce::ComboBox& box,juce::Label& label)override
    {if(box.getComponentID()=="pedal-preset"){tonegrid::captureui::position(box,label);return;}label.setFont(getComboBoxFont(box));label.setBounds(ui::buttonPadding,0,juce::jmax(0,box.getWidth()-ui::buttonPadding-28),box.getHeight());label.setJustificationType(juce::Justification::centredLeft);label.setBorderSize({0,0,0,0});if(box.getComponentID()=="midi-input"){label.setComponentID("midi-input-text");label.setMinimumHorizontalScale(1.f);}}
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box,juce::Label& label)override
    {return box.getComponentID()=="pedal-preset"?tonegrid::captureui::options(box,label,getPopupMenuFont()):LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box,label);}
    void drawLabel(juce::Graphics& g,juce::Label& label)override
    {
        if(label.getComponentID()=="pedal-preset-text"){tonegrid::captureui::paint(g,label);return;}
        if(label.getComponentID()!="midi-input-text"){LookAndFeel_V4::drawLabel(g,label);return;}
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(label.isEnabled()?1.f:.5f));g.setFont(label.getFont());
        g.drawText(label.getText(),label.getBorderSize().subtractedFrom(label.getLocalBounds()),juce::Justification::centredLeft,true);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider& slider)override
    {
        using namespace juce;auto r=Rectangle<float>((float)x,(float)y,(float)w,(float)h).withSizeKeepingCentre((float)jmin(w,h)-6,(float)jmin(w,h)-6);
        g.setColour(Colours::black.withAlpha(.7f));g.fillEllipse(r.translated(1,3));g.setGradientFill(ColourGradient(Colour(0xffb7c0c4),r.getX(),r.getY(),Colour(0xff131a1d),r.getRight(),r.getBottom(),false));g.fillEllipse(r);
        r=r.reduced(2);g.setGradientFill(ColourGradient(Colour(0xff343e43),r.getX(),r.getY(),Colour(0xff020609),r.getRight(),r.getBottom(),false));g.fillEllipse(r);
        Path needle;needle.addRoundedRectangle(-1,-r.getHeight()*.45f,2,r.getHeight()*.34f,1);g.setColour(slider.isEnabled()?Colours::white:Colours::grey);g.fillPath(needle,AffineTransform::rotation(start+pos*(end-start)).translated(r.getCentreX(),r.getCentreY()));
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,const juce::Slider::SliderStyle,juce::Slider&)override
    {
        using namespace juce;const float cx=x+w*.5f;g.setColour(Colour(0xff061219));g.fillRoundedRectangle(cx-3,(float)y,6,(float)h,2);g.setColour(Colour(0xff4e879b));g.drawHorizontalLine(y+h/2,cx-6,cx+6);
        auto r=Rectangle<float>(cx-7,pos-4,14,8);g.setGradientFill(ColourGradient(Colour(0xffbacbd1),r.getX(),r.getY(),Colour(0xff26343c),r.getX(),r.getBottom(),false));g.fillRoundedRectangle(r,2);g.setColour(Colour(0xff07171f));g.drawRoundedRectangle(r,2,1);g.setColour(Colours::white);g.drawHorizontalLine(roundToInt(pos),cx-4,cx+4);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour& c,bool hover,bool down)override
    {
        using namespace juce;auto r=b.getLocalBounds().toFloat().reduced(1);
        if(b.getComponentID()=="gate")
        {const bool on=b.getToggleState()&&b.isEnabled();g.setColour(on?ui::switchOn():ui::switchOff());g.fillRoundedRectangle(r,r.getHeight()/2);g.setColour(Colours::white);auto ball=r.reduced(3).withWidth(r.getHeight()-6);if(on)ball.setX(r.getRight()-r.getHeight()+3);g.fillEllipse(ball);return;}
        if(b.getComponentID()=="power")
        {ui::paintFootswitch(g,r,down,hover||b.hasKeyboardFocus(true));return;}
        ui::paintControl(g,b.getLocalBounds().toFloat(),b.getToggleState(),hover,down,b.isEnabled(),b.hasKeyboardFocus(true));juce::ignoreUnused(c);
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool over,bool down)override
    {
        if(b.getComponentID()=="preset-browser")
        {g.setColour(ui::ink().withMultipliedAlpha(b.isEnabled()?1.f:ui::disabledOpacity));g.setFont(ui::font());g.drawText(captureui::curtailed(b.getButtonText(),ui::font(),float(b.getWidth()-38)),b.getLocalBounds().reduced(ui::buttonPadding,0).withTrimmedRight(20),juce::Justification::centredLeft,false);ui::paintChevron(g,b.getLocalBounds().toFloat(),ui::secondaryInk());return;}
        if(b.getComponentID()=="guitar-calibration")
        {
            using namespace juce;using calibration::InterfaceStatus;
            const auto status=static_cast<InterfaceStatus>(int(b.getProperties()["interface-status"]));
            const float x=float(b.getWidth()-16),y=float(b.getHeight()/2);Path icon;
            g.setColour(status==InterfaceStatus::active?Colour(0xff8ab49f):status==InterfaceStatus::disabled?Colour(0xff9ba5a8):Colour(0xffd3b77f));
            if(bool(b.getProperties()["calibration-attention"]))
            {
                const float pulse=b.getProperties().contains("calibration-pulse")?float(b.getProperties()["calibration-pulse"]):1.f;
                g.setColour(Colour(0xfff0a34d).withAlpha(pulse*.18f));g.fillEllipse(x-7,y-7,14,14);
                g.setColour(Colour(0xfff0a34d).withAlpha(pulse));g.fillEllipse(x-4,y-4,8,8);
            }
            else if(status==InterfaceStatus::active){icon.startNewSubPath(x-4,y);icon.lineTo(x-1,y+3);icon.lineTo(x+5,y-4);g.strokePath(icon,PathStrokeType(1.6f));}
            else if(status==InterfaceStatus::verification){icon.addTriangle(x,y-5,x-5,y+4,x+5,y+4);g.strokePath(icon,PathStrokeType(1.2f));g.drawLine(x,y-2,x,y+1,1.f);g.fillEllipse(x-.6f,y+2,1.2f,1.2f);}
            else if(status==InterfaceStatus::disabled){g.drawEllipse(x-4,y-4,8,8,1.1f);g.drawLine(x-2,y,x+2,y,1.1f);}
            else g.fillEllipse(x-2,y-2,4,4);
            g.setColour(Colours::white);g.setFont(getTextButtonFont(b,b.getHeight()));g.drawText(b.getButtonText(),7,0,b.getWidth()-28,b.getHeight(),Justification::centredLeft);return;
        }
        if(b.getComponentID()=="refresh-midi"&&b.getButtonText().isEmpty())
        {
            using namespace juce;const float x=b.getWidth()*.5f,y=b.getHeight()*.5f;Path p;p.addCentredArc(x,y,6,6,0,.7f,5.6f,true);
            g.setColour(Colours::white);g.strokePath(p,PathStrokeType(1.3f));p.clear();p.startNewSubPath(x+1,y-6);p.lineTo(x+5,y-6);p.lineTo(x+5,y-2);g.strokePath(p,PathStrokeType(1.3f));return;
        }
        if(b.getComponentID()=="power"||b.getComponentID()=="gate")return;
        if(b.getComponentID()=="save")
        {using namespace juce;g.setColour(b.isEnabled()?Colours::white:Colours::grey);auto y=(float)b.getHeight()/2-7;g.drawRoundedRectangle(9,y,13,14,1,1.2f);g.drawRect(12.f,y+1,6.f,4.f,1.f);g.drawRect(12.f,y+9,7.f,5.f,1.f);g.setFont(getTextButtonFont(b,b.getHeight()));const auto full=b.getButtonText();const auto label=getTextButtonFont(b,b.getHeight()).getStringWidth(full)<=b.getWidth()-30?full:l10n::tr("toolbar.save_short");g.drawFittedText(label,27,0,b.getWidth()-30,b.getHeight(),Justification::centred,1,.8f);return;}
        LookAndFeel_V4::drawButtonText(g,b,over,down);
    }
};
class Meter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void update(float peak){level=juce::jmax(peak,level*.82f);if(peak>=1)clip=30;else if(clip>0)--clip;repaint();}
    void paint(juce::Graphics& g)override
    {using namespace juce;for(int i=0;i<8;++i){auto r=Rectangle<float>(2.f,(float)(7-i)*getHeight()/8+1,(float)getWidth()-4,(float)getHeight()/8-3);const bool on=Decibels::gainToDecibels(level,-100.f)>=-42.f+i*6.f || (i==7&&clip>0);auto c=i<5?Colour(0xff30f17d):i<7?Colours::orange:Colours::red;g.setColour(on?c:c.withAlpha(.15f));g.fillRoundedRectangle(r,2);}}
private:float level=0;int clip=0;
};
class LearnButton : public l10n::TextButton
{
public:
    using l10n::TextButton::TextButton;
    std::function<void()> onClear;
    void mouseDown(const juce::MouseEvent& e)override{context=e.mods.isPopupMenu();if(context){if(onClear)onClear();}else TextButton::mouseDown(e);}
    void mouseUp(const juce::MouseEvent& e)override{if(!context)TextButton::mouseUp(e);context=false;}
private:bool context=false;
};
}
// A personal capture remains a normal selectable menu item, with a separate
// accessible delete button. Factory items never receive a delete action.
class PersonalCaptureRow final:public juce::PopupMenu::CustomComponent
{
    class Trash final:public juce::Button
    {
    public:Trash():Button("Delete capture") {setTitle("Delete personal capture");}
        void paintButton(juce::Graphics& g,bool hover,bool) override
        {
            const float x=getWidth()*.5f,y=getHeight()*.5f;
            g.setColour(hover?juce::Colour(0xffe7a49e):juce::Colour(0xffbcc5c7));
            g.drawLine(x-5,y-4,x+5,y-4,1.3f);g.drawLine(x-2,y-6,x+2,y-6,1.3f);
            juce::Path p;p.startNewSubPath(x-4,y-3);p.lineTo(x-3,y+6);p.lineTo(x+3,y+6);p.lineTo(x+4,y-3);g.strokePath(p,juce::PathStrokeType(1.3f));
            for(float d:{-1.5f,1.5f})g.drawLine(x+d,y-1,x+d,y+4,1);
        }
    };
public:
    PersonalCaptureRow(juce::String label,std::function<void()> remove):CustomComponent(false),text(std::move(label))
    {
        addAndMakeVisible(select);addAndMakeVisible(trash);select.setTitle(text);select.onClick=[this]{triggerMenuItem();};
        select.setAlpha(0);trash.onClick=[remove=std::move(remove)]{juce::PopupMenu::dismissAllActiveMenus();juce::MessageManager::callAsync(remove);};
    }
    void getIdealSize(int& w,int& h) override {w=juce::jlimit(320,650,int(juce::Font(juce::FontOptions(13)).getStringWidthFloat(text))+82);h=30;}
    void resized() override{auto r=getLocalBounds();trash.setBounds(r.removeFromRight(34));select.setBounds(r);}
    void paint(juce::Graphics& g) override
    {
        if(isItemHighlighted()){g.setColour(juce::Colour(0xff354043));g.fillAll();}
        g.setColour(juce::Colour(0xffe5e8e7));g.setFont(juce::FontOptions(13));
        if(getItem()&&getItem()->isTicked){juce::Path p;p.startNewSubPath(7,15);p.lineTo(11,19);p.lineTo(18,10);g.strokePath(p,juce::PathStrokeType(1.5f));}
        g.drawText(text,25,0,getWidth()-64,getHeight(),juce::Justification::centredLeft,true);
    }
private:juce::String text;juce::TextButton select;Trash trash;
};
class CreatorCaptureHeader final:public juce::PopupMenu::CustomComponent
{
public:
    explicit CreatorCaptureHeader(juce::String value):CustomComponent(true),text(std::move(value)){setMouseCursor(juce::MouseCursor::PointingHandCursor);setTitle(text+" - open creator profile on TONE3000");}
    void getIdealSize(int& w,int& h)override{w=juce::jlimit(200,680,int(juce::GlyphArrangement::getStringWidth(tonegrid::ui::font(13,juce::Font::bold),text))+28);h=27;}
    void paint(juce::Graphics& g)override
    {
        if(isItemHighlighted()){g.fillAll(juce::Colour(0xff354043));}
        g.setColour(isItemHighlighted()?juce::Colour(0xff8dd2ff):tonegrid::ui::ink());g.setFont(tonegrid::ui::font(13,juce::Font::bold));
        g.drawText(text,getLocalBounds().reduced(12,0),juce::Justification::centredLeft,true);
    }
    static bool validProfile(const juce::String& url)
    {
        const juce::String prefix="https://www.tone3000.com/";
        const auto slug=url.substring(prefix.length());
        return url.startsWith(prefix)&&slug.isNotEmpty()&&slug.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-");
    }
private:juce::String text;
};
class PedalCard final : public juce::Component
{
public:
    using CardLook=tonegrid::Look;
    static int engineFor(int visual){return board::order[(size_t)visual];}
    PedalCard(int visual,board::Processor& engine,bool modernSkin=true):modern(modernSkin),id(visual),pedal(engineFor(visual)),processor(engine)
    {
        using namespace juce;setLookAndFeel(&look);
        if(!modern){int skinSize=0;const auto* skinData=BinaryData::getNamedResource(board::definition(pedal).skinResource,skinSize);
        skin=ImageCache::getFromMemory(skinData,skinSize);jackSkin=ImageCache::getFromMemory(BinaryData::compbody_png,BinaryData::compbody_pngSize);}
        for(auto* c:std::initializer_list<Component*>{&title,&power,&preset})addAndMakeVisible(c);
        if(modern)addChildComponent(learn);else addAndMakeVisible(learn);

        title.setComponentID("pedal-title");preset.setComponentID("pedal-preset");learn.setComponentID("pedal-midi");
        title.setText(String(board::definition(pedal).displayName),dontSendNotification);title.setJustificationType(Justification::centred);title.setColour(Label::textColourId,(id==1||id==5||id==9||id==11)?Colours::black:Colours::white);
        const StringArray labels=pedal==board::id(board::PedalId::AutoWah)?StringArray{"Sens","Range","Decay"}:pedal==board::id(board::PedalId::GraphicEq)?StringArray{"Low","Mid","High"}:pedal==board::id(board::PedalId::StudioComp)?StringArray{"Thr","Ratio","Level"}:pedal==board::id(board::PedalId::CleanBoost)?StringArray{"Boost","Mix","Level"}:StringArray{"Input trim","Tone","Level"};
        for(int k=0;k<3;++k)
        {
            auto& knob=knobs[(size_t)k];auto& label=knobLabels[(size_t)k];addAndMakeVisible(knob);addAndMakeVisible(label);label.setText(labels[k],dontSendNotification);label.setJustificationType(Justification::centred);
            knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);knob.setTextBoxStyle(Slider::NoTextBox,false,0,0);knob.setPopupDisplayEnabled(true,true,this);knob.setTooltip(labels[k]);
            const bool namStyle=board::isNam(pedal);
            knob.setRange(k==0?(pedal==board::id(board::PedalId::StudioComp)?-48:namStyle?-12:-18):k==1?(pedal==board::id(board::PedalId::StudioComp)?1:namStyle?-6:0):-24,k==0?(pedal==board::id(board::PedalId::StudioComp)?0:namStyle?12:18):k==1?(pedal==board::id(board::PedalId::StudioComp)?20:namStyle?6:100):24,k==1&&!namStyle&&pedal!=board::id(board::PedalId::StudioComp)?1:.1);
            if(namStyle&&k==1){knob.setDoubleClickReturnValue(true,0);knob.setTooltip("Post-NAM Tone: tilt at 1 kHz, -6 to +6 dB. Double-click for neutral.");}
            if(pedal==board::id(board::PedalId::AutoWah)){knob.setRange(k==2?50:0,k==2?800:100,1);knob.setTooltip(k==0?"Envelope sensitivity":k==1?"Wah sweep range":"Envelope release (ms)");}
            knob.setTextValueSuffix(pedal==board::id(board::PedalId::AutoWah)?(k==2?" ms":"%"):k==1?(pedal==board::id(board::PedalId::StudioComp)?":1":namStyle?" dB":"%") : " dB");knob.setEnabled(pedal>=0);
            knob.onValueChange=[this]{if(pedal<0)return;auto s=processor.get(pedal);if(pedal==board::id(board::PedalId::AutoWah)){s.wahSensitivity=(float)knobs[0].getValue();s.wahRange=(float)knobs[1].getValue();s.wahDecay=(float)knobs[2].getValue();processor.set(pedal,s);return;}s.input=(float)knobs[0].getValue();if(pedal==board::id(board::PedalId::StudioComp))s.ratio=(float)knobs[1].getValue();else if(board::isNam(pedal))s.tone=(float)knobs[1].getValue();else s.mix=(float)knobs[1].getValue()/100;s.output=(float)knobs[2].getValue();processor.set(pedal,s);};
        }
        if(pedal==board::id(board::PedalId::GraphicEq))
        {
            for(auto& knob:knobs)knob.setVisible(false);for(auto& label:knobLabels)label.setVisible(false);
            const StringArray hz{"100","200","400","800","1.6k","3.2k","6.4k"};
            for(int b=0;b<7;++b){auto& f=eqFaders[(size_t)b];auto& l=eqLabels[(size_t)b];addAndMakeVisible(f);addAndMakeVisible(l);f.setSliderStyle(Slider::LinearVertical);f.setTextBoxStyle(Slider::NoTextBox,false,0,0);f.setRange(-12,12,.1);f.setDoubleClickReturnValue(true,0);f.setTextValueSuffix(" dB");f.setPopupDisplayEnabled(true,true,this);f.setTooltip(hz[b]+" Hz / -12 to +12 dB. Double-click for flat.");f.setTitle("EQ "+hz[b]);f.onValueChange=[this,b]{auto state=processor.get(pedal);state.eqBands[(size_t)b]=(float)eqFaders[(size_t)b].getValue();processor.set(pedal,state);};l.setText(hz[b],dontSendNotification);l.setJustificationType(Justification::centred);}
        }
        addFactoryCaptures();
        if(modern)addMidiLearnAction();
        preset.setTitle(String(board::definition(pedal).displayName)+" capture");
        preset.setTextWhenNothingSelected("");preset.setEnabled(pedal>=0);
        preset.onChange=[this]
        {
            if(pedal<0)return;const int chosen=preset.getSelectedId();
            for(const auto& action:creatorActions)if(chosen==action.first){sync();if(onOpenCreator)onOpenCreator(action.second);return;}
            if(modern&&chosen==midiLearnItem){sync();if(onLearn)onLearn();return;}
            if(chosen==900001||chosen==900002){sync();if(onAddCapture)onAddCapture(chosen==900001);return;}
            if(chosen>=10000){const auto index=size_t(chosen-10000);sync();if(index<personal.size()&&onSelectCapture)onSelectCapture(personal[index].first);return;}
            auto s=board::defaults(pedal,chosen-1);s.enabled=processor.get(pedal).enabled;processor.set(pedal,s);sync();
        };
        power.setComponentID("power");power.setTitle(String(board::definition(pedal).displayName)+" bypass");power.setEnabled(pedal>=0);power.onClick=[this]{toggle();};
        learn.onClick=[this]{if(onLearn)onLearn();};learn.onClear=[this]{if(onClear)onClear();};learn.setTooltip("Learn a MIDI button. Right-click to clear the assignment.");
        if(assetPilot())
        {
            assetLook=std::make_unique<tonegrid::assets::Look>(activeSkin());setLookAndFeel(assetLook.get());
            title.setText({},dontSendNotification);title.setTitle(board::definition(pedal).displayName);
            for(size_t k=0;k<knobs.size();++k){knobLabels[k].setVisible(false);knobs[k].setComponentID(k==0?"input-trim":k==1?"tone":"level");knobs[k].getProperties().set("skin-knob-index",int(k));}
            learn.setButtonText("MIDI");
        }
        sync();
    }
    ~PedalCard()override{setLookAndFeel(nullptr);}
    void setPersonalCaptures(std::vector<std::pair<juce::String,juce::String>> entries)
    {
        if(!board::isNam(pedal))return;personal=std::move(entries);preset.clear(juce::dontSendNotification);
        addFactoryCaptures();
        if(!personal.empty())
        {
            preset.addSeparator();preset.addSectionHeading("My captures");
            for(size_t i=0;i<personal.size();++i)
            {
                const auto hash=personal[i].first;
                preset.getRootMenu()->addCustomItem(int(i)+10000,std::make_unique<PersonalCaptureRow>(personal[i].second,
                    [safe=juce::Component::SafePointer<PedalCard>(this),hash]{if(safe&&safe->onDeleteCapture)safe->onDeleteCapture(hash);}),nullptr,personal[i].second);
            }
        }
        preset.addSeparator();tonegrid::l10n::item(preset,"Add pedal pack from Tone3000...",900001);tonegrid::l10n::item(preset,"Open pedal folder...",900002);if(modern)addMidiLearnAction();sync();
    }
    void toggle(){if(pedal<0)return;board::togglePedal(processor,pedal);sync();}
    void addFactoryCaptures()
    {
        juce::String previous;creatorActions.clear();
        if(pedal<0)return;
        for(int n=0;n<board::presetCount(pedal);++n)
        {
            const auto section=processor.captureSection(pedal,n);
            if(section.isNotEmpty()&&section!=previous)
            {
                const auto url=processor.captureCreatorUrl(pedal,n);
                if(modern&&CreatorCaptureHeader::validProfile(url))
                {
                    const int action=creatorFirstItem+int(creatorActions.size());creatorActions.emplace_back(action,url);
                    preset.getRootMenu()->addCustomItem(action,std::make_unique<CreatorCaptureHeader>(section),nullptr,section);
                }
                else preset.addSectionHeading(section);
            }
            previous=section;preset.addItem(processor.label(pedal,n),n+1);
        }
    }
    void sync()
    {
        using namespace juce;if(pedal<0){learn.setEnabled(false);repaint();return;}
        const auto s=processor.get(pedal);
        int selectedCapture=s.preset+1;
        if(board::isNam(pedal)&&s.preset==board::presetCount(pedal)){selectedCapture=0;for(size_t i=0;i<personal.size();++i)if(personal[i].first==processor.identity(pedal,s.preset)){selectedCapture=int(i)+10000;break;}}
        preset.setSelectedId(preset.indexOfItemId(selectedCapture)>=0?selectedCapture:0,dontSendNotification);if(selectedCapture==0)preset.setText(processor.label(pedal,s.preset),dontSendNotification);knobs[0].setValue(s.input,dontSendNotification);knobs[1].setValue(pedal==board::id(board::PedalId::StudioComp)?s.ratio:board::isNam(pedal)?s.tone:s.mix*100,dontSendNotification);knobs[2].setValue(s.output,dontSendNotification);
        if(pedal==board::id(board::PedalId::AutoWah)){knobs[0].setValue(s.wahSensitivity,dontSendNotification);knobs[1].setValue(s.wahRange,dontSendNotification);knobs[2].setValue(s.wahDecay,dontSendNotification);}
        if(pedal==board::id(board::PedalId::GraphicEq))for(int b=0;b<7;++b)eqFaders[(size_t)b].setValue(s.eqBands[(size_t)b],dontSendNotification);
        power.setToggleState(s.enabled,dontSendNotification);power.setTooltip(s.enabled?"ON - bypass":"OFF - enable");preset.setTooltip({});
        if(modern)title.setTooltip({});
        else
        {
            String information=String(board::definition(pedal).displayName)+"\nType: "+(board::isNam(pedal)?"NAM Capture":"Algorithmic");
            if(board::isNam(pedal))information+="\n"+processor.modelFile(pedal,s.preset)+"\n"+processor.calibrationInfo(pedal,s.preset).description();
            information+="\nPreset: "+processor.label(pedal,s.preset);
            const auto details=processor.details(pedal,s.preset);if(details.isNotEmpty())information+="\n"+details;
            title.setTooltip(information);
        }
        repaint();
    }
    void midi(const rig::Binding& b,bool learning,bool available)
    {
        if(pedal<0)return;learn.setEnabled(available);if(assetPilot()){midiAssigned=b.valid()&&!learning;resized();}
        learn.setButtonText(learning?(assetPilot()?"Cancel":"Cancel Learn"):b.valid()?(assetPilot()?(b.kind==1?juce::String("PC "):juce::String("CC "))+juce::String(b.number)+" / Ch "+juce::String(b.channel):b.text()):(assetPilot()?"MIDI":"MIDI Learn"));
        learn.setTooltip((learning?"Press a controller button. Click to cancel.":b.valid()?b.text()+". Click to learn a new assignment.":"Learn a MIDI button.")+juce::String(" Right-click to clear the assignment."));
    }
    void animate(double seconds)
    {
        if(!modern||!isShowing()||!processor.get(pedal).enabled)return;
        ledIntensity=tonegrid::motion::ledOpacity(seconds);
        repaint(activeSkin().logicalToLocal(activeSkin().led,getLocalBounds()).toNearestInt().expanded(3));
    }
    void paint(juce::Graphics& g)override
    {
        using namespace juce;
        if(assetPilot())
        {
            activeSkin().paint(g,getLocalBounds(),processor.get(pedal).enabled,modern?ledIntensity:1.f);
            if(pedal==board::id(board::PedalId::GraphicEq)&&tonegrid::isBass(processor.instrumentMode())&&!eqFaders[0].getBounds().isEmpty())
            {
                const int stripY=eqFaders[0].getBottom()+1;
                const int stripX=eqFaders[0].getX()-4;
                const int stripW=eqFaders[6].getRight()+4-stripX;
                g.setColour(Colour(0xffe8e0d3));
                g.fillRoundedRectangle(float(stripX),float(stripY),float(stripW),15.f,2.f);
                static constexpr std::array<const char*,7> bassLabels{{"40","80","160","320","640","1.3K","2.6K"}};
                g.setColour(Colour(0xff1e1b18));
                g.setFont(tonegrid::ui::font(9.5f,true));
                for(size_t b=0;b<7;++b)
                {
                    const auto fb=eqFaders[b].getBounds();
                    g.drawText(bassLabels[b],fb.getX()-6,stripY,fb.getWidth()+12,15,Justification::centred,false);
                }
            }
            return;
        }
        g.drawImage(skin,face,RectanglePlacement::stretchToFit);
        auto led=box(105,19,14,14).toFloat();const bool on=pedal>=0&&processor.get(pedal).enabled;
        g.setColour(Colours::black);g.fillEllipse(led.expanded(1));if(on){g.setColour(Colour(0xffff3b32).withAlpha(.25f));g.fillEllipse(led.expanded(4));}g.setColour(on?Colour(0xffff382e):Colour(0xff56272a));g.fillEllipse(led);if(on){g.setColour(Colours::white);g.fillEllipse(led.reduced(3));}
        // Reuse the photographed metallic sockets from the previously approved enclosure.
        const std::array<int,13> left{13,15,16,17,8,9,12,11,8,8,8,8,8},right{210,208,208,207,216,215,212,213,216,216,216,216,216};
        for(int side=0;side<2;++side)
        {
            const int x=side==0?jmax(0,left[(size_t)id]-11):right[(size_t)id]-1;
            const int width=side==0?left[(size_t)id]-x+1:jmin(11,224-x);
            auto jack=box((float)x,94,(float)width,32);Graphics::ScopedSaveState saved(g);Path outline;outline.addRoundedRectangle(jack.toFloat(),(float)width*.2f);g.reduceClipRegion(outline);
            g.drawImage(jackSkin,jack.getX(),jack.getY(),jack.getWidth(),jack.getHeight(),side==0?169:813,725,44,130);
        }
    }
    void resized()override
    {
        using namespace juce;if(assetPilot())
        {
            const auto& asset=activeSkin();auto local=[&](Rectangle<float> logical){return asset.logicalToLocal(logical,getLocalBounds()).toNearestInt();};
            face=asset.faceBounds(getLocalBounds());title.setBounds(local(asset.titleHover));
            for(size_t k=0;k<knobs.size();++k)knobs[k].setBounds(local(asset.knobs[k]));
            if(pedal==board::id(board::PedalId::GraphicEq))for(size_t b=0;b<eqFaders.size();++b){eqFaders[b].setComponentID("eq-band-"+String((int)b));eqFaders[b].setBounds(local(asset.faders[b]));eqLabels[b].setVisible(false);}
            preset.setBounds(local(asset.preset));if(!modern)learn.setBounds(local(midiAssigned?asset.midi.withSizeKeepingCentre(asset.preset.getWidth(),asset.midi.getHeight()):asset.midi));power.setBounds(local(asset.footswitch));return;
        }
        using namespace juce;face=RectanglePlacement(RectanglePlacement::centred).appliedTo(Rectangle<float>(0,0,224,288),getLocalBounds().toFloat());
        const float scale=face.getWidth()/224;title.setFont(FontOptions("Segoe UI",17*scale,Font::bold));title.setBounds(box(24,34,176,22));
        for(int k=0;k<3;++k){knobs[(size_t)k].setBounds(box(26+k*57,61,52,49));knobLabels[(size_t)k].setFont(FontOptions("Segoe UI",11*scale,Font::plain));knobLabels[(size_t)k].setBounds(box(21+k*60,111,60,15));}
        for(int b=0;b<7;++b){eqFaders[(size_t)b].setBounds(box(25+b*25,62,24,45));eqLabels[(size_t)b].setBounds(box(24+b*25,109,26,13));eqLabels[(size_t)b].setFont(FontOptions("Segoe UI",8*scale,Font::plain));}
        // Compact controls sit above a real stomp switch, matching the approved face.
        preset.setBounds(box((224-tonegrid::ui::pedalControlWidth)/2,158,tonegrid::ui::pedalControlWidth,29));
        learn.setBounds(box((224-tonegrid::ui::pedalMidiWidth)/2,194,tonegrid::ui::pedalMidiWidth,25));
        power.setBounds(box(88,226,48,48));
    }

    std::function<void()> onLearn,onClear;
    static constexpr int midiLearnItem=900003,creatorFirstItem=900100;
    std::function<bool(const juce::String&)> onOpenCreator=[](const juce::String& url){return juce::URL(url).launchInDefaultBrowser();};
    std::function<void(bool)> onAddCapture;std::function<void(const juce::String&)> onSelectCapture,onDeleteCapture;
private:
    void addMidiLearnAction(){preset.addSeparator();tonegrid::l10n::item(preset,"LEARN MIDI",midiLearnItem);}
    const tonegrid::assets::Skin& activeSkin()const{return modern?tonegrid::assets::pedal137(id):tonegrid::assets::pedal(id);}
    std::vector<std::pair<int,juce::String>> creatorActions;
    std::vector<std::pair<juce::String,juce::String>> personal;
    juce::Rectangle<int> box(float x,float y,float w,float h)const{return juce::Rectangle<float>(face.getX()+x*face.getWidth()/224,face.getY()+y*face.getHeight()/288,w*face.getWidth()/224,h*face.getHeight()/288).toNearestInt();}
    bool assetPilot()const{return tonegrid::assets::pedal(id).valid;}
    CardLook look;std::unique_ptr<tonegrid::assets::Look> assetLook;float ledIntensity=1.f;bool midiAssigned=false,modern=true;int id,pedal;board::Processor& processor;juce::Image skin,jackSkin;juce::Rectangle<float> face;

    std::array<juce::Slider,7> eqFaders;std::array<juce::Label,7> eqLabels;
    juce::Label title;std::array<juce::Label,3> knobLabels;std::array<juce::Slider,3> knobs;juce::ComboBox preset;juce::TextButton power;tonegrid::LearnButton learn{"MIDI Learn"};
};
