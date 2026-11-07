#include "Core/ProcessingGraph.h"
#include "DeviceRateProcessor.h"
#include "Content/Presets/RigCodec.h"
#include "Vst3Loader.h"
#include "UserPedalLibrary.h"
#include "Content/Amps/CaptureResolver.h"
namespace tonegrid::core {
GraphStages buildGuitarGraph(juce::AudioProcessorGraph& graph,std::unique_ptr<board::Processor> pedals,amp::AmpSource& source) {
 using namespace juce;GraphStages s;graph.setPlayConfigDetails(2,2,48000,256);using IO=AudioProcessorGraph::AudioGraphIOProcessor;
 const auto in=graph.addNode(std::make_unique<IO>(IO::audioInputNode)),out=graph.addNode(std::make_unique<IO>(IO::audioOutputNode));
 const auto stomp=graph.addNode(std::move(pedals));s.amp=source.addToGraph(graph);
 auto cabinet=std::make_unique<cabinet::Node>();s.cabinetProcessor=cabinet.get();const auto cab=graph.addNode(std::move(cabinet));
 auto eq=std::make_unique<output::AmpEqNode>();s.eq=eq.get();const auto eqNode=graph.addNode(std::move(eq));
 auto norm=std::make_unique<output::NormalizationNode>();s.normalize=norm.get();const auto normalized=graph.addNode(std::move(norm));
 auto spread=std::make_unique<output::SpreadNode>();s.spread=spread.get();const auto sp=graph.addNode(std::move(spread));
 auto mod=std::make_unique<output::ModNode>();s.mod=mod.get();const auto mo=graph.addNode(std::move(mod));
 auto delay=std::make_unique<output::DelayNode>();s.delay=delay.get();const auto de=graph.addNode(std::move(delay));
 auto reverb=std::make_unique<output::ReverbNode>();s.reverb=reverb.get();const auto re=graph.addNode(std::move(reverb));
 if(!in||!out||!stomp||!s.amp||!cab||!eqNode||!normalized||!sp||!mo||!de||!re)return s;
 const std::array<AudioProcessorGraph::NodeID,11> chain{in->nodeID,stomp->nodeID,s.amp->nodeID,cab->nodeID,normalized->nodeID,eqNode->nodeID,sp->nodeID,mo->nodeID,de->nodeID,re->nodeID,out->nodeID};
 s.connected=true;for(size_t n=1;n<chain.size();++n)for(int c=0;c<2;++c)s.connected=graph.addConnection({{chain[n-1],c},{chain[n],c}})&&s.connected;
 s.pedal=stomp->nodeID;s.output=out->nodeID;s.normalization=normalized->nodeID;s.cabinet=cab->nodeID;return s;
}
void configureGuitarGraph(const GraphStages& s,board::Processor& pedals,amp::AmpSource& source,const output::Settings& settings,calibration::Settings calibration,bool cabinetEnabled) {
 auto* native=dynamic_cast<amp::NamAmp*>(&source);pedals.setCalibration(calibration);
 if(native){native->setCalibration(calibration);native->setUpstreamReference(&pedals.referenceRouting);}
 if(s.cabinetProcessor)s.cabinetProcessor->enable(cabinetEnabled);
 s.normalize->configure(settings.ampNormalizationEnabled,native!=nullptr,native?native->loudnessDb():std::nullopt,native?output::selectedNormalizationTrim(settings,native->normalizationTrimDb()):0.f);
 s.eq->configure(settings.ampEq,native!=nullptr);s.spread->configure(settings);s.reverb->configure(settings.reverb);
 pedals.pitch.configure(settings.pitch);s.mod->configure(settings.mod);s.delay->configure(settings.delay);
}
class RenderProcessor final:public IRenderProcessor {
public:
 RenderProcessor():device(graph){}
 ~RenderProcessor()override{device.releaseResources();if(source)source->unload();graph.clear();}
 void process(juce::AudioBuffer<float>& buffer)override{midi.clear();device.processBlock(buffer,midi);if(device.fault.load())throw std::runtime_error("Tone processing failed during export.");if(auto* nam=dynamic_cast<amp::NamAmp*>(source.get()))if(nam->takeFault())throw std::runtime_error("NAM processing failed during export.");}
 bool realtimeOnly()const noexcept override{return source&&source->type()==amp::SourceType::Vst3;}
 int latencySamples()const noexcept override{return device.getLatencySamples()+pitchLatency;}
 int pitchLatency=0;
 juce::AudioProcessorGraph graph;std::unique_ptr<amp::AmpSource> source;audio::DeviceRateProcessor device;juce::MidiBuffer midi;
};
std::unique_ptr<IRenderProcessor> createRenderProcessor(const juce::String& text,double rate,const RenderResources& resources,juce::String& error) {
 using namespace juce;const auto fail=[&](const String& message)->std::unique_ptr<IRenderProcessor>{error=message;return {};};
 try {
  auto xml=XmlDocument::parse(text);if(!xml)return fail("Cannot read the tone snapshot.");auto pedals=std::make_unique<board::Processor>();
  auto loaded=pedals->load(resources.models,resources.lite);if(loaded.failed())return fail(loaded.getErrorMessage());
  PluginDescription description;if(auto* plugin=xml->getChildByName("PLUGIN"))if(!description.loadFromXml(*plugin))return fail("Invalid plugin snapshot.");
  board::State state;auto decoded=board::decode(*xml,description,[&](int p,int preset){return pedals->identity(p,preset);},state);if(decoded.failed())return fail(decoded.getErrorMessage());
  auto out=std::make_unique<RenderProcessor>();
  if(state.sourceType==amp::SourceType::Nam){auto nam=amp::loadNamAmp(namamp::Library(resources.namLibrary),state.namAmp,resources.lite);if(nam.result.failed())return fail(nam.result.getErrorMessage());out->source=std::move(nam.source);}
  else {auto vst=vst3::load(description,&state.amp);if(vst.result.failed())return fail(vst.result.getErrorMessage());out->source=std::move(vst.source);}
  userpedals::Library personal(resources.userPedals);personal.refresh();
  for(int p=0;p<board::count;++p) {
   if(state.customPedalHashes[size_t(p)].isNotEmpty()) {
    auto custom=userpedals::prepare(personal.file(p,state.customPedalHashes[size_t(p)]),resources.lite,state.customPedalHashes[size_t(p)]);
    if(custom.result.failed())return fail("A personal pedal capture is missing. Export cancelled: "+custom.result.getErrorMessage());pedals->installCustom(p,std::move(custom.model));
   }pedals->set(p,state.pedals[size_t(p)]);
  }
  pedals->front.set(state.input);
  pedals->setInstrumentMode(state.instrumentMode);
  pedals->setRackOrder(state.rackOrder);
  pedals->setTriComp(state.triComp);
  pedals->setTriDrive(state.triDrive);
  auto* board=pedals.get();auto stages=buildGuitarGraph(out->graph,std::move(pedals),*out->source);if(!stages.connected)return fail("Cannot prepare export processing chain.");
  bool useCabinet=false;
  if(state.sourceType==amp::SourceType::Nam) {
   const auto capture=captures::resolveKind(namamp::Library(resources.namLibrary),state.namAmp.model,resources.factoryAmps);
   if(capture.result.failed())return fail(capture.result.getErrorMessage());
   useCabinet=cabinet::needsIR(capture.kind)&&!(state.cabinet&&state.cabinet->bypassed);
   if(useCabinet){if(!state.cabinet||state.cabinet->empty())return fail("This Head needs a cabinet IR before export.");auto cab=cabinet::prepare(resources.cabinetLibrary.getChildFile(state.cabinet->file),resources.cabinetLibrary,&*state.cabinet);if(cab.result.failed())return fail(cab.result.getErrorMessage());stages.cabinetProcessor->load(std::move(cab));}
  }
  configureGuitarGraph(stages,*board,*out->source,state.outputDsp.value_or(output::legacySettings()),resources.calibration,useCabinet);
  out->graph.setNonRealtime(!out->realtimeOnly());out->device.setNonRealtime(!out->realtimeOnly());out->device.prepareToPlay(rate,512);
  if(state.outputDsp&&state.outputDsp->pitch.mode!=pitch::Mode::Off)out->pitchLatency=(int)std::llround(board->pitch.latencySamples()*rate/48000.);
  return out;
 }catch(const std::exception& e){return fail("Export tone preparation failed: "+String(e.what()));}catch(...){return fail("Export tone preparation failed.");}
}
}
