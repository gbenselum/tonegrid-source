#pragma once
#include "Modules/Learn/Model/LearnCatalogue.h"
#include "Shared/Media/VideoPlayer.h"
#include "Shared/UI/PopupStyle.h"
#include <ToneGridApplicationAssets.h>
#include "Content/EditableContent.h"
#include <map>
namespace tonegrid::learn
{
inline juce::Colour blue(){return juce::Colour(0xffec66c9);}
inline constexpr int gridTop=266,cardTextHeight=174,cardGap=20;
inline int cardPhotoHeight(int width){return juce::jmin(228,width*9/16);}
inline void filterIcon(juce::Graphics& g,const juce::String& kind,float x,float y)
{
    using namespace juce;Path p;
    if(kind=="learn-search"){p.addEllipse(x-8,y-8,13,13);p.startNewSubPath(x+3,y+3);p.lineTo(x+9,y+9);}
    else if(kind=="learn-date"){p.addRoundedRectangle(x-8,y-7,16,15,2);p.startNewSubPath(x-8,y-2);p.lineTo(x+8,y-2);for(float dx:{-4.f,4.f}){p.startNewSubPath(x+dx,y-10);p.lineTo(x+dx,y-4);}p.addRectangle(x-4,y+1,2,2);p.addRectangle(x+1,y+1,2,2);}
    else if(kind=="learn-style"){p.addEllipse(x-8,y+1,9,9);p.addEllipse(x-4,y-3,8,8);p.startNewSubPath(x+1,y+1);p.lineTo(x+9,y-8);p.lineTo(x+6,y-10);p.lineTo(x-1,y-2);p.startNewSubPath(x-4,y+5);p.lineTo(x+8,y-8);}
    else{p.addEllipse(x-4,y-10,8,8);p.addRoundedRectangle(x-8,y+1,16,9,3);p.startNewSubPath(x+7,y-7);p.cubicTo(x+12,y-7,x+12,y-2,x+8,y-1);}
    g.setColour(ui::ink());g.strokePath(p,PathStrokeType(1.4f,PathStrokeType::curved,PathStrokeType::rounded));
}
class Look final:public juce::LookAndFeel_V4
{
public:
    Look()
    {
        using namespace juce;setColour(ComboBox::backgroundColourId,ui::field());setColour(ComboBox::textColourId,ui::ink());setColour(Label::textColourId,ui::ink());
        setColour(PopupMenu::backgroundColourId,ui::panel());setColour(PopupMenu::textColourId,ui::ink());setColour(PopupMenu::highlightedBackgroundColourId,ui::selected());setColour(PopupMenu::highlightedTextColourId,ui::ink());
        setColour(TextEditor::textColourId,ui::ink());setColour(TextEditor::highlightColourId,ui::selected());setColour(CaretComponent::caretColourId,ui::ink());setColour(ScrollBar::thumbColourId,ui::metadataInk());
    }
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox& box)override
    {ui::paintControl(g,{0.f,0.f,float(w),float(h)},false,box.isMouseOver(),false,true,box.hasKeyboardFocus(true));if(box.getComponentID()!="learn-parts")filterIcon(g,box.getComponentID(),23,float(h)/2);ui::paintChevron(g,box.getLocalBounds().toFloat(),ui::ink());}
    void positionComboBoxText(juce::ComboBox& box,juce::Label& label)override
    {const int inset=box.getComponentID()=="learn-parts"?12:44;label.setBounds(inset,1,box.getWidth()-inset-26,box.getHeight()-2);label.setFont(ui::font(15));}
    juce::Font getComboBoxFont(juce::ComboBox&)override{return ui::font(15);}
    void fillTextEditorBackground(juce::Graphics& g,int w,int h,juce::TextEditor& e)override
    {ui::paintControl(g,{0.f,0.f,float(w),float(h)},false,e.isMouseOver(),false,true,e.hasKeyboardFocus(true));filterIcon(g,e.getComponentID(),23,float(h)/2);}
    void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&)override{}
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool hover,bool)override
    {
        using namespace juce;g.setColour(hover?ui::secondaryInk():ui::metadataInk());g.drawRoundedRectangle(1,12,24,24,4,1);
        if(b.getToggleState()){Path tick;tick.startNewSubPath(6,24);tick.lineTo(11,29);tick.lineTo(20,18);g.setColour(blue());g.strokePath(tick,PathStrokeType(2));}
        Path star;star.addStar({46,24},5,4,9);g.setColour(b.getToggleState()?blue():ui::ink());g.strokePath(star,PathStrokeType(1.4f));g.setFont(ui::font(15));g.drawText(tonegrid::l10n::translate("Favourites"),61,0,b.getWidth()-61,b.getHeight(),Justification::centredLeft);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down)override
    {ui::paintControl(g,b.getLocalBounds().toFloat(),b.getToggleState(),hover,down,true,b.hasKeyboardFocus(true));}
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool)override
    {g.setColour(ui::ink());g.setFont(ui::font(14));g.drawText(b.getButtonText(),b.getLocalBounds().reduced(10,2),juce::Justification::centred,true);}
};
class ActionButton final:public juce::TextButton
{
public:
    enum Style {partner,star,link,back,resource};
    ActionButton(const juce::String& text,Style value):TextButton(text),style(value){}
    void paintButton(juce::Graphics& g,bool hover,bool down)override
    {
        using namespace juce;auto r=getLocalBounds().toFloat().reduced(.5f);
        if(style==partner){g.setGradientFill(ColourGradient(Colour(0xffd137e8).brighter(hover?.1f:0.f),r.getTopLeft(),Colour(0xff8c13da).darker(down?.2f:0.f),r.getBottomRight(),false));g.fillRoundedRectangle(r,9);g.setColour(Colour(0xffec79e8).withAlpha(.7f));g.drawRoundedRectangle(r,9,1);
            const auto lines=StringArray::fromLines(getButtonText());g.setColour(Colours::white);g.setFont(ui::font(getWidth()<220?17.f:20.f,Font::bold));g.drawText(lines[0],getLocalBounds().withHeight(29).translated(0,9),Justification::centred,true);g.setFont(ui::font(15));g.drawText(lines.size()>1?lines[1]:String{},getLocalBounds().withHeight(23).translated(0,38),Justification::centred);return;}
        else if(style==star||style==link){g.setGradientFill(ColourGradient(Colour(0xff302237).brighter(hover?.12f:0.f),r.getTopLeft(),ui::panel(),r.getBottomRight(),false));g.fillRoundedRectangle(r,8);g.setColour(blue().withAlpha(.4f));g.drawRoundedRectangle(r,8,1);}
        g.setColour(style==partner?Colours::white:blue());Path icon;const float x=style==partner?r.getRight()-17:r.getX()+14,y=r.getCentreY();
        if(style==star){icon.addStar({x+2,y},5,3.6f,8);if(getToggleState())g.fillPath(icon);else g.strokePath(icon,PathStrokeType(1.2f));}
        else if(style==back){icon.startNewSubPath(x+4,y-5);icon.lineTo(x-1,y);icon.lineTo(x+4,y+5);icon.startNewSubPath(x-1,y);icon.lineTo(x+10,y);g.strokePath(icon,PathStrokeType(1.3f));}
        else{icon.startNewSubPath(x-5,y-2);icon.lineTo(x-5,y+5);icon.lineTo(x+2,y+5);icon.startNewSubPath(x-1,y-5);icon.lineTo(x+5,y-5);icon.lineTo(x+5,y+1);icon.startNewSubPath(x-1,y+1);icon.lineTo(x+5,y-5);g.strokePath(icon,PathStrokeType(1.2f));}
        g.setColour(style==star||style==link?ui::ink():blue());g.setFont(ui::font(14));g.drawText(style==partner?getButtonText():l10n::translate(getButtonText()),getLocalBounds().withTrimmedLeft(36).withTrimmedRight(12),style==star?Justification::centred:Justification::centredLeft,true);
        if(hasKeyboardFocus(true)&&style!=star){g.setColour(blue());g.drawRoundedRectangle(r,5,1);}
    }
private:Style style;
};
class StarButton final:public juce::Button
{
public:
    StarButton():Button("Add to favourites"){tonegrid::l10n::tooltip(*this,"Save this lesson to your favourites");}
    void paintButton(juce::Graphics& g,bool hover,bool)override
    {
        using namespace juce;const auto r=getLocalBounds().toFloat().reduced(1);g.setColour(Colours::black.withAlpha(hover?.85f:.65f));g.fillEllipse(r);
        Path p;p.addStar(r.getCentre(),5,5,11);g.setColour(getToggleState()?blue():ui::ink());if(getToggleState())g.fillPath(p);else g.strokePath(p,PathStrokeType(1.3f));
        if(hasKeyboardFocus(true)){g.setColour(blue());g.drawEllipse(r,1);}
    }
};
class Description final:public juce::Component
{
    class Hit final:public juce::Button
    {
    public:
        explicit Hit(const juce::String& url):Button(url){setTooltip(url);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
        void paintButton(juce::Graphics& g,bool hover,bool)override{if(hover||hasKeyboardFocus(true)){g.setColour(blue());g.drawHorizontalLine(getHeight()-2,0.f,float(getWidth()));}}
    };
public:
    void setText(const juce::String& text,int width)
    {
        using namespace juce;if(text==plain&&width==layoutWidth)return;plain=text;layoutWidth=width;hits.clear();links.clear();
        AttributedString attributed;attributed.setText(plain);attributed.setFont(ui::font(16));attributed.setColour(ui::secondaryInk());attributed.setLineSpacing(5);
        for(int position=0;(position=plain.indexOf(position,"https://"))>=0;)
        {
            int end=position;while(end<plain.length()&&!CharacterFunctions::isWhitespace(plain[end])&&plain[end]!='<'&&plain[end]!='>')++end;
            while(end>position&&String(").,;\"'").containsChar(plain[end-1]))--end;const auto url=plain.substring(position,end);
            if(safeLink(url)){links.push_back({Range<int>(position,end),url});attributed.setColour(Range<int>(position,end),blue());}position=jmax(end,position+8);
        }
        layout.createLayout(attributed,float(jmax(100,width)));setSize(width,jmax(24,int(std::ceil(layout.getHeight()))+8));
        for(int lineIndex=0;lineIndex<layout.getNumLines();++lineIndex)
        {
            const auto& line=layout.getLine(lineIndex);for(auto* run:line.runs)for(const auto& link:links)if(run->stringRange.intersects(link.first))
            {
                const auto x=run->getRunBoundsX();const auto y=line.getLineBoundsY();auto hit=std::make_unique<Hit>(link.second);hit->onClick=[this,url=link.second]{if(open)open(url);};
                hit->setBounds(juce::Rectangle<float>(x.getStart()+line.lineOrigin.x,y.getStart(),x.getLength(),y.getLength()).expanded(1).toNearestInt());addAndMakeVisible(*hit);hits.push_back(std::move(hit));
            }
        }
        repaint();
    }
    void paint(juce::Graphics& g)override{layout.draw(g,getLocalBounds().toFloat());}
    const juce::String& text()const{return plain;}
    int linkCount()const{return int(links.size());}
    std::function<void(const juce::String&)> open;
private:juce::String plain;int layoutWidth=0;juce::TextLayout layout;std::vector<std::pair<juce::Range<int>,juce::String>> links;std::vector<std::unique_ptr<Hit>> hits;
};
class View final:public juce::Component,private juce::Timer
{
    class Card final:public juce::Button
    {
    public:
        Card(View& parent,Lesson value):Button(value.title+" — "+value.artist),owner(parent),lesson(std::move(value))
        {
            addAndMakeVisible(star);setComponentID("learn-lesson-"+lesson.id);star.setComponentID("learn-favourite-"+lesson.id);setTooltip(lesson.title+"\n"+lesson.artist+"\n"+lesson.instructor);
            star.onClick=[this]{const auto id=lesson.id;juce::MessageManager::callAsync([safe=juce::Component::SafePointer<View>(&owner),id]{if(safe)safe->toggleFavourite(id);});};
            onClick=[this]{const auto id=lesson.id;juce::MessageManager::callAsync([safe=juce::Component::SafePointer<View>(&owner),id]{if(safe)safe->openLesson(id);});};syncStar();
        }
        void syncStar(){const bool saved=owner.favourites.contains(lesson.id);star.setToggleState(saved,juce::dontSendNotification);star.setName(saved?"Remove from favourites":"Add to favourites");}
        void resized()override{star.setBounds(getWidth()-50,12,38,38);}
        void paintButton(juce::Graphics& g,bool hover,bool)override
        {
            using namespace juce;const auto r=getLocalBounds().toFloat().reduced(.5f);g.setGradientFill(ColourGradient(hover?Colour(0xff1c2c36):Colour(0xff16242e),r.getTopLeft(),ui::panel(),r.getBottomRight(),false));g.fillRoundedRectangle(r,12);
            const auto photo=juce::Rectangle<int>(0,0,getWidth(),cardPhotoHeight(getWidth()));const auto image=owner.thumbnail(lesson.thumbnail);
            {Graphics::ScopedSaveState save(g);Path p;p.addRoundedRectangle(r,12);g.reduceClipRegion(p);if(image.isValid()){g.setImageResamplingQuality(Graphics::highResamplingQuality);const int sourceHeight=jmin(image.getHeight(),roundToInt(image.getWidth()*double(photo.getHeight())/photo.getWidth()));g.drawImage(image,0,0,photo.getWidth(),photo.getHeight(),0,0,image.getWidth(),sourceHeight);}else{g.setColour(Colour(0xff253844));g.fillRect(photo);}}
            const auto centre=photo.toFloat().getCentre();g.setColour(Colours::black.withAlpha(hover?.75f:.6f));g.fillEllipse(centre.x-28,centre.y-28,56,56);Path triangle;triangle.addTriangle(centre.x-6,centre.y-11,centre.x-6,centre.y+11,centre.x+12,centre.y);g.setColour(Colours::white);g.fillPath(triangle);
            int y=photo.getBottom()+12;g.setColour(ui::ink());g.setFont(ui::font(22,Font::bold));g.drawText(lesson.title,18,y,getWidth()-36,28,Justification::centredLeft,true);y+=31;
            g.setColour(ui::secondaryInk());g.setFont(ui::font(17));g.drawText(lesson.artist,18,y,getWidth()-36,24,Justification::centredLeft,true);y+=33;
            g.setFont(ui::font(15));g.drawFittedText(lesson.summary,18,y,getWidth()-36,53,Justification::topLeft,3,1.f);g.setFont(ui::font(15));g.drawText(lesson.instructor,18,getHeight()-31,getWidth()-152,22,Justification::centredLeft,true);g.drawText(dateLabel(lesson.date),getWidth()-136,getHeight()-31,118,22,Justification::centredRight);
            g.setColour(hasKeyboardFocus(true)?blue():ui::outline().withAlpha(hover?.95f:.7f));g.drawRoundedRectangle(r,12,1);
        }
        Lesson lesson;
    private:View& owner;StarButton star;
    };
    class Body final:public juce::Component
    {
    public:
        explicit Body(View& parent):owner(parent){}
        void resized()override{owner.layout();}
        void paint(juce::Graphics& g)override{owner.paintBody(g);}
    private:View& owner;
    };
public:
    View():body(*this)
    {
        using namespace juce;setLookAndFeel(&look);setComponentID("learn-view");addAndMakeVisible(viewport);viewport.setViewedComponent(&body,false);viewport.setScrollBarsShown(true,true);viewport.setScrollBarThickness(10);
        for(Component* c:std::initializer_list<Component*>{&partnerButton,&search,&dates,&styles,&artists,&onlyFavourites,&empty,&back,&favourite,&parts,&descriptionViewport,&resources,&external,&replay})body.addChildComponent(*c);
        body.addChildComponent(player);descriptionViewport.setViewedComponent(&description,false);descriptionViewport.setScrollBarsShown(true,false);descriptionViewport.setScrollBarThickness(8);
        search.setComponentID("learn-search");tonegrid::l10n::placeholder(search,"Search songs or artists...",ui::secondaryInk());search.setFont(ui::font(15));search.setIndents(44,13);search.onTextChange=[this]{rebuildCards();};search.setTitle("Search lessons");
        dates.setComponentID("learn-date");tonegrid::l10n::item(dates,"Date: All dates",1);tonegrid::l10n::item(dates,"Last 30 days",2);tonegrid::l10n::item(dates,"Last 90 days",3);tonegrid::l10n::item(dates,"Last 12 months",4);dates.setSelectedId(1,dontSendNotification);
        styles.setComponentID("learn-style");artists.setComponentID("learn-artist");dates.onChange=styles.onChange=artists.onChange=[this]{rebuildCards();};
        onlyFavourites.setComponentID("learn-favourites-only");tonegrid::l10n::button(onlyFavourites,"Favourites");onlyFavourites.onClick=[this]{rebuildCards();};
        partnerButton.setComponentID("learn-partner");partnerButton.onClick=[this]{launch(partner.url);};
        back.setComponentID("learn-back");back.onClick=[this]{closeLesson();};favourite.setComponentID("learn-add-favourite");favourite.onClick=[this]{toggleFavourite(current.id);};
        parts.setComponentID("learn-parts");parts.onChange=[this]{playPart(parts.getSelectedId()-1);};player.setComponentID("learn-youtube-player");
        external.setComponentID("learn-open-youtube");external.onClick=[this]{if(part>=0&&part<int(current.videos.size()))launch(current.videos[size_t(part)].url);};
        replay.setComponentID("learn-replay");replay.onClick=[this]{playPart(part);};resources.setComponentID("learn-resources");resources.onClick=[this]{launch(current.lessonUrl);};
        tonegrid::l10n::text(empty,"No lessons match. Try another filter or search.",dontSendNotification);empty.setJustificationType(Justification::centred);empty.setInterceptsMouseClicks(false,false);empty.setFont(ui::font(16));
        description.open=player.openExternal=[this](const String& url){launch(url);};setSize(1200,680);startTimerHz(15);
    }
    ~View()override{stopTimer();player.stop();cards.clear();descriptionViewport.setViewedComponent(nullptr,false);viewport.setViewedComponent(nullptr,false);setLookAndFeel(nullptr);}
    void configure(const juce::File& catalogue,const juce::File& savedFavourites,const juce::File& profile,bool testing=false)
    {catalogueLayer.shipped=catalogue;testingPlayer=testing;favourites.load(savedFavourites);player.configure(profile,testing);reload();}
    void setCatalogueOverride(const juce::File& file){catalogueLayer.user=file;reload();}
    void reload()
    {
        using namespace juce;catalogueFile=catalogueLayer.effective();catalogueRevision=catalogueLayer.revision();fileTime=catalogueFile.getLastModificationTime();fileSize=catalogueFile.getSize();std::vector<Lesson> next;Partner nextPartner;String source;
        if(catalogueFile.existsAsFile())
        {if(fileSize>8*1024*1024){message="Lesson JSON exceeds 8 MB. Existing catalogue kept.";body.repaint();return;}source=catalogueFile.loadFileAsString();}
        else source=String::fromUTF8(ToneGridApplicationAssets::lessons_json,ToneGridApplicationAssets::lessons_jsonSize);
        const auto result=decode(source,next,nextPartner);if(result.failed())
        {message=result.getErrorMessage()+" Existing catalogue kept.";if(lessons.empty())decode(String::fromUTF8(ToneGridApplicationAssets::lessons_json,ToneGridApplicationAssets::lessons_jsonSize),lessons,partner);}
        else{message.clear();closeLesson();cards.clear();images.clear();imageOrder.clear();lessons=std::move(next);partner=std::move(nextPartner);}
        const auto style=styles.getText(),artist=artists.getText();styles.clear(dontSendNotification);artists.clear(dontSendNotification);tonegrid::l10n::item(styles,"Style: All styles",1);tonegrid::l10n::item(artists,"Artist: All artists",1);
        StringArray genreNames,artistNames;for(const auto& item:lessons){if(item.style.isNotEmpty())genreNames.addIfNotAlreadyThere(item.style);if(item.artist.isNotEmpty())artistNames.addIfNotAlreadyThere(item.artist);}genreNames.sort(true);artistNames.sort(true);styles.addItemList(genreNames,2);artists.addItemList(artistNames,2);styles.setSelectedId(genreNames.contains(style)?genreNames.indexOf(style)+2:1,dontSendNotification);artists.setSelectedId(artistNames.contains(artist)?artistNames.indexOf(artist)+2:1,dontSendNotification);
        partnerButton.setButtonText(partner.button);rebuildCards();
    }
    void openLesson(const juce::String& id)
    {
        auto found=std::find_if(lessons.begin(),lessons.end(),[&](const auto& value){return value.id==id;});if(found==lessons.end())return;
        player.stop();current=*found;detail=true;part=0;savedGridPosition=viewport.getViewPositionY();parts.clear(juce::dontSendNotification);for(size_t i=0;i<current.videos.size();++i)tonegrid::l10n::item(parts,"Video "+juce::String(int(i)+1)+" of "+juce::String(int(current.videos.size())),int(i)+1);parts.setSelectedId(1,juce::dontSendNotification);syncFavourite();resized();viewport.setViewPosition(0,0);playPart(0);
    }
    void closeLesson(){player.stop();if(!detail)return;detail=false;current={};resized();viewport.setViewPosition(0,savedGridPosition);}
    void stopPlayer(){player.stop();}
    void toggleFavourite(const juce::String& id)
    {const auto result=favourites.toggle(id);if(result.failed())message=result.getErrorMessage();syncFavourite();for(auto& card:cards)card->syncStar();if(onlyFavourites.getToggleState())rebuildCards();body.repaint();}
    void resized()override
    {
        viewport.setBounds(getLocalBounds());const int width=juce::jmax(760,viewport.getMaximumVisibleWidth());const int cardWidth=(width-72)/3;const int rows=(int(cards.size())+2)/3;const int height=detail?juce::jmax(getHeight()-4,174+detailVideoWidth(width)*9/16+230):juce::jmax(getHeight()-4,gridTop+(width<1100?58:0)+rows*(cardPhotoHeight(cardWidth)+cardTextHeight+cardGap)+24);
        body.setSize(width,height);layout();
    }
    void visibilityChanged()override{if(!testingPlayer&&!isShowing())player.stop();}
    size_t lessonCount()const{return lessons.size();}
    size_t visibleCount()const{return cards.size();}
    bool isDetail()const{return detail;}
    bool playerRequested()const{return player.isRequested();}
    juce::Rectangle<int> playerBounds()const{return player.getBounds();}
    const juce::String& selectedId()const{return current.id;}
    const juce::String& descriptionText()const{return description.text();}
    bool isFavourite(const juce::String& id)const{return favourites.contains(id);}
    int descriptionLinks()const{return description.linkCount();}
    void selectPartForTest(int index){parts.setSelectedId(index+1,juce::sendNotificationSync);}
    juce::Component& content(){return body;}
    void reloadIfChanged(){if(catalogueLayer.revision()!=catalogueRevision)reload();}
    void inspectPlayer(std::function<void(juce::var)> callback){player.inspect(std::move(callback));}
    juce::String playerNetworkError()const{return player.lastNetworkError();}
    void sendFullscreenEventForTest(bool active){player.sendFullscreenEventForTest(active);}
    void escapeFullscreenForTest(){player.escapeFullscreenForTest();}
    bool playerFullScreen()const{return player.isFullScreen();}
    std::function<bool(const juce::String&)> openExternal=[](const juce::String& address){return juce::URL(address).launchInDefaultBrowser();};
private:
    int detailVideoWidth(int width)const{return juce::jmin(1000,width-80,juce::jmax(220,viewport.getHeight()-240)*16/9);}
    bool launch(const juce::String& url){return safeLink(url)&&openExternal&&openExternal(url);}
    juce::Image thumbnail(const juce::String& name)
    {
        if(name.isEmpty())return {};auto found=images.find(name);if(found!=images.end())return found->second;
        auto file=catalogueFile.getParentDirectory().getChildFile(name);if(!file.existsAsFile())file=catalogueLayer.shipped.getParentDirectory().getChildFile(name);juce::Image image;if(file.existsAsFile()&&file.getSize()<2*1024*1024)image=juce::ImageFileFormat::loadFrom(file);
        if(images.size()>=18){images.erase(imageOrder[0]);imageOrder.remove(0);}images.emplace(name,image);imageOrder.add(name);return image;
    }
    void rebuildCards()
    {
        cards.clear();const int days[]={0,30,90,365};const int range=days[juce::jlimit(0,3,dates.getSelectedId()-1)];
        for(const auto& item:lessons)if(matches(item,search.getText().trim(),styles.getSelectedId()>1?styles.getText():juce::String{},artists.getSelectedId()>1?artists.getText():juce::String{},range)&&(!onlyFavourites.getToggleState()||favourites.contains(item.id)))
        {auto card=std::make_unique<Card>(*this,item);body.addChildComponent(*card);cards.push_back(std::move(card));}resized();body.repaint();
    }
    void syncFavourite(){const bool saved=favourites.contains(current.id);tonegrid::l10n::button(favourite,saved?"Remove from favourites":"Add to favourites");favourite.setToggleState(saved,juce::dontSendNotification);}
    void playPart(int index)
    {
        if(!detail||index<0||index>=int(current.videos.size()))return;part=index;const auto& video=current.videos[size_t(index)];media::Track track;track.id=media::videoId(video.url);track.url=video.url;track.title=video.title;player.start(track);
        description.setText(video.description.isEmpty()?"This video has no YouTube description. Visit Guitar Club for the full lesson and resources.":video.description,descriptionViewport.getMaximumVisibleWidth());descriptionViewport.setViewPosition(0,0);syncPlayer();body.repaint();
    }
    void layout()
    {
        if(layingOut)return;const juce::ScopedValueSetter<bool> guard(layingOut,true);using namespace juce;const int width=body.getWidth();const int cardWidth=(width-72)/3;
        for(auto* component:std::initializer_list<Component*>{&partnerButton,&search,&dates,&styles,&artists,&onlyFavourites})component->setVisible(!detail);
        for(auto* component:std::initializer_list<Component*>{&back,&favourite,&descriptionViewport,&resources,&external})component->setVisible(detail);parts.setVisible(detail&&current.videos.size()>1);
        const int start=gridTop+(width<1100?58:0);
        for(size_t i=0;i<cards.size();++i){cards[i]->setVisible(!detail);cards[i]->setBounds(16+(int(i)%3)*(cardWidth+cardGap),start+(int(i)/3)*(cardPhotoHeight(cardWidth)+cardTextHeight+cardGap),cardWidth,cardPhotoHeight(cardWidth)+cardTextHeight);}
        empty.setVisible(!detail&&cards.empty());empty.setBounds(16,start,width-32,100);partnerButton.setBounds(width-282,32,248,68);
        const int searchWidth=jlimit(240,380,width*28/100);search.setBounds(16,204,searchWidth,46);int x=28+searchWidth;
        const int filterWidth=width<1100?(width-56-searchWidth)/2:jlimit(160,240,(width-32-searchWidth-154-48)/3);
        dates.setBounds(x,204,filterWidth,46);x+=filterWidth+12;styles.setBounds(x,204,filterWidth,46);x+=filterWidth+12;
        if(width<1100){artists.setBounds(16,262,260,46);onlyFavourites.setBounds(292,262,154,46);}
        else{artists.setBounds(x,204,filterWidth,46);onlyFavourites.setBounds(x+filterWidth+12,204,154,46);}
        if(detail)
        {
            const int videoWidth=detailVideoWidth(width),videoHeight=videoWidth*9/16;
            const int contentWidth=videoWidth,left=(width-contentWidth)/2;
            back.setBounds(left,14,160,30);favourite.setBounds(left+contentWidth-244,contentWidth<820?10:62,244,42);parts.setBounds(left+contentWidth-244,116,244,34);
            player.setBounds((width-videoWidth)/2,174,videoWidth,videoHeight);aboutBounds={left,player.getBottom()+22,contentWidth,jmax(150,body.getHeight()-player.getBottom()-70)};
            external.setBounds(aboutBounds.getRight()-224,aboutBounds.getY()+16,200,38);replay.setBounds((width-120)/2,player.getY()+videoHeight/2-20,120,40);
            descriptionViewport.setBounds(aboutBounds.getX()+24,aboutBounds.getY()+67,contentWidth-48,aboutBounds.getHeight()-83);resources.setBounds(left,aboutBounds.getBottom()+8,contentWidth,30);
            if(part>=0&&part<int(current.videos.size()))description.setText(current.videos[size_t(part)].description.isEmpty()?"This video has no YouTube description. Visit Guitar Club for the full lesson and resources.":current.videos[size_t(part)].description,descriptionViewport.getMaximumVisibleWidth());
        }
        syncPlayer();body.repaint();
    }
    void syncPlayer()
    {
        if(player.isFullScreen())return;
        if(!detail){player.setVisible(false);replay.setVisible(false);return;}
        const bool visible=(testingPlayer||isShowing())&&viewport.getViewArea().contains(player.getBounds());
        // Visibility uses the logical layout, matching the scaled Mac board.
#if JUCE_MAC
        const bool nativeSize=player.getWidth()>=200&&player.getHeight()>=200;
#else
        const bool nativeSize=testingPlayer||(player.getScreenBounds().getWidth()>=200&&player.getScreenBounds().getHeight()>=200);
#endif
        player.setVisible(visible);player.showPlayer(visible&&nativeSize);replay.setVisible(!player.isRequested());
    }
    void paintBody(juce::Graphics& g)
    {
        using namespace juce;g.fillAll(Colour(0xff0c171e));const int width=body.getWidth();if(!detail)
        {
            const juce::Rectangle<float> banner(16,16,float(width-32),100);
            {Graphics::ScopedSaveState save(g);Path clip;clip.addRoundedRectangle(banner,12);g.reduceClipRegion(clip);
                g.setGradientFill(ColourGradient(Colour(0xff281537),banner.getTopLeft(),Colour(0xff14132c),banner.getBottomRight(),false));g.fillRect(banner);
                const float right=banner.getRight(),mid=banner.getX()+banner.getWidth()*.54f;Path wave;wave.startNewSubPath(mid,128);wave.cubicTo(mid+110,8,right-320,40,right-270,16);wave.lineTo(right,16);wave.lineTo(right,128);wave.closeSubPath();g.setGradientFill(ColourGradient(Colour(0xffa32aa8).withAlpha(.7f),right-280,16,Colour(0xff2b184f).withAlpha(.2f),mid,128,false));g.fillPath(wave);
                Path ribbon;ribbon.startNewSubPath(mid+60,128);ribbon.cubicTo(mid+190,-35,right-220,150,right-120,16);ribbon.lineTo(right,16);ribbon.lineTo(right,128);ribbon.closeSubPath();g.setGradientFill(ColourGradient(Colour(0xff7d238d).withAlpha(.6f),right,16,Colour(0xff452262).withAlpha(.15f),mid,128,false));g.fillPath(ribbon);
            }
            g.setColour(Colour(0xff85608e).withAlpha(.6f));g.drawRoundedRectangle(banner,12,1);
            const juce::Rectangle<float> logoCard(26,26,width<1100?180.f:242.f,80.f);
            g.setColour(Colours::white);g.fillRoundedRectangle(logoCard,6);
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.drawImage(partnerLogo,logoCard.reduced(8),RectanglePlacement::centred);
            const int textLeft=roundToInt(logoCard.getRight())+24,textWidth=width-textLeft-302;
            g.setColour(Colour(0xff756480));g.drawVerticalLine(textLeft-12,32.f,100.f);
            g.setColour(ui::ink());g.setFont(ui::font(width<1100?23.f:30.f,Font::bold));g.drawFittedText(partner.headline,textLeft,27,textWidth,50,Justification::centredLeft,2,1.f);
            g.setColour(ui::secondaryInk());g.setFont(ui::font(width<1100?14.f:17.f));g.drawFittedText(partner.message,textLeft,77,textWidth,35,Justification::topLeft,2,1.f);
            g.setColour(ui::ink());g.setFont(ui::font(34,Font::bold));g.drawText(tonegrid::l10n::translate("Free Lessons"),16,128,400,42,Justification::centredLeft);
            g.setColour(ui::secondaryInk());g.setFont(ui::font(17));g.drawText(tonegrid::l10n::translate("Learn iconic songs with step-by-step guitar lessons from Guitar Club."),16,173,width-32,25,Justification::centredLeft,true);
        }
        else
        {
            const int left=aboutBounds.getX(),textWidth=aboutBounds.getWidth()-(aboutBounds.getWidth()<820?0:268);
            Path highlight;highlight.startNewSubPath(float(left-6),61);highlight.lineTo(float(left+textWidth+6),55);highlight.lineTo(float(left+textWidth-4),109);highlight.lineTo(float(left-16),115);highlight.closeSubPath();g.setGradientFill(ColourGradient(Colour(0xff702a62).withAlpha(.8f),float(left),65,Colour(0xff39203e).withAlpha(.5f),float(left+textWidth),110,false));g.fillPath(highlight);
            const int metadataWidth=current.videos.size()>1?aboutBounds.getWidth()-268:textWidth;
            g.setColour(ui::ink());g.setFont(ui::font(36,Font::bold|Font::italic));g.drawText(current.title,left,62,textWidth,47,Justification::centredLeft,true);g.setFont(ui::font(19));g.drawText(current.artist,left,112,metadataWidth,25,Justification::centredLeft,true);g.setColour(ui::secondaryInk());g.setFont(ui::font(14));g.drawText((current.instructor.isEmpty()?String{}:tonegrid::l10n::translate("Lesson by ")+current.instructor+tonegrid::l10n::translate("   |   "))+dateLabel(current.date),left,143,metadataWidth,22,Justification::centredLeft,true);
            if(aboutBounds.getWidth()>=820){g.setFont(ui::font(15,Font::bold));g.drawText(tonegrid::l10n::translate("guitar club"),aboutBounds.getRight()-150,14,150,28,Justification::centredRight);}
            const auto border=player.getBounds().toFloat().expanded(1);g.setColour(blue().withAlpha(.35f));g.drawRoundedRectangle(border,9,1);
            g.setColour(Colour(0xff111e26));g.fillRoundedRectangle(aboutBounds.toFloat(),12);g.setColour(ui::outline().withAlpha(.5f));g.drawRoundedRectangle(aboutBounds.toFloat().reduced(.5f),12,1);
            g.setColour(ui::ink());g.setFont(ui::font(24,Font::bold));g.drawText(tonegrid::l10n::translate("About this lesson"),aboutBounds.getX()+24,aboutBounds.getY()+18,aboutBounds.getWidth()-272,33,Justification::centredLeft,true);
        }
        if(message.isNotEmpty()){g.setColour(Colour(0xffffca80));g.setFont(ui::font(12));g.drawText(message,16,body.getHeight()-26,width-32,22,Justification::centredLeft,true);}
    }
    void timerCallback()override{if(detail)syncPlayer();if(++fileTicks>=15){fileTicks=0;if(isShowing())reloadIfChanged();}}
    const juce::Image partnerLogo=juce::ImageCache::getFromMemory(ToneGridApplicationAssets::GuitarClub_png,ToneGridApplicationAssets::GuitarClub_pngSize);
    Look look;juce::Viewport viewport;Body body;ActionButton partnerButton{"START YOUR FREE TRIAL\non Guitar Club",ActionButton::partner},back{"Back to lessons",ActionButton::back},favourite{"Add to favourites",ActionButton::star},resources{"Full lesson and resources on Guitar Club",ActionButton::resource},external{"Open on YouTube",ActionButton::link};tonegrid::l10n::TextButton replay{"Play video"};
    juce::TextEditor search;juce::ComboBox dates,styles,artists,parts;juce::ToggleButton onlyFavourites;juce::Label empty;juce::Viewport descriptionViewport;Description description;media::Player player;
    std::vector<Lesson> lessons;Partner partner;Lesson current;Favourites favourites;std::vector<std::unique_ptr<Card>> cards;
    std::map<juce::String,juce::Image> images;juce::StringArray imageOrder;content::FileLayer catalogueLayer;juce::String catalogueRevision;juce::File catalogueFile;juce::Time fileTime;juce::int64 fileSize=0;juce::String message;juce::Rectangle<int> aboutBounds;int part=0,fileTicks=0,savedGridPosition=0;bool detail=false,testingPlayer=false,layingOut=false;
};
}
