#include "BoardTests.h"
#include "TestIsolationTests.h"
#include "CabinetIRTests.h"
#include "IRExpansionRender.h"
#include "Modules/Play/UI/PedalCard.h"
#include "RigStorageTests.h"
#include "MidiControllerTests.h"
#include "AmpSourceTests.h"
#include "Vst3Tests.h"
#include "NamAmpTests.h"
#include "InputCompensation136Tests.h"
#include "Milestone3DTests.h"
#include "AmpNormalizationTests.h"
#include "StereoSpreadTests.h"
#include "OutputRealtimeTests.h"
#include "OutputDspTests.h"
#include "ReverbTests.h"
#include "SpringReverbTests.h"
#include "SpringBenchmark.h"
#include "ReverbBenchmark.h"
#include "AmpUxTests.h"
#include "PedalFingerprint.h"
#include "CompactUxTests.h"
#include "UiCleanupTests.h"
#include "PedalAssetTests.h"
#include "PedalSkin137Tests.h"
#include "UiMotion138Tests.h"
#include "Refinements138Tests.h"
#include <iostream>
#include "NamBrowserLifetimeTests.h"
#include "Effects119Tests.h"
#include "ToneBankTests.h"
#include "Effects119Benchmark.h"
#include "Tone3000Tests.h"
#include "Release113Tests.h"
#include "Recording114Tests.h"
#include "TunerDisplayTests.h"
#include "Popup115Tests.h"
#include "Popup126Tests.h"
#include "Release130Tests.h"
#include "Learn132Tests.h"
#include "Diagnostic126Tests.h"
#include "Distribution116Tests.h"
#include "Release120Tests.h"
#include "ToneBank136Render.h"
#include "CaptureSelector120Tests.h"
#include "AmpEq121Tests.h"
#include "Release122Tests.h"
#include "NamEq123Tests.h"
#include "Library124Tests.h"
#include "DeviceRateTests.h"
#include "PortabilityTests.h"
#include "Calibration117Tests.h"
#include "Release134Tests.h"
#include "Release135Tests.h"
#include "Regression139Tests.h"
#include "LibraryEscapeTests.h"
#include "Regression140Tests.h"
#include "PopupOnboardingTests.h"
#include "Optimized140Tests.h"
#include "Record141Tests.h"
#include "Audit142Tests.h"
#include "LibraryFeedback142Tests.h"
#include "FactoryRefinements142Tests.h"
#include "Release143Tests.h"
#include "Language144Tests.h"
#include "BassModeTests.h"

