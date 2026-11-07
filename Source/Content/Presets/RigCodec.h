#pragma once
#include "Content/Presets/RigMigrations.h"
#include "Content/Presets/PresetMetadata.h"
#include "Vst3Identity.h"
#include "Vst3State.h"

namespace board
{
inline Result ampCompatibility(const PluginDescription& saved,const PluginDescription& actual,bool generic)
{
    const bool same=generic?vst3::Identity::of(saved).valid()&&vst3::Identity::of(saved)==vst3::Identity::of(actual):saved.isDuplicateOf(actual);
    return same?Result::ok():Result::fail(generic?"VST3 identity differs from this saved rig.":"TONEX identity differs from this saved rig.");
}
inline XmlElement encodeBassRack(const State& s)
{
    XmlElement rack("BassRack");
    rack.setAttribute("version", 1);
    rack.setAttribute("mode", static_cast<int>(s.instrumentMode));
    rack.setAttribute("order", static_cast<int>(s.rackOrder));

    auto* tc = rack.createNewChildElement("TriComp");
    tc->setAttribute("enabled", s.triComp.enabled);
    tc->setAttribute("preset", s.triComp.preset);
    tc->setAttribute("inputDb", s.triComp.inputDb);
    tc->setAttribute("outputDb", s.triComp.outputDb);
    tc->setAttribute("mix", s.triComp.mix);
    tc->setAttribute("lowMidHz", s.triComp.lowMidHz);
    tc->setAttribute("midHighHz", s.triComp.midHighHz);
    tc->setAttribute("linkAmount", s.triComp.linkAmount);
    tc->setAttribute("audioHighPass", s.triComp.audioHighPass);
    for (int b = 0; b < 3; ++b)
    {
        const auto& band = s.triComp.bands[static_cast<size_t>(b)];
        auto* bn = tc->createNewChildElement("Band");
        bn->setAttribute("index", b);
        bn->setAttribute("ratio", static_cast<int>(band.ratio));
        bn->setAttribute("attackMs", band.attackMs);
        bn->setAttribute("releaseMs", band.releaseMs);
        bn->setAttribute("audioMode", static_cast<int>(band.audioMode));
        bn->setAttribute("detectorMode", static_cast<int>(band.detectorMode));
        bn->setAttribute("levelDb", band.levelDb);
        bn->setAttribute("solo", band.solo);
        bn->setAttribute("mute", band.mute);
    }

    auto* td = rack.createNewChildElement("TriDrive");
    td->setAttribute("enabled", s.triDrive.enabled);
    td->setAttribute("preset", s.triDrive.preset);
    td->setAttribute("inputDb", s.triDrive.inputDb);
    td->setAttribute("outputDb", s.triDrive.outputDb);
    td->setAttribute("blend", s.triDrive.blend);
    td->setAttribute("lowMidHz", s.triDrive.lowMidHz);
    td->setAttribute("midHighHz", s.triDrive.midHighHz);
    td->setAttribute("lowComp", s.triDrive.lowComp);
    td->setAttribute("lowLpfHz", s.triDrive.lowLpfHz);
    td->setAttribute("lowLevelDb", s.triDrive.lowLevelDb);
    td->setAttribute("lowSolo", s.triDrive.lowSolo);
    td->setAttribute("lowMute", s.triDrive.lowMute);
    td->setAttribute("midCharacter", static_cast<int>(s.triDrive.midCharacter));
    td->setAttribute("midDrive", s.triDrive.midDrive);
    td->setAttribute("midPresence", s.triDrive.midPresence);
    td->setAttribute("midLevelDb", s.triDrive.midLevelDb);
    td->setAttribute("midSolo", s.triDrive.midSolo);
    td->setAttribute("midMute", s.triDrive.midMute);
    td->setAttribute("highCharacter", static_cast<int>(s.triDrive.highCharacter));
    td->setAttribute("highDrive", s.triDrive.highDrive);
    td->setAttribute("highHpfHz", s.triDrive.highHpfHz);
    td->setAttribute("highLevelDb", s.triDrive.highLevelDb);
    td->setAttribute("highSolo", s.triDrive.highSolo);
    td->setAttribute("highMute", s.triDrive.highMute);
    td->setAttribute("cabSimEnabled", s.triDrive.cabSimEnabled);
    td->setAttribute("postEqEnabled", s.triDrive.postEqEnabled);
    for (int b = 0; b < 7; ++b)
        td->setAttribute("eq" + String(b), s.triDrive.postEqBands[static_cast<size_t>(b)]);
    if (s.triDriveAssets.midNamFile.isNotEmpty())
    {
        td->setAttribute("midNamFile", s.triDriveAssets.midNamFile);
        td->setAttribute("midNamHash", s.triDriveAssets.midNamHash);
        td->setAttribute("midNamLabel", s.triDriveAssets.midNamLabel);
    }
    if (s.triDriveAssets.highNamFile.isNotEmpty())
    {
        td->setAttribute("highNamFile", s.triDriveAssets.highNamFile);
        td->setAttribute("highNamHash", s.triDriveAssets.highNamHash);
        td->setAttribute("highNamLabel", s.triDriveAssets.highNamLabel);
    }
    if (s.triDriveAssets.cabIrFile.isNotEmpty())
    {
        td->setAttribute("cabIrFile", s.triDriveAssets.cabIrFile);
        td->setAttribute("cabIrHash", s.triDriveAssets.cabIrHash);
        td->setAttribute("cabIrLabel", s.triDriveAssets.cabIrLabel);
    }
    return rack;
}

inline bool decodeBassRack(const XmlElement& rack, State& out)
{
    if (rack.getIntAttribute("version", 0) != 1)
        return false;
    const int modeInt = rack.getIntAttribute("mode", 0);
    const int orderInt = rack.getIntAttribute("order", 0);
    if (modeInt < 0 || modeInt > 1 || orderInt < 0 || orderInt > 1)
        return false;
    out.instrumentMode = static_cast<tonegrid::InstrumentMode>(modeInt);
    out.rackOrder = static_cast<tonegrid::BassRackOrder>(orderInt);

    if (const auto* tc = rack.getChildByName("TriComp"))
    {
        auto p = tonegrid::triComp::presetDefaults(tc->getIntAttribute("preset", 0));
        p.enabled = tc->getBoolAttribute("enabled", false);
        p.preset = tc->getIntAttribute("preset", 0);
        p.inputDb = static_cast<float>(tc->getDoubleAttribute("inputDb", p.inputDb));
        p.outputDb = static_cast<float>(tc->getDoubleAttribute("outputDb", p.outputDb));
        p.mix = static_cast<float>(tc->getDoubleAttribute("mix", p.mix));
        p.lowMidHz = static_cast<float>(tc->getDoubleAttribute("lowMidHz", p.lowMidHz));
        p.midHighHz = static_cast<float>(tc->getDoubleAttribute("midHighHz", p.midHighHz));
        p.linkAmount = static_cast<float>(tc->getDoubleAttribute("linkAmount", p.linkAmount));
        p.audioHighPass = tc->getBoolAttribute("audioHighPass", p.audioHighPass);
        for (const auto* bn : tc->getChildWithTagNameIterator("Band"))
        {
            const int idx = bn->getIntAttribute("index", -1);
            if (idx < 0 || idx >= 3)
                return false;
            auto& b = p.bands[static_cast<size_t>(idx)];
            b.ratio = static_cast<tonegrid::multiband::DistressorRatio>(bn->getIntAttribute("ratio", static_cast<int>(b.ratio)));
            b.attackMs = static_cast<float>(bn->getDoubleAttribute("attackMs", b.attackMs));
            b.releaseMs = static_cast<float>(bn->getDoubleAttribute("releaseMs", b.releaseMs));
            b.audioMode = static_cast<tonegrid::multiband::DistressorAudioMode>(bn->getIntAttribute("audioMode", static_cast<int>(b.audioMode)));
            b.detectorMode = static_cast<tonegrid::multiband::DetectorMode>(bn->getIntAttribute("detectorMode", static_cast<int>(b.detectorMode)));
            b.levelDb = static_cast<float>(bn->getDoubleAttribute("levelDb", b.levelDb));
            b.solo = bn->getBoolAttribute("solo", false);
            b.mute = bn->getBoolAttribute("mute", false);
        }
        if (!tonegrid::triComp::valid(p))
            return false;
        out.triComp = p;
    }

    if (const auto* td = rack.getChildByName("TriDrive"))
    {
        auto p = tonegrid::triDrive::presetDefaults(td->getIntAttribute("preset", 0));
        p.enabled = td->getBoolAttribute("enabled", false);
        p.preset = td->getIntAttribute("preset", 0);
        p.inputDb = static_cast<float>(td->getDoubleAttribute("inputDb", p.inputDb));
        p.outputDb = static_cast<float>(td->getDoubleAttribute("outputDb", p.outputDb));
        p.blend = static_cast<float>(td->getDoubleAttribute("blend", p.blend));
        p.lowMidHz = static_cast<float>(td->getDoubleAttribute("lowMidHz", p.lowMidHz));
        p.midHighHz = static_cast<float>(td->getDoubleAttribute("midHighHz", p.midHighHz));
        p.lowComp = static_cast<float>(td->getDoubleAttribute("lowComp", p.lowComp));
        p.lowLpfHz = static_cast<float>(td->getDoubleAttribute("lowLpfHz", p.lowLpfHz));
        p.lowLevelDb = static_cast<float>(td->getDoubleAttribute("lowLevelDb", p.lowLevelDb));
        p.lowSolo = td->getBoolAttribute("lowSolo", false);
        p.lowMute = td->getBoolAttribute("lowMute", false);
        p.midCharacter = static_cast<tonegrid::multiband::DriveCharacter>(td->getIntAttribute("midCharacter", static_cast<int>(p.midCharacter)));
        p.midDrive = static_cast<float>(td->getDoubleAttribute("midDrive", p.midDrive));
        p.midPresence = static_cast<float>(td->getDoubleAttribute("midPresence", p.midPresence));
        p.midLevelDb = static_cast<float>(td->getDoubleAttribute("midLevelDb", p.midLevelDb));
        p.midSolo = td->getBoolAttribute("midSolo", false);
        p.midMute = td->getBoolAttribute("midMute", false);
        p.highCharacter = static_cast<tonegrid::multiband::DriveCharacter>(td->getIntAttribute("highCharacter", static_cast<int>(p.highCharacter)));
        p.highDrive = static_cast<float>(td->getDoubleAttribute("highDrive", p.highDrive));
        p.highHpfHz = static_cast<float>(td->getDoubleAttribute("highHpfHz", p.highHpfHz));
        p.highLevelDb = static_cast<float>(td->getDoubleAttribute("highLevelDb", p.highLevelDb));
        p.highSolo = td->getBoolAttribute("highSolo", false);
        p.highMute = td->getBoolAttribute("highMute", false);
        p.cabSimEnabled = td->getBoolAttribute("cabSimEnabled", true);
        p.postEqEnabled = td->getBoolAttribute("postEqEnabled", true);
        for (int b = 0; b < 7; ++b)
            p.postEqBands[static_cast<size_t>(b)] = static_cast<float>(td->getDoubleAttribute("eq" + String(b), p.postEqBands[static_cast<size_t>(b)]));
        if (!tonegrid::triDrive::valid(p))
            return false;
        out.triDrive = p;
        out.triDriveAssets.midNamFile = td->getStringAttribute("midNamFile");
        out.triDriveAssets.midNamHash = td->getStringAttribute("midNamHash");
        out.triDriveAssets.midNamLabel = td->getStringAttribute("midNamLabel");
        out.triDriveAssets.highNamFile = td->getStringAttribute("highNamFile");
        out.triDriveAssets.highNamHash = td->getStringAttribute("highNamHash");
        out.triDriveAssets.highNamLabel = td->getStringAttribute("highNamLabel");
        out.triDriveAssets.cabIrFile = td->getStringAttribute("cabIrFile");
        out.triDriveAssets.cabIrHash = td->getStringAttribute("cabIrHash");
        out.triDriveAssets.cabIrLabel = td->getStringAttribute("cabIrLabel");
    }
    return true;
}

// Pure XML conversion/validation. A failed decode never publishes its candidate.
inline XmlElement encode(const State& original, const PluginDescription& description, const RigIdentityLookup& identity)
{
    State snapshot=original;bool custom=false;
    for(int p=0;p<count;++p)custom|=isNam(p)&&snapshot.pedals[size_t(p)].preset==presetCount(p);
    if((custom||snapshot.cabinet)&&!snapshot.outputDsp)snapshot.outputDsp=tonegrid::output::legacySettings();
    const auto& s=snapshot;
    const bool native=s.sourceType==amp::SourceType::Nam;
    const bool modern=s.outputDsp.has_value();
    XmlElement xml("PedalRig"); xml.setAttribute("version", modern&&tonegrid::ampEq::hasDepth(s.outputDsp->ampEq)?16:custom?15:modern?(tonegrid::ampEq::advanced(s.outputDsp->ampEq)?14:tonegrid::output::requiresAmpEq(*s.outputDsp)?13:tonegrid::output::requiresExpanded(*s.outputDsp)?12:s.outputDsp->reverb.type==tonegrid::reverb::Type::Spring?11:10):native?8:s.genericVst3?7:6); xml.setAttribute("name", s.name);
    if(native&&s.cabinet){xml.setAttribute("version",17);xml.addChildElement(new XmlElement(tonegrid::cabinet::encode(*s.cabinet)));}
    if(modern)xml.addChildElement(new XmlElement(tonegrid::output::encode(*s.outputDsp)));
    if(native)xml.addChildElement(new XmlElement(namamp::encode(s.namAmp)));
    else {xml.addChildElement(description.createXml().release());if(s.genericVst3||modern)xml.addChildElement(new XmlElement(vst3::Identity::of(description).xml()));}
    auto* input=xml.createNewChildElement("Input"); input->setAttribute("gain",s.input.gainDb);input->setAttribute("threshold",s.input.thresholdDb);input->setAttribute("gate",s.input.gate);
    if(!native){auto* amp = xml.createNewChildElement("Amp"); amp->setAttribute("bytes", (int)s.amp.getSize());
    amp->setAttribute("sha256", SHA256(s.amp).toHexString()); amp->addTextElement(s.amp.toBase64Encoding());}
    for (int p = 0; p < count; ++p)
    {
        const auto& v = s.pedals[(size_t)p]; auto* item = xml.createNewChildElement("Pedal");
        item->setAttribute("id", p); item->setAttribute("preset", v.preset); item->setAttribute("enabled", v.enabled);
        item->setAttribute("input", v.input); if(isNam(p))item->setAttribute("tone",v.tone);else item->setAttribute("mix", v.mix); item->setAttribute("output", v.output); item->setAttribute("ratio",v.ratio);
        if(p==id(PedalId::AutoWah)){item->setAttribute("wahSensitivity",v.wahSensitivity);item->setAttribute("wahRange",v.wahRange);item->setAttribute("wahDecay",v.wahDecay);}
        if(p==id(PedalId::GraphicEq))for(int b=0;b<7;++b)item->setAttribute("eq"+String(b),v.eqBands[(size_t)b]);
        item->setAttribute("model",isNam(p)&&v.preset==presetCount(p)&&s.customPedalHashes[size_t(p)].isNotEmpty()?s.customPedalHashes[size_t(p)]:identity(p,v.preset));
        if(isNam(p)&&v.preset==presetCount(p))item->setAttribute("personalCapture",true);
    }
    if (tonegrid::isBass(s.instrumentMode) || s.triComp.enabled || s.triDrive.enabled)
        xml.addChildElement(new XmlElement(encodeBassRack(s)));
    tonegrid::presets::restoreExtensions(xml, s.extensionXml);
    if(native&&s.cabinet&&s.cabinet->bypassed)
    {auto features=StringArray::fromTokens(xml.getStringAttribute("requiredFeatures"),",",{});features.addIfNotAlreadyThere("cabinet-bypass");xml.setAttribute("requiredFeatures",features.joinIntoString(","));}
    xml.setAttribute("presetId", s.presetId.isEmpty() ? "legacy-" + SHA256(xml.toString().toUTF8()).toHexString() : s.presetId);
    return xml;
}
inline Result decode(const XmlElement& xml, const PluginDescription& expected, const RigIdentityLookup& identity, State& result)
{
    const int version=xml.getIntAttribute("version");
    if (! xml.hasTagName("PedalRig") || version<3 || version>17) return Result::fail("A version 3 through 17 pedalboard rig is required.");
    if (const auto compatible = tonegrid::presets::compatibility(xml); compatible.failed()) return compatible;
    State candidate = migrations::initialState();
    candidate.presetId = xml.getStringAttribute("presetId", "legacy-" + SHA256(xml.toString().toUTF8()).toHexString());
    if (candidate.presetId.isEmpty() || candidate.presetId.length() > 128) return Result::fail("Invalid preset ID.");
    candidate.extensionXml = tonegrid::presets::captureExtensions(xml);
    if (const auto* bassNode = xml.getChildByName("BassRack"))
    {
        if (!decodeBassRack(*bassNode, candidate))
            return Result::fail("Invalid BassRack settings.");
    }
    if(version>=9)
    {
        int countOutput=0;for(auto* child:xml.getChildIterator())countOutput+=child->hasTagName("OutputDSP")?1:0;
        auto* node=xml.getChildByName("OutputDSP");tonegrid::output::Settings settings;
        if(countOutput!=1||!node||(version!=15&&version!=17&&node->getStringAttribute("version")!=(version==9?"1":version==10?"2":version==11?"3":version==12?"4":version==13?"5":version==16?"7":"6"))||!tonegrid::output::decode(*node,settings))return Result::fail("Invalid output DSP settings.");
        candidate.outputDsp=settings;
    }
    const auto* sourceNode=xml.getChildByName("AmpSource");
    const bool native=version==8||(version>=9&&sourceNode&&sourceNode->getStringAttribute("type")=="NAM");
    const bool generic=version==7||(version>=9&&!native);
    if((version==17&&!native)||(version!=17&&xml.getChildByName("CabinetIR")))return Result::fail("Unexpected cabinet IR preset state.");
    if(native)
    {
        int sources=0;for(auto* child:xml.getChildIterator())sources+=child->hasTagName("AmpSource")?1:0;
        auto* source=xml.getChildByName("AmpSource");
        if(sources!=1||xml.getChildByName("PLUGIN")||xml.getChildByName("Amp")||!source||!namamp::decode(*source,candidate.namAmp))return Result::fail("Invalid NAM amp rig identity/settings.");
        candidate.sourceType=amp::SourceType::Nam;
        if(version==17){int count=0;for(auto* child:xml.getChildIterator())count+=child->hasTagName("CabinetIR");auto* node=xml.getChildByName("CabinetIR");tonegrid::cabinet::State cab;if(count!=1||!node||!tonegrid::cabinet::decode(*node,cab))return Result::fail("Invalid cabinet IR preset.");candidate.cabinet=cab;}
        if(source->getStringAttribute("file")!=candidate.namAmp.model.file&&!namamp::normalizationMetadataReplacement(source->getStringAttribute("sha256"))){if(!candidate.outputDsp)candidate.outputDsp=tonegrid::output::freshSessionSettings();if(!namamp::withdrawnSilverJubilee(source->getStringAttribute("sha256")))candidate.outputDsp->ampEq.enabled=false;candidate.missingCaptureDiagnostics.add(namamp::withdrawnSilverJubilee(source->getStringAttribute("sha256"))?"Withdrawn Silver Jubilee capture replaced with the approved Wizard Modern Classic capture.":"Withdrawn Princeton capture replaced with an approved Fender Ramparte capture.");}
    }
    else
    {
    PluginDescription saved; auto* plugin = xml.getChildByName("PLUGIN");
    if (!plugin||!saved.loadFromXml(*plugin))return Result::fail(generic?"Invalid VST3 rig description.":"TONEX identity or version differs from this saved rig.");
    if(generic)
    {
        int descriptions=0,sources=0;for(auto* child:xml.getChildIterator()){descriptions+=child->hasTagName("PLUGIN")?1:0;sources+=child->hasTagName("AmpSource")?1:0;}
        if(descriptions!=1||sources!=1)return Result::fail("Invalid VST3 rig identity.");
        vst3::Identity key;auto* source=xml.getChildByName("AmpSource");
        if(!source||!vst3::Identity::read(*source,key)||!(key==vst3::Identity::of(saved)))return Result::fail("Invalid VST3 rig identity.");
    }
    if(const auto compatible=ampCompatibility(saved,expected,generic);compatible.failed())return compatible;
    candidate.genericVst3=generic;
    }
    if(auto* input=xml.getChildByName("Input")){candidate.input={ (float)input->getDoubleAttribute("gain",999),(float)input->getDoubleAttribute("threshold",999),input->getBoolAttribute("gate")};if(!validInput(candidate.input))return Result::fail("Invalid input/gate settings.");}
    else if(version>=4)return Result::fail("Missing input/gate settings.");
    candidate.name = xml.getStringAttribute("name"); auto* amp = xml.getChildByName("Amp");
    if (!native&&(! amp || ! candidate.amp.fromBase64Encoding(amp->getAllSubText()) || candidate.amp.isEmpty()
        || candidate.amp.getSize() > 45*1024*1024 || (int)candidate.amp.getSize() != amp->getIntAttribute("bytes")
        || SHA256(candidate.amp).toHexString() != amp->getStringAttribute("sha256"))) return Result::fail(generic?"Missing or corrupt VST3 state.":"Missing or corrupt TONEX state.");
    if(generic&&vst3::hasEnvelopePrefix(candidate.amp))
    {vst3::State parsed;if(!vst3::decodeState(candidate.amp,parsed))return Result::fail("Invalid extended VST3 state.");}
    std::array<bool,count> seen {};
    for (auto* item : xml.getChildIterator())
    {
        if (! item->hasTagName("Pedal")) continue;
        const int p = item->getIntAttribute("id", -1); if (p < 0 || p >= migrations::pedalCount(version) || seen[(size_t)p]) return Result::fail("Invalid pedal list.");
        auto& s = candidate.pedals[(size_t)p]; s.preset = item->getIntAttribute("preset", -1);
        s.enabled = item->getBoolAttribute("enabled"); s.input = (float)item->getDoubleAttribute("input", 999);
        s.mix = isNam(p)?1.f:(float)item->getDoubleAttribute("mix", 999); s.output = (float)item->getDoubleAttribute("output", 999);
        s.tone = isNam(p)&&version>=5?(float)item->getDoubleAttribute("tone",999):0.f;
        if (auto migrated = migrations::legacyNam(*item, version, p, s); migrated.failed()) return migrated;
        s.ratio=(float)item->getDoubleAttribute("ratio",migrations::defaultRatio(p,s.preset));
        if(p==id(PedalId::AutoWah)){s.wahSensitivity=(float)item->getDoubleAttribute("wahSensitivity",999);s.wahRange=(float)item->getDoubleAttribute("wahRange",999);s.wahDecay=(float)item->getDoubleAttribute("wahDecay",999);}
        if(p==id(PedalId::GraphicEq))for(int b=0;b<7;++b)s.eqBands[(size_t)b]=(float)item->getDoubleAttribute("eq"+String(b),999);
        auto model=item->getStringAttribute("model");
        if(p==id(PedalId::OrangeDist))
        {
            const int migrated=migrations::orangePreset(model);
            if(migrated>=0){s.preset=migrated;if(migrations::withdrawnOrange(model)){model=identity(p,migrated);candidate.missingCaptureDiagnostics.add("Withdrawn DS-1 capture replaced with the approved Wampler JCM Mod capture.");}}
            else if(version>=15&&(s.preset==9||item->getBoolAttribute("personalCapture")))s.preset=presetCount(p);
        }
        // Restore factory and personal identities across Blues library revisions;
        // numerical slots alone never authorize a change of capture.
        if(p==id(PedalId::BluesOd))
        {
            if(s.preset==14&&model=="c4fb6b032d6149e16e555860cdd715651fefec59f917519d5fe6a2bd1a680836"&&!item->getBoolAttribute("personalCapture"))
            {
                s.preset=recommendedPreset(p);s.enabled=false;
                candidate.missingCaptureDiagnostics.add("Waza Custom was withdrawn from Blues OD. Pedal bypassed; select a current capture to enable it.");
                if(!valid(s,p))return Result::fail("Invalid withdrawn Blues OD settings.");
                seen[size_t(p)]=true;continue;
            }
            const bool personal=item->getBoolAttribute("personalCapture");
            if(version>=15&&(personal||((s.preset==14||s.preset==15)&&model!=identity(p,s.preset))))s.preset=presetCount(p);
            else if(!personal)
            {
                const int replacement=migrations::bluesPreset(model);
                if(replacement>=0){s.preset=replacement;model=identity(p,replacement);}
            }
        }
        const bool legacyWah=migrations::isLegacyWah(p,model);
        if(!valid(s,p))return Result::fail("Invalid settings or changed capture for "+pedalName(p));
        if(isNam(p)&&s.preset==presetCount(p))
        {
            if(version<15||model.length()!=64||!model.containsOnly("0123456789abcdef"))return Result::fail("Invalid personal capture identity.");
            candidate.customPedalHashes[size_t(p)]=model;seen[size_t(p)]=true;continue;
        }
        const auto installed=identity(p,s.preset);
        if(isNam(p)&&installed.isEmpty())
        {s.enabled=false;candidate.missingCaptureDiagnostics.add("Missing capture for "+pedalName(p)+" (preset "+String(s.preset)+", model "+model+"). Pedal bypassed; remaining rig restored.");}
        else if(!legacyWah&&model!=installed)return Result::fail("Invalid settings or changed capture for "+pedalName(p));
        seen[(size_t)p] = true;
    }
    if (std::find(seen.begin(),seen.begin()+migrations::pedalCount(version),false) != seen.begin()+migrations::pedalCount(version)) return Result::fail("Incomplete pedalboard.");
    result = std::move(candidate); return Result::ok();
}
}
