#pragma once
#include "RigSupport.h"
#include "Effects/Pedalboard/PedalDefinitions.h"
#include "InputStage.h"
#include "Effects/Pitch/PitchEngine.h"
#include "Effects/Pedalboard/ToneTilt.h"
#include "Effects/AutoWah/AutoWahProcessor.h"
#include "Effects/GraphicEq/GraphicEqProcessor.h"
#include "Effects/Compressor/CompressorProcessor.h"
#include "Effects/NamCapture/CaptureModel.h"
#include "Effects/Pedalboard/PedalPresets.h"
#include <NAM/get_dsp.h>
#include "NamQuality.h"
#include "LevelCalibration.h"
#include "InstrumentMode.h"
#include "Effects/TriComp/TriCompProcessor.h"
#include "Effects/TriDrive/TriDriveProcessor.h"
#include <atomic>
#include <cmath>

namespace board
{
using namespace juce;
constexpr int presets = 8, chunk = 128;
inline String pedalName(int p) { return definition(p).name; }
struct Setting
{
    int preset = 0;
    bool enabled = false;
    float input = 0, mix = 1, output = 0, ratio = 2, tone = 0;
    float wahSensitivity=55, wahRange=75, wahDecay=160;
    std::array<float,7> eqBands {};
};
inline Setting defaults(int pedal, int preset)
{
    Setting s; s.preset = preset;
    if (pedal==id(PedalId::StudioComp)) { const auto& p = compressors[(size_t)preset]; s.input = p.threshold; s.mix = p.mix; s.output = p.makeup; s.ratio = p.ratio; }
    if (pedal==id(PedalId::CleanBoost)) s.input = (float) std::array<int,8>{ 1,2,3,4,6,8,10,12 }[(size_t)preset];
    if(pedal==id(PedalId::AutoWah)){const auto& w=wahPresets[(size_t)preset];s.wahSensitivity=w.sensitivity;s.wahRange=w.range;s.wahDecay=w.decay;}
    if(pedal==id(PedalId::GraphicEq))s.eqBands=eqPresets[(size_t)preset].bands;
    return s;
}
inline bool valid(const Setting& s, int pedal)
{
    for(auto db:s.eqBands)if(!std::isfinite(db)||db< -12||db>12)return false;
    if(!std::isfinite(s.wahSensitivity)||s.wahSensitivity<0||s.wahSensitivity>100||!std::isfinite(s.wahRange)||s.wahRange<0||s.wahRange>100||!std::isfinite(s.wahDecay)||s.wahDecay<50||s.wahDecay>800)return false;
    return s.preset >= 0 && s.preset < presetCount(pedal)+(isNam(pedal)?1:0) && std::isfinite(s.input) && std::isfinite(s.mix) && std::isfinite(s.output)
        && s.input >= (pedal==id(PedalId::StudioComp) ? -48.f : isNam(pedal) ? -12.f : -18.f) && s.input <= (pedal==id(PedalId::StudioComp) ? 0.f : isNam(pedal) ? 12.f : 18.f)
        && std::isfinite(s.tone) && s.tone >= -6 && s.tone <= 6
        && std::isfinite(s.ratio) && s.ratio >= 1 && s.ratio <= 20
        && s.mix >= 0 && s.mix <= 1 && s.output >= -24 && s.output <= 24;
}
class Processor final : public AudioProcessor
{
public:
    Processor() : AudioProcessor(BusesProperties().withInput("Guitar", AudioChannelSet::stereo(), true).withOutput("Pedals", AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < count; ++i) {models[size_t(i)].resize(size_t(presetCount(i)+(isNam(i)?1:0)));set(i, defaults(i, recommendedPreset(i)));}
        setTriComp(tonegrid::triComp::presetDefaults(0));
        setTriDrive(tonegrid::triDrive::presetDefaults(0));
    }
    void setInstrumentMode(tonegrid::InstrumentMode mode) noexcept
    {
        instrumentModeAtomic.store(static_cast<int>(mode), std::memory_order_release);
        const bool bass = tonegrid::isBass(mode);
        front.setBassMode(bass);
        wah.setBassMode(bass);
        if (prepared)
            eq.prepare(sr, get(id(PedalId::GraphicEq)).eqBands, bass ? tonegrid::bassEqFrequencies : eqFrequencies);
    }
    tonegrid::InstrumentMode instrumentMode() const noexcept
    {
        return static_cast<tonegrid::InstrumentMode>(instrumentModeAtomic.load(std::memory_order_acquire));
    }
    void setRackOrder(tonegrid::BassRackOrder ro) noexcept
    {
        rackOrderAtomic.store(static_cast<int>(ro), std::memory_order_release);
    }
    tonegrid::BassRackOrder rackOrder() const noexcept
    {
        return static_cast<tonegrid::BassRackOrder>(rackOrderAtomic.load(std::memory_order_acquire));
    }
    void setTriComp(const tonegrid::triComp::Parameters& p) noexcept
    {
        if (!tonegrid::triComp::valid(p)) return;
        triCompControl = p;
        triCompMailbox.publish(p);
    }
    tonegrid::triComp::Parameters getTriComp() const noexcept { return triCompControl; }
    void setTriDrive(const tonegrid::triDrive::Parameters& p) noexcept
    {
        if (!tonegrid::triDrive::valid(p)) return;
        triDriveControl = p;
        triDriveMailbox.publish(p);
    }
    tonegrid::triDrive::Parameters getTriDrive() const noexcept { return triDriveControl; }
    tonegrid::triComp::Processor& triCompProcessor() noexcept { return triComp; }
    const tonegrid::triComp::Processor& triCompProcessor() const noexcept { return triComp; }
    tonegrid::triDrive::Processor& triDriveProcessor() noexcept { return triDrive; }
    const tonegrid::triDrive::Processor& triDriveProcessor() const noexcept { return triDrive; }
    // One reserved user slot per NAM pedal. Install only with audio detached.
    // The library index is dynamic; unselected user captures do not own DSPs.
    void installCustom(int p,Model model)
    {jassert(MessageManager::existsAndIsCurrentThread());jassert(isNam(p));calibrateModel(model);models[size_t(p)][size_t(presetCount(p))]=std::move(model);customMissing[size_t(p)]=false;}
    // Control-thread availability only: keep the DSP alive while the existing
    // audio bypass ramp completes. It is released on the next detached install.
    void markCustomMissing(int p){jassert(MessageManager::existsAndIsCurrentThread());customMissing[size_t(p)]=true;auto s=get(p);s.enabled=false;set(p,s);}
    bool modelIndex(int p,int n) const{return p>=0&&p<count&&n>=0&&n<int(models[size_t(p)].size());}
    Result load(const File& directory,bool lite=false)
    {
        try
        {
            auto root = XmlDocument::parse(directory.getChildFile("catalog.xml"));
            if (! root || ! root->hasTagName("PedalCatalog")) return Result::fail("Missing Models/catalog.xml");
            for (int p = 0; p < count; ++p)
            {
                if(!isNam(p))continue;
                auto* bank = root->getChildByAttribute("id", String(p));
                if (! bank || bank->getNumChildElements() != presetCount(p)) return Result::fail("Unexpected capture count for " + pedalName(p));
                if(bank->getIntAttribute("recommended",-1)!=recommendedPreset(p))return Result::fail("Unexpected recommended capture for "+pedalName(p));
                int n = 0;
                for (auto* item : bank->getChildIterator())
                {
                    auto& m = models[(size_t)p][(size_t)n++];
                    m.label = item->getStringAttribute("label"); m.file = item->getStringAttribute("file");
                    m.hash = item->getStringAttribute("sha256"); m.details = item->getStringAttribute("details");
                    m.section = item->getStringAttribute("section",bank->getStringAttribute("section",item->getStringAttribute("author",bank->getStringAttribute("author"))));
                    m.creatorUrl=item->getStringAttribute("creatorUrl",bank->getStringAttribute("creatorUrl"));
                    auto file = directory.getChildFile(m.file);
                    if(!file.isAChildOf(directory))return Result::fail("Capture path is outside Models: "+m.file);
                    if(!file.existsAsFile()){m.hash.clear();continue;}
                    if (! file.isAChildOf(directory) || file.getSize() > 8 * 1024 * 1024
                        || SHA256(file).toHexString() != m.hash) return Result::fail("Missing or altered capture: " + m.file);
                    nam::DspLoadOptions options; options.prewarm = false;
                    auto payload=nlohmann::json::parse(file.loadFileAsString().toStdString());
                    m.calibration=tonegrid::calibration::metadata(payload);tonegrid::calibration::sanitiseMetadata(payload);
                    m.dsp = nam::get_dsp(payload, options);
                    if (m.dsp->NumInputChannels() != 1 || m.dsp->NumOutputChannels() != 1 || m.dsp->GetExpectedSampleRate() != 48000)
                        return Result::fail("Capture must be mono, 48 kHz: " + m.file);
                    m.dsp->SetPrewarmOnReset(false);m.dsp->Reset(48000,chunk);tonegrid::namquality::apply(*m.dsp,lite);m.dsp->prewarm();
                    calibrateModel(m);
                }
            }
            loaded = true; return Result::ok();
        }
        catch (const std::exception& e) { return Result::fail("NAM: " + String(e.what())); }
    }
    // Control thread only, with the player detached: model gains are audio-owned.
    void setCalibration(tonegrid::calibration::Settings value)
    {
        jassert(MessageManager::existsAndIsCurrentThread());
        
        calibrationSettings=value;
        for(auto& bank:models)for(auto& model:bank)if(model.dsp)calibrateModel(model);
    }
    // Caller must detach the audio player before changing model tiers.
    int setNamLite(bool lite)
    {
        int compatible=0;
        for(auto& bank:models)for(auto& model:bank)
            if(model.dsp&&tonegrid::namquality::apply(*model.dsp,lite)){model.dsp->prewarm();++compatible;}
        return compatible;
    }
    bool available(int p,int n) const {return modelIndex(p,n)&&(!isNam(p)||(models[(size_t)p][(size_t)n].dsp!=nullptr&&!(n==presetCount(p)&&customMissing[size_t(p)])));}
    String label(int p, int n) const
    {
        if(!modelIndex(p,n))return "Capture unavailable";
        if (p==id(PedalId::StudioComp)) return compressors[(size_t)n].name;
        if (p==id(PedalId::CleanBoost)) return String(n+1).paddedLeft('0',2) + " Boost +" + String(defaults(p,n).input,0) + " dB";
        if(p==id(PedalId::AutoWah))return wahPresets[(size_t)n].name;
        if(p==id(PedalId::GraphicEq))return tonegrid::isBass(instrumentMode()) ? tonegrid::bassEqPresets[(size_t)n].name : eqPresets[(size_t)n].name;
        return models[(size_t)p][(size_t)n].label;
    }
    tonegrid::calibration::Metadata calibrationInfo(int p,int n) const{return modelIndex(p,n)?models[(size_t)p][(size_t)n].calibration:tonegrid::calibration::Metadata{};}
    bool completeCalibrationChain() const
    {for(const int p:order)if(isNam(p)){const auto s=get(p);if(s.enabled&&!calibrationInfo(p,s.preset).complete())return false;}return true;}
    String details(int p, int n) const { return modelIndex(p,n)?models[(size_t)p][(size_t)n].details:String{}; }
    String captureSection(int p,int n) const {return !isNam(p)?String("ToneGrid"):modelIndex(p,n)?models[size_t(p)][size_t(n)].section:String{};}
    String captureCreatorUrl(int p,int n) const {return isNam(p)&&modelIndex(p,n)?models[size_t(p)][size_t(n)].creatorUrl:String{};}
    String modelFile(int p,int n) const {return modelIndex(p,n)?models[(size_t)p][(size_t)n].file:String{};}
    String identity(int p, int n) const { if(!modelIndex(p,n))return {};return isNam(p) ? models[(size_t)p][(size_t)n].hash : p==id(PedalId::AutoWah)?"auto-wah-v2":p==id(PedalId::GraphicEq)?"graphic-eq-v1":"algorithm-v1"; }
    Setting get(int p) const
    { const auto& a = controls[(size_t)p]; Setting s{ a.preset.load(), a.enabled.load(), a.input.load(), a.mix.load(), a.output.load(), a.ratio.load(), a.tone.load() };s.wahSensitivity=a.wahSensitivity.load();s.wahRange=a.wahRange.load();s.wahDecay=a.wahDecay.load();for(size_t b=0;b<7;++b)s.eqBands[b]=a.eqBands[b].load();return s; }
    void set(int p, const Setting& s)
    { auto& a = controls[(size_t)p]; if(!modelIndex(p,s.preset)){a.enabled.store(false);settings[(size_t)p].publish(get(p));return;} a.input.store(isNam(p)?jlimit(-12.f,12.f,s.input):s.input); a.mix.store(isNam(p)?1.f:s.mix); a.output.store(s.output); a.ratio.store(s.ratio); a.tone.store(s.tone); a.wahSensitivity.store(s.wahSensitivity);a.wahRange.store(s.wahRange);a.wahDecay.store(s.wahDecay);for(size_t b=0;b<7;++b)a.eqBands[b].store(s.eqBands[b]);a.preset.store(s.preset); a.enabled.store(s.enabled&&(!loaded||available(p,s.preset)));settings[(size_t)p].publish(get(p)); }
    void prepareToPlay(double rate, int) override
    {
        rateOK.store(rate == 48000); sr = rate;
        const bool bass = tonegrid::isBass(instrumentMode());
        front.setBassMode(bass);
        front.prepare(rate);pitch.prepare(rate);
        for (int p = 0; p < count; ++p)
        {
            const auto idx=size_t(p);const auto oldIn=calibrationIn[idx].getCurrentValue(),oldOut=calibrationOut[idx].getCurrentValue();
            calibrationIn[idx].reset(rate,.02);calibrationOut[idx].reset(rate,.02);
            const auto& model=models[idx][size_t(get(p).preset)];
            calibrationIn[idx].setCurrentAndTargetValue(prepared?oldIn:model.inputGain);
            calibrationOut[idx].setCurrentAndTargetValue(prepared?oldOut:model.outputGain);
            wet[(size_t)p].reset(rate, .008); wet[(size_t)p].setCurrentAndTargetValue(0);
            smoothIn[(size_t)p] = get(p).input; smoothMix[(size_t)p] = get(p).mix; smoothOut[(size_t)p] = get(p).output;
            toneFilters[(size_t)p].prepare(rate, get(p).tone);
            namMute[(size_t)p].reset(rate,.004); namMute[(size_t)p].setCurrentAndTargetValue(1);
            namActive[(size_t)p]=get(p).enabled; current[(size_t)p]=get(p).preset;
        }
        const auto w=get(id(PedalId::AutoWah));wah.setBassMode(bass);wah.prepare(rate,w.wahSensitivity,w.wahRange,w.wahDecay,w.preset);
        eq.prepare(rate,get(id(PedalId::GraphicEq)).eqBands,bass?tonegrid::bassEqFrequencies:eqFrequencies);
        triComp.prepare(rate, triCompControl);
        triDrive.prepare(rate, triDriveControl);
        compressor.reset();prepared=true;
    }
    void releaseResources() override {}
    void processBlock(AudioBuffer<float>& audio, MidiBuffer& midi) override
    {
        ScopedNoDenormals noDenormals; midi.clear();
        if (! loaded || ! rateOK.load()) { audio.clear(); return; }
        auto* left = audio.getWritePointer(0); front.process(left,audio.getNumSamples());pitch.process(left,audio.getNumSamples());
        const auto mode = instrumentMode();
        const bool bassModeActive = tonegrid::isBass(mode);
        referenceRouting.chunks=jmin(int(referenceRouting.known.size()),(audio.getNumSamples()+chunk-1)/chunk);
        for (int pos = 0; pos < audio.getNumSamples(); pos += chunk)
        {
            const int length = jmin(chunk, audio.getNumSamples() - pos);
            bool referenceKnown=tonegrid::calibration::active(calibrationSettings);
            bool bassRackProcessedInChunk = false;
            for (const int p : order)
            {
                if (bassModeActive)
                {
                    // In Bass mode, run TRI-COMP and TRI-DRIVE right after AutoWah and Classic/Vintage Fuzz
                    // (where StudioComp / overdrives used to sit), before GraphicEq, CleanBoost, and the Amp!
                    if (!bassRackProcessedInChunk && p == id(PedalId::StudioComp))
                    {
                        bassRackProcessedInChunk = true;
                        const auto tcParams = triCompMailbox.read();
                        const auto tdParams = triDriveMailbox.read();
                        if (rackOrder() == tonegrid::BassRackOrder::CompThenDrive)
                        {
                            triComp.processBlock(left + pos, length, tcParams);
                            triDrive.processBlock(left + pos, length, tdParams);
                        }
                        else
                        {
                            triDrive.processBlock(left + pos, length, tdParams);
                            triComp.processBlock(left + pos, length, tcParams);
                        }
                    }
                    if (tonegrid::isPedalRemovedInBassMode(p))
                        continue;
                }
                const auto idx = (size_t)p; const auto s = settings[idx].read();
                auto& fade = wet[idx];
                if (isNam(p))
                {
                    // Switch serial paths only at silence, never blend dry with NAM.
                    auto& mute=namMute[idx];
                    if (namActive[idx]!=s.enabled || current[idx]!=s.preset)
                    {
                        mute.setTargetValue(0);
                        if (!mute.isSmoothing() && mute.getCurrentValue()==0)
                        { namActive[idx]=s.enabled; current[idx]=s.preset; toneFilters[idx].reset(); mute.setTargetValue(1); }
                    }
                    else mute.setTargetValue(1);
                    if (!namActive[idx])
                    { for(int j=0;j<length;++j)left[pos+j]*=mute.getNextValue(); continue; }
                    toneFilters[idx].setTone(s.tone);
                }
                else
                {
                // Fade through dry before switching capture; all models are already loaded and warmed.
                if (current[idx] != s.preset)
                {
                    fade.setTargetValue(0);
                    if (! fade.isSmoothing() && fade.getCurrentValue() == 0) current[idx] = s.preset;
                }
                if (current[idx] == s.preset) fade.setTargetValue(s.enabled ? 1.f : 0.f);
                if (! fade.isSmoothing() && fade.getCurrentValue() == 0) {if(p==id(PedalId::AutoWah))wah.reset();if(p==id(PedalId::GraphicEq))eq.reset();continue;}
                }
                if(p==id(PedalId::AutoWah))wah.set(s.wahSensitivity,s.wahRange,s.wahDecay,s.preset);
                if(p==id(PedalId::GraphicEq))eq.set(s.eqBands);
                auto& model = models[idx][(size_t)current[idx]];
                const float calibratedInput=isNam(p)&&!referenceKnown?1.f:model.inputGain;
                calibrationIn[idx].setTargetValue(calibratedInput);calibrationOut[idx].setTargetValue(model.outputGain);
                if(isNam(p))referenceKnown=tonegrid::calibration::active(calibrationSettings)&&!model.calibration.invalid&&model.calibration.output.has_value();
                const float smoothing = (float)(1.0 - std::exp(-1.0 / (.01 * sr)));
                for (int j = 0; j < length; ++j)
                {
                    smoothIn[idx] += smoothing * (s.input - smoothIn[idx]);
                    smoothMix[idx] += smoothing * (s.mix - smoothMix[idx]);
                    smoothOut[idx] += smoothing * (s.output - smoothOut[idx]);
                    input[(size_t)j] = left[pos+j] * (p==id(PedalId::StudioComp) ? 1.f : Decibels::decibelsToGain(smoothIn[idx])) * calibrationIn[idx].getNextValue();
                    mixes[(size_t)j] = smoothMix[idx]; gains[(size_t)j] = Decibels::decibelsToGain(smoothOut[idx]);
                    thresholds[(size_t)j] = smoothIn[idx];
                }
                if (isNam(p))
                {
                    float* in = input.data(); float* out = output.data(); try{model.dsp->process(&in, &out, length);}catch(...){audio.clear();fault.store(true);return;}
                }
                else if(p==id(PedalId::AutoWah))for(int j=0;j<length;++j)output[(size_t)j]=wah.process(input[(size_t)j]);
                else if(p==id(PedalId::GraphicEq))for(int j=0;j<length;++j)output[(size_t)j]=eq.process(input[(size_t)j]);
                else if (p==id(PedalId::CleanBoost)) std::copy_n(input.begin(), length, output.begin());
                else
                {
                    compressor.process(input.data(),output.data(),thresholds.data(),length,s.ratio,sr,compressors[(size_t)current[idx]]);
                }
                for (int j = 0; j < length; ++j)
                {
                    const auto k = (size_t)j; const float dry = left[pos+j];
                    float processed = isNam(p)
                        ? toneFilters[idx].process(output[k]*calibrationOut[idx].getNextValue()) * gains[k]
                        : ((1-mixes[k])*dry + mixes[k]*output[k]*calibrationOut[idx].getNextValue()) * gains[k];
                    if (! std::isfinite(processed)) { processed = 0; fault.store(true); }
                    left[pos+j] = isNam(p) ? processed * namMute[idx].getNextValue() : dry + fade.getNextValue() * (processed-dry);
                }
            }
            outputReferenceKnown.store(referenceKnown,std::memory_order_relaxed);
            if(pos/chunk<referenceRouting.chunks)referenceRouting.known[size_t(pos/chunk)]=referenceKnown;
        }
        for (int c = 1; c < audio.getNumChannels(); ++c) audio.copyFrom(c, 0, audio, 0, 0, audio.getNumSamples());
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainInputChannelSet() == AudioChannelSet::stereo() && l.getMainOutputChannelSet() == AudioChannelSet::stereo(); }
    const String getName() const override { return "Curated Pedalboard"; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    bool hasEditor() const override { return false; } AudioProcessorEditor* createEditor() override { return nullptr; }
    void getStateInformation(MemoryBlock&) override {} void setStateInformation(const void*, int) override {}
    std::atomic<bool> outputReferenceKnown{false};
    tonegrid::calibration::ReferenceRouting referenceRouting;
    InputStage front;
    tonegrid::PitchEngine pitch;
    std::atomic<bool> rateOK { true }, fault { false };
private:
    tonegrid::calibration::Settings calibrationSettings;
    std::array<SmoothedValue<float>,count> calibrationIn,calibrationOut;bool prepared=false;
    void calibrateModel(Model& model)
    {
        const auto gains=model.calibration.invalid?tonegrid::calibration::PedalGains{}:tonegrid::calibration::pedalGains(calibrationSettings,model.calibration.input,model.calibration.output);
        model.inputGain=gains.input;model.outputGain=gains.output;
    }
    struct Control { std::atomic<int> preset {0}; std::atomic<bool> enabled {false}; std::atomic<float> input {0}, mix {1}, output {0}, ratio {2}, tone {0}, wahSensitivity{55},wahRange{75},wahDecay{160}; std::array<std::atomic<float>,7> eqBands{}; };
    AutoWah wah;GraphicEq eq;CompressorProcessor compressor;
    tonegrid::triComp::Processor triComp;
    tonegrid::triDrive::Processor triDrive;
    tonegrid::triComp::Parameters triCompControl{};
    tonegrid::triDrive::Parameters triDriveControl{};
    tonegrid::ParameterMailbox<tonegrid::triComp::Parameters> triCompMailbox;
    tonegrid::ParameterMailbox<tonegrid::triDrive::Parameters> triDriveMailbox;
    std::atomic<int> instrumentModeAtomic{ static_cast<int>(tonegrid::InstrumentMode::Guitar) };
    std::atomic<int> rackOrderAtomic{ static_cast<int>(tonegrid::BassRackOrder::CompThenDrive) };
    std::array<Control,count> controls;
    std::array<tonegrid::ParameterMailbox<Setting>,count> settings;
    std::array<std::vector<Model>,count> models;
    std::array<bool,count> customMissing{};
    std::array<SmoothedValue<float>,count> wet;
    std::array<ToneTilt,count> toneFilters;
    std::array<SmoothedValue<float>,count> namMute;
    std::array<bool,count> namActive {};
    std::array<int,count> current {};
    std::array<float,count> smoothIn {}, smoothMix {}, smoothOut {};
    std::array<float,chunk> input {}, output {}, mixes {}, gains {}, thresholds {};
    bool loaded = false; double sr = 48000;
};
}
