#pragma once
#include <juce_core/juce_core.h>
#include <ToneGridBuildInfo.h>
namespace tonegrid::presets {
inline bool knownNode(const juce::String& tag) {
    return tag == "Input" || tag == "Pedal" || tag == "AmpSource" || tag == "Amp"
        || tag == "PLUGIN" || tag == "CabinetIR" || tag == "OutputDSP" || tag == "BassRack";
}
inline bool knownAttribute(const juce::String& name) {
    return name == "version" || name == "name" || name == "presetId";
}
// Preserve additive descriptive data without retaining copies of plugin/audio state.
inline juce::String captureExtensions(const juce::XmlElement& source) {
    juce::XmlElement extra("PresetExtensions");
    for (int i = 0; i < source.getNumAttributes(); ++i) {
        const auto name = source.getAttributeName(i);
        if (!knownAttribute(name)) extra.setAttribute(name, source.getAttributeValue(i));
    }
    for (auto* child : source.getChildIterator())
        if (!knownNode(child->getTagName()) && !child->isTextElement()) extra.addChildElement(new juce::XmlElement(*child));
    return extra.toString();
}
inline void restoreExtensions(juce::XmlElement& target, const juce::String& saved) {
    if (auto extra = juce::XmlDocument::parse(saved)) {
        for (int i = 0; i < extra->getNumAttributes(); ++i) {
            const auto name = extra->getAttributeName(i);
            if (!knownAttribute(name)) target.setAttribute(name, extra->getAttributeValue(i));
        }
        for (auto* child : extra->getChildIterator())
            if (!knownNode(child->getTagName()) && !child->isTextElement()) target.addChildElement(new juce::XmlElement(*child));
    }
}
inline juce::Result compatibility(const juce::XmlElement& xml) {
    const auto minimum = xml.getStringAttribute("minEngineVersion");
    if (minimum.isNotEmpty()) {
        const auto requested = juce::StringArray::fromTokens(minimum, ".", {});
        const auto installed = juce::StringArray::fromTokens(tonegrid::build::version, ".", {});
        if (requested.size() != 3 || installed.size() != 3) return juce::Result::fail("Invalid preset minimum engine version.");
        for (int i = 0; i < 3; ++i)
            if (requested[i].isEmpty() || requested[i].length() > 6 || !requested[i].containsOnly("0123456789"))
                return juce::Result::fail("Invalid preset minimum engine version.");
        for (int i = 0; i < 3; ++i) {
            if (requested[i].getIntValue() > installed[i].getIntValue()) return juce::Result::fail("This preset needs ToneGrid " + minimum + " or newer.");
            if (requested[i].getIntValue() < installed[i].getIntValue()) break;
        }
    }
    const auto required = juce::StringArray::fromTokens(xml.getStringAttribute("requiredFeatures"), ",", {});
    for (auto name : required) {
        name = name.trim();
        if (name.isNotEmpty() && name != "nam" && name != "cabinet-ir" && name != "postfx-v1" && name != "cabinet-bypass" && name != "bass-rack")
            return juce::Result::fail("This preset needs an unsupported feature: " + name);
    }
    return juce::Result::ok();
}
}