namespace
{
using namespace juce;

// Deliberately independent compatibility oracle, recorded before refactoring.
constexpr std::array<int,13> historicalOrder{7,8,9,0,4,2,1,10,11,3,5,12,6};
constexpr std::array<bool,13> historicalNam{false,true,true,true,true,true,false,true,true,false,true,true,false};
const std::array<String,13> historicalNames{"COMP","TS9","CENTURA","DS-1","TIMMY","BIG MUFF","BOOST","CLASSIC FUZZ","TREBLE BOOST","AUTO WAH","BLUES OD","FILTER DIST","GRAPHIC EQ"};

bool sameSetting(const board::Setting& a, const board::Setting& b)
{
    return a.preset==b.preset && a.enabled==b.enabled && a.input==b.input && a.mix==b.mix
        && a.output==b.output && a.ratio==b.ratio && a.tone==b.tone
        && a.wahSensitivity==b.wahSensitivity && a.wahRange==b.wahRange
        && a.wahDecay==b.wahDecay && a.eqBands==b.eqBands;
}

std::unique_ptr<XmlElement> snapshot(board::Processor& processor)
{
    auto root=std::make_unique<XmlElement>("CompatibilityBaseline");
    root->setAttribute("fixtureVersion",1);
    PluginDescription plugin; plugin.name="Synthetic TONEX"; plugin.pluginFormatName="VST3";
    plugin.fileOrIdentifier="synthetic.vst3"; plugin.uniqueId=123; plugin.version="test";
    board::State state; state.name="Synthetic compatibility rig";
    state.amp.append("synthetic-only",14); state.input={4.5f,-57,true};
    for(int p=0;p<13;++p)
    {
        state.pedals[(size_t)p]=board::defaults(p,p%board::presetCount(p));
        state.pedals[(size_t)p].enabled=p%2==0;
        state.pedals[(size_t)p].tone=board::isNam(p)?(p%2?2.5f:-3.5f):0;
        auto* pedal=root->createNewChildElement("Definition");
        pedal->setAttribute("id",p); pedal->setAttribute("name",board::pedalName(p));
        pedal->setAttribute("nam",board::isNam(p));
        for(int n=0;n<board::presetCount(p);++n)
        {
            const auto s=board::defaults(p,n); auto* preset=pedal->createNewChildElement("Preset");
            preset->setAttribute("index",n); preset->setAttribute("label",processor.label(p,n));
            preset->setAttribute("identity",processor.identity(p,n)); preset->setAttribute("details",processor.details(p,n));
            preset->setAttribute("input",s.input); preset->setAttribute("mix",s.mix);
            preset->setAttribute("output",s.output); preset->setAttribute("ratio",s.ratio); preset->setAttribute("tone",s.tone);
            preset->setAttribute("sens",s.wahSensitivity); preset->setAttribute("range",s.wahRange); preset->setAttribute("decay",s.wahDecay);
            for(int b=0;b<7;++b)preset->setAttribute("eq"+String(b),s.eqBands[(size_t)b]);
        }
    }
    state.pedals[9].wahSensitivity=83; state.pedals[9].wahRange=37; state.pedals[9].wahDecay=333;
    state.pedals[12].eqBands={-5,3,7,0,-4,2,9};
    auto* rigs=root->createNewChildElement("Rigs");
    for(int version=3;version<=6;++version)
    {
        auto xml=board::encode(state,plugin,processor); xml.setAttribute("version",version);
        if(version<6)for(int p=7;p<13;++p)xml.removeChildElement(xml.getChildByAttribute("id",String(p)),true);
        if(version==3)xml.removeChildElement(xml.getChildByName("Input"),true);
        for(auto* pedal:xml.getChildIterator())if(pedal->hasTagName("Pedal"))
        {
            if(version==3)pedal->removeAttribute("ratio");
            if(version<5 && board::isNam(pedal->getIntAttribute("id")))
            { pedal->removeAttribute("tone"); pedal->setAttribute("mix",.3); pedal->setAttribute("input",18); }
        }
        rigs->addChildElement(new XmlElement(xml));
    }
    board::Bindings bindings{};
    for(int p=0;p<13;++p)board::assign(bindings,128+p,{2,3,60+p});
    board::assign(bindings,127,{1,16,127}); root->addChildElement(new XmlElement(board::encodeBindings(bindings)));
    return root;
}

// Finite deterministic inputs, all presets, toggles, serial stacking and variable
// callback sizes. Same toolchain comparison, not a cross-platform sonic promise.
String audioFingerprint(board::Processor& processor)
{
    // Hold historical controls fixed even when the private build defaults to H2O.
    for(int p=0;p<13;++p)processor.set(p,board::defaults(p,0));
    MemoryOutputStream rendered;
    AudioBuffer<float> buffer(2,256); MidiBuffer midi; int frame=0;
    auto render=[&](int blocks)
    {
        constexpr std::array<int,5> sizes{96,128,1,256,37};
        for(int b=0;b<blocks;++b)
        {
            const int length=sizes[(size_t)b%sizes.size()]; buffer.setSize(2,length,false,false,true);
            for(int j=0;j<length;++j,++frame)
            {
                const float x=.035f*(float)std::sin(frame*.031)+.013f*(float)std::sin(frame*.073);
                buffer.setSample(0,j,x); buffer.setSample(1,j,-x);
            }
            processor.processBlock(buffer,midi);
            for(int c=0;c<2;++c)rendered.write(buffer.getReadPointer(c),(size_t)length*sizeof(float));
        }
    };
    processor.prepareToPlay(48000,256); render(20);
    for(int p=0;p<13;++p)
    {
        for(int n=0;n<board::presetCount(p);++n)
        {
            auto s=board::defaults(p,n); s.enabled=true;
            if(board::isNam(p)){s.input=2;s.tone=n%2?3.f:-2.f;s.output=-1;}
            processor.set(p,s); render(30);
        }
        auto s=processor.get(p); s.enabled=false; processor.set(p,s); render(20);
    }
    for(int p=0;p<13;++p){auto s=board::defaults(p,3%board::presetCount(p));s.enabled=true;processor.set(p,s);}
    render(50);
    return SHA256(rendered.getData(),rendered.getDataSize()).toHexString();
}

bool compatibility(const File& models, const File& fixture, bool record, String& report)
{
    bool ok=true; int checks=0;
    auto check=[&](bool value,const String& label){++checks;ok&=value;report+=(value?"PASS ":"FAIL ")+label+"\n";};
    check(board::count==13 && board::presets==8,"Historical pedal count and algorithmic bank size");
    check(board::order==historicalOrder,"Historical fixed signal order");
    // Explicit numbers are intentional here: deriving expected values from the
    // definitions themselves would fail to detect a compatibility-breaking edit.
    using board::PedalId;
    constexpr std::array namedIds{PedalId::StudioComp,PedalId::TubeScreamer,PedalId::HorseOd,
        PedalId::OrangeDist,PedalId::TransparentOd,PedalId::VintageFuzz,PedalId::CleanBoost,
        PedalId::ClassicFuzz,PedalId::TrebleBoost,PedalId::AutoWah,PedalId::BluesOd,
        PedalId::FilterDist,PedalId::GraphicEq};
    using board::PedalFamily;
    constexpr std::array families{PedalFamily::Compressor,PedalFamily::Nam,PedalFamily::Nam,
        PedalFamily::Nam,PedalFamily::Nam,PedalFamily::Nam,PedalFamily::Boost,
        PedalFamily::Nam,PedalFamily::Nam,PedalFamily::AutoWah,PedalFamily::Nam,
        PedalFamily::Nam,PedalFamily::GraphicEq};
    check(board::validDefinitions(),"Canonical IDs unique and complete");
    for(int p=0;p<13;++p)
    {
        const auto& d=board::definition(p);
        check(board::id(namedIds[(size_t)p])==p && board::id(d.persistentId)==p
            && d.family==families[(size_t)p] && board::pedalMidiTarget(p)==128+p
            && board::pedalForMidiTarget(128+p)==p,"Named ID, family and MIDI compatibility "+String(p));
        int bytes=0;
        check(BinaryData::getNamedResource(d.skinResource,bytes)!=nullptr && bytes>0,"Canonical skin resolves for ID "+String(p));
    }
    for(int p=0;p<13;++p)
        check(board::isNam(p)==historicalNam[(size_t)p] && board::pedalName(p)==historicalNames[(size_t)p],"Historical identity/classification "+String(p));
    for(int v=0;v<13;++v)check(PedalCard::engineFor(v)==historicalOrder[(size_t)v],"Visual-to-persistent ID "+String(v));
    board::Processor processor; auto loaded=processor.load(models);
    check(loaded.wasOk(),"Compatibility model load: "+loaded.getErrorMessage()); if(loaded.failed())return false;
    auto actual=snapshot(processor);
    if(record)
    {
        check(!fixture.exists(),"Refuse to overwrite an existing baseline");
        if(ok)check(actual->writeTo(fixture),"Write pre-refactor synthetic fixture");
    }
    auto expected=XmlDocument::parse(fixture);
    check(expected!=nullptr,"Read frozen compatibility fixture"); if(!expected)return false;
    // 1.1.19 explicitly replaces all nine NAM banks. The frozen fixture is not
    // rewritten: its four algorithmic banks and MIDI remain our exact oracle.
    auto actualComparable=std::make_unique<XmlElement>(*actual),expectedComparable=std::make_unique<XmlElement>(*expected);
    // 1.1.42 adds descriptive preset IDs; the frozen sound/MIDI oracle stays exact.
    for(auto* document:{actualComparable.get(),expectedComparable.get()})
        for(auto* rig:document->getChildByName("Rigs")->getChildIterator())rig->removeAttribute("presetId");
    for(auto* document:{actualComparable.get(),expectedComparable.get()})
        for(int p=0;p<board::count;++p)if(board::isNam(p))
        {document->removeChildElement(document->getChildByAttribute("id",String(p)),true);for(auto* rig:document->getChildByName("Rigs")->getChildIterator())rig->removeChildElement(rig->getChildByAttribute("id",String(p)),true);}
    check(actualComparable->isEquivalentTo(expectedComparable.get(),false),"Exact unchanged four algorithmic banks, rig controls and MIDI snapshot");
    PluginDescription plugin;plugin.loadFromXml(*expected->getChildByName("Rigs")->getFirstChildElement()->getChildByName("PLUGIN"));
    for(auto* xml:expected->getChildByName("Rigs")->getChildIterator())
    {
        board::State decoded;decoded.name="previous";
        check(board::decode(*xml,plugin,processor,decoded).failed()&&decoded.name=="previous","Frozen old NAM identities are rejected without substitution or partial restore, version "+xml->getStringAttribute("version"));
    }
    board::Bindings bindings{};check(board::decodeBindings(*expected->getChildByName("PedalMidi"),bindings),"Frozen MIDI assignments decode");
    for(int p=0;p<13;++p)check(board::match(bindings,{2,3,60+p})==128+p,"Historical MIDI target "+String(128+p));
    report+="AUDIO_SHA256="+audioFingerprint(processor)+"\n";
    board::Processor unchanged;if(unchanged.load(models).failed())return false;
    report+="CURRENT_119_PEDALS_SHA256="+unchangedPedalFingerprint(unchanged,board::defaults)+"\n";
    // Render actual cards without RigHost, devices, MIDI ports or user presets.
    Image cards(Image::ARGB,224*13,288,true); Graphics graphics(cards);
    for(int v=0;v<13;++v){PedalCard card(v,processor);card.setSize(224,288);card.midi({},false,true);graphics.drawImageAt(card.createComponentSnapshot(card.getLocalBounds()),224*v,0);}
    MemoryOutputStream png;PNGImageFormat().writeImageToStream(cards,png);
    report+="UI_SHA256="+SHA256(png.getData(),png.getDataSize()).toHexString()+"\n";
    report+=String(checks)+" compatibility checks; "+(ok?"ALL PASSED":"FAILURES")+"\n";
    return ok;
}
}

