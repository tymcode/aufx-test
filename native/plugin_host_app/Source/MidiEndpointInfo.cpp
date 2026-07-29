#include "MidiEndpointInfo.h"

#if JUCE_MAC
 #include "MidiEndpointInfo_mac.h"
#endif

namespace
{
    void enrich (MidiEndpointInfo& info)
    {
#if JUCE_MAC
        char manufacturer[256] = {};
        char model[256] = {};
        if (midiEndpointLookupMeta (info.name.toRawUTF8(),
                                    manufacturer, sizeof (manufacturer),
                                    model, sizeof (model)))
        {
            if (manufacturer[0] != '\0')
                info.manufacturer = juce::String::fromUTF8 (manufacturer);
            if (model[0] != '\0')
                info.model = juce::String::fromUTF8 (model);
        }
#else
        juce::ignoreUnused (info);
#endif
    }
}

juce::Array<MidiEndpointInfo> getMidiInputEndpointInfos()
{
    juce::Array<MidiEndpointInfo> results;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        MidiEndpointInfo info;
        info.identifier = device.identifier;
        info.name = device.name;
        info.isInput = true;
        enrich (info);
        results.add (info);
    }
    return results;
}

juce::Array<MidiEndpointInfo> getMidiOutputEndpointInfos()
{
    juce::Array<MidiEndpointInfo> results;
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
    {
        MidiEndpointInfo info;
        info.identifier = device.identifier;
        info.name = device.name;
        info.isOutput = true;
        enrich (info);
        results.add (info);
    }
    return results;
}

MidiEndpointInfo findMidiEndpointInfo (const juce::String& identifier, bool preferOutput)
{
    const auto list = preferOutput ? getMidiOutputEndpointInfos() : getMidiInputEndpointInfos();
    for (const auto& info : list)
        if (info.identifier == identifier)
            return info;

    const auto other = preferOutput ? getMidiInputEndpointInfos() : getMidiOutputEndpointInfos();
    for (const auto& info : other)
        if (info.identifier == identifier)
            return info;

    return {};
}