int main(int argc,char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    if((argc==3||argc==4)&&String(argv[1])=="--bass-mode-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=bassModeTests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==4&&String(argv[1])=="--language144-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=language144Tests::run(File(String(argv[2])),File(String(argv[3])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--release143-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=release143Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--rig-storage-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=storageTests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--factory-refinements142-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=factoryRefinements142Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--library-feedback142-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=libraryFeedback142Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--audit142-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=audit142Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==5&&String(argv[1])=="--record141-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=record141Tests::run(File(String(argv[2])),File(String(argv[3])),File(String(argv[4])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--nam-controls-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=namTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==4&&String(argv[1])=="--optimized140-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=optimized140Tests::run(File(String(argv[2])),File(String(argv[3])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--popup-onboarding-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=popupOnboardingTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--regression140-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=regression140Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&juce::String(argv[1])=="--library-escape-test")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=libraryEscapeTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&juce::String(argv[1])=="--regression139-test")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=regression139Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==5&&juce::String(argv[1])=="--ir-expansion-render")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=irExpansionRender::run(juce::File(juce::String(argv[2])),juce::File(juce::String(argv[3])),juce::File(juce::String(argv[4])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&juce::String(argv[1])=="--cabinet-ir-test")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=cabinetIRTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&juce::String(argv[1])=="--test-isolation")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=testIsolationTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&juce::String(argv[1])=="--refinements138-test")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=refinements138Tests::run(juce::File(juce::String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&juce::String(argv[1])=="--motion138-test")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=uiMotion138Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==4&&juce::String(argv[1])=="--pedal137-test")
    {juce::ScopedJuceInitialiser_GUI init;juce::String report;const bool ok=pedalSkin137Tests::run(juce::File(juce::String(argv[2])),juce::File(juce::String(argv[3])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&juce::String(argv[1])=="--input-compensation136-test")
    {juce::ScopedJuceInitialiser_GUI juce;juce::String report;const bool ok=inputCompensation136Tests::run(juce::File(juce::String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--diagnostic126-test")
    {ScopedJuceInitialiser_GUI init;String report;const bool ok=diagnostic126Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--popup126-test")
    {ScopedJuceInitialiser_GUI init;String report;const bool ok=popup126Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==5&&String(argv[1])=="--normalization125-render")
    {ScopedJuceInitialiser_GUI init;String report;const bool ok=normalizationTests::render(File(String(argv[2])),File(String(argv[3])),File(String(argv[4])),report);std::cout<<report;return ok?0:1;}
    if(argc==5&&String(argv[1])=="--tonebank136-render")
    {ScopedJuceInitialiser_GUI init;String report;const bool ok=toneBank136Render::run(File(String(argv[2])),File(String(argv[3])),File(String(argv[4])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--normalization125-test")
    {ScopedJuceInitialiser_GUI init;String report;const bool normalization=normalizationTests::run(report);const bool realtime=outputRealtimeTests::run(report);std::cout<<report;return normalization&&realtime?0:1;}
    if(argc==2&&String(argv[1])=="--nam-eq123-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=namEq123Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--release122-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=release122Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--amp-eq121-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=ampEq121Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==4&&String(argv[1])=="--release120-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=release120Tests::run(File(String(argv[2])),File(String(argv[3])),report);std::cout<<report;return ok?0:1;}
    if(argc==4&&String(argv[1])=="--capture-selector120-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=captureSelector120Tests::run(File(String(argv[2])),File(String(argv[3])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&(String(argv[1])=="--calibration117-test"||String(argv[1])=="--calibration118-test"))
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=calibration117Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--portability-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=portabilityTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--distribution116-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=distribution116Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--quality-benchmark")
    {
        ScopedJuceInitialiser_GUI juce;nam::DspLoadOptions options;options.prewarm=false;auto model=nam::get_dsp(nlohmann::json::parse(File(String(argv[2])).loadFileAsString().toStdString()),options);
        std::array<float,96> input{},output{};for(size_t i=0;i<input.size();++i)input[i]=float(.1*std::sin(i*.07));float* in=input.data();float* out=output.data();
        double full=0,lite=0;for(bool slim:{false,true}){if(!tonegrid::namquality::apply(*model,slim))return 1;model->Reset(48000,96);model->prewarm();auto start=Time::getMillisecondCounterHiRes();for(int block=0;block<5000;++block)model->process(&in,&out,96);const auto elapsed=Time::getMillisecondCounterHiRes()-start;if(slim)lite=elapsed;else full=elapsed;}
        std::cout<<"Full-ms="<<full<<" Lite-ms="<<lite<<" Lite/Full="<<lite/full<<" callbacks=5000 frames=96\n";return 0;
    }
    if((argc==4||argc==5||argc==6)&&String(argv[1])=="--library124-preview")
    {
        ScopedJuceInitialiser_GUI juce;namamp::Library library{File(String(argv[2]))};namamp::LibraryService service(library,argc==6?File(String(argv[5])):File{});namamp::Catalog catalog;Result status=Result::ok();
        const auto start=Time::getMillisecondCounterHiRes();service.refresh();while(service.busy()){service.poll(catalog,status);Thread::sleep(1);}if(status.failed()||!catalog.browser)return 1;
        String selected;for(const auto& [path,c]:catalog.browser->captures)if(c.name.containsIgnoreCase("Princeton")){selected=path;break;}
        if(selected.isEmpty()&&!catalog.entries.empty())selected=catalog.entries.front().identity.file;
        tonegrid::NamLibraryBrowser browser;browser.update(catalog,library.folder(),selected);browser.reveal(selected);if(argc>=5){const String wanted(argv[4]);if(browser.find(wanted)){selected=wanted;browser.reveal(wanted);}else browser.navigate(wanted);}tonegrid::popup::Frame frame(browser,false,"Local NAM Library",440);
        const auto output=File(String(argv[3]));output.getParentDirectory().createDirectory();auto stream=output.createOutputStream();
        const bool written=stream&&PNGImageFormat().writeImageToStream(frame.createComponentSnapshot(frame.getLocalBounds()),*stream);
        std::cout<<"Indexed="<<browser.totalCaptures()<<" folders="<<catalog.browser->folders.size()<<" worker-ms="<<Time::getMillisecondCounterHiRes()-start<<" selected="<<selected<<" preview="<<written<<"\n";return written?0:1;
    }
    if(argc==3&&String(argv[1])=="--library-benchmark")
    {
        ScopedJuceInitialiser_GUI juce;namamp::Library library{File(String(argv[2]))};namamp::LibraryService service(library);namamp::Catalog catalog;Result status=Result::ok();
        auto scan=[&]{auto start=Time::getMillisecondCounterHiRes();service.refresh();while(service.busy()){service.poll(catalog,status);Thread::sleep(1);}return Time::getMillisecondCounterHiRes()-start;};
        auto cold=scan(),warm=scan();tonegrid::NamLibraryBrowser browser;browser.setSize(700,560);browser.update(catalog,library.folder(),{});
        auto start=Time::getMillisecondCounterHiRes();auto image=browser.createComponentSnapshot(browser.getLocalBounds());auto paint=Time::getMillisecondCounterHiRes()-start;
        std::cout<<"NAM entries="<<catalog.entries.size()<<" cold-background-ms="<<cold<<" warm-refresh-ms="<<warm<<" reused="<<service.reusedCount()<<" parsed="<<service.parsedCount()<<" cached-dialog-paint-ms="<<paint<<"\n";
        return status.wasOk()&&image.isValid()?0:1;
    }
    if(argc==2&&String(argv[1])=="--tone3000-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=tone3000Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==4&&String(argv[1])=="--effects119-benchmark")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=effects119Benchmark::run(File(String(argv[2])),File(String(argv[3])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--effects119-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=effects119Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--pitch-study")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=effects119Tests::pitchStudy(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--learn132-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=learn132Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--release130-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=release130Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==4&&String(argv[1])=="--scan-vst3-worker")return vstTests::fakeWorker(File(String(argv[2])),File(String(argv[3])));
    if(argc==3&&String(argv[1])=="--pedal-asset-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=pedalAssetTests::run(File(String(argv[2])),report,true);return ok?0:1;}
    if(argc==2&&String(argv[1])=="--device-rate-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=deviceRateTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--library124-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=library124Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--nam-browser-lifetime-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=namBrowserLifetimeTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--spring-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=springTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--spring-benchmark")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=springBenchmark::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--reverb-benchmark")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=reverbBenchmark::run(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--recording-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=recording114Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--tuner-display-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=tunerDisplayTests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--popup115-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=popup115Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==2&&String(argv[1])=="--release135-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=release135Tests::run(report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--release134-test")
    {ScopedJuceInitialiser_GUI juce;String report;const bool ok=release134Tests::run(File(String(argv[2])),report);std::cout<<report;return ok?0:1;}
    if(argc==3&&String(argv[1])=="--tonebank133-test")
    {ScopedJuceInitialiser_GUI juce;String report;board::Processor processor;const auto loaded=processor.load(File(String(argv[2])));if(loaded.failed()){std::cerr<<loaded.getErrorMessage();return 1;}const auto json=tonegrid::tonebank::Json::parse(File(TONEGRID_BANK_FILE).loadFileAsString().toStdString());const bool ok=toneBank133Tests::run(json,processor,report);std::cout<<report;return ok?0:1;}
    if(argc<3 || argc>4){std::cerr<<"Usage: ToneGridOfflineTests <models> <fixture> [--record-baseline]\n";return 2;}
    const bool record=argc==4 && String(argv[3])=="--record-baseline";
    if(argc==4 && !record)return 2;
    ScopedJuceInitialiser_GUI juce;
    String report;
    std::cerr << "Running bool effectsOk=effects119Tests::run\n";
    bool effectsOk=effects119Tests::run(report);
    std::cerr << "Running effectsOk=release113Tests::run\n";
    effectsOk=release113Tests::run(File(String(argv[1])),report)&&effectsOk;
    std::cerr << "Running effectsOk=tunerDisplayTests::run\n";
    effectsOk=tunerDisplayTests::run(report)&&effectsOk;
    std::cerr << "Running effectsOk=popup115Tests::run\n";
    effectsOk=popup115Tests::run(report)&&effectsOk;
    std::cerr << "Running bool ok=midiTests::run\n";
    bool ok=midiTests::run(report);
    ok=effectsOk&&ok;
    std::cerr << "Running ok=toneBankTests::run\n";
    ok=toneBankTests::run(File(String(argv[1])),report)&&ok;
    std::cerr << "Running ok=tone3000Tests::run\n";
    ok=tone3000Tests::run(report)&&ok;
    std::cerr << "Running ok=compactUxTests::run\n";
    ok=compactUxTests::run(report)&&ok;
    std::cerr << "Running ok=namBrowserLifetimeTests::run\n";
    ok=namBrowserLifetimeTests::run(report)&&ok;
    std::cerr << "Running ok=ampTests::run\n";
    ok=ampTests::run(report)&&ok;
    std::cerr << "Running ok=vstTests::run\n";
    ok=vstTests::run(report)&&ok;
    std::cerr << "Running ok=namTests::run\n";
    ok=namTests::run(report)&&ok;
    std::cerr << "Running ok=normalizationTests::run\n";
    ok=normalizationTests::run(report)&&ok;
    std::cerr << "Running ok=spreadTests::run\n";
    ok=spreadTests::run(report)&&ok;
    std::cerr << "Running ok=outputRealtimeTests::run\n";
    ok=outputRealtimeTests::run(report)&&ok;
    std::cerr << "Running ok=outputTests::run\n";
    ok=outputTests::run(report)&&ok;
    std::cerr << "Running ok=reverbTests::run\n";
    ok=reverbTests::run(report)&&ok;
    std::cerr << "Running ok=springTests::run\n";
    ok=springTests::run(report)&&ok;
    std::cerr << "Running ok=pedalAssetTests::run\n";
    ok=pedalAssetTests::run(File(String(argv[1])),report)&&ok;
    std::cerr << "Running ok=uiCleanupTests::run\n";
    ok=uiCleanupTests::run(File(String(argv[1])),report)&&ok;
    std::cerr << "Running ok=ampUxTests::run\n";
    ok=ampUxTests::run(File(String(argv[1])),report)&&ok;
    std::cerr << "Running ok=milestone3dTests::run\n";
    ok=milestone3dTests::run(File(String(argv[1])),report)&&ok;
    std::cerr << "Running ok=runBoardTests\n";
    ok=runBoardTests(File(String(argv[1])),report)&&ok;
    std::cerr << "Running ok=compatibility\n";
    ok=compatibility(File(String(argv[1])),File(String(argv[2])),record,report)&&ok;
    std::cerr << "Running ok=storageTests::run\n";
    ok=storageTests::run(File(String(argv[2])),report)&&ok;
    std::cout<<report;return ok?0:1;
}

