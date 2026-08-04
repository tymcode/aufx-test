#include "SendPluginSettingsToDevice.h"
#include "AUpresetLoader.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "PluginAudioEngine.h"
#include "Utf8.h"

#include "AlesisCodec.h"
#include "Qdv1StateIO.h"
#include "QuadraverbProgram.h"

juce::String SendPluginSettingsToDevice::resolveTargetDeviceName()
{
    const auto outId = HostPreferences::get().getMidiOutIdentifier();
    if (outId.isEmpty())
        return {};

    const auto info = findMidiEndpointInfo (outId, true);
    if (info.name.isNotEmpty())
        return info.name;

    return {};
}

bool SendPluginSettingsToDevice::send (PluginAudioEngine& engine, juce::String& errorOrStatus)
{
    auto* plugin = engine.getPlugin();
    if (plugin == nullptr)
    {
        errorOrStatus = utf8 ("No plugin loaded");
        return false;
    }

    juce::MemoryBlock hostState;
    plugin->getStateInformation (hostState);
    if (hostState.isEmpty())
    {
        errorOrStatus = utf8 ("Plugin returned empty state");
        return false;
    }

    juce::MemoryBlock state;
    juce::String unwrapError;
    if (! AUpresetLoader::extractStateBytes (hostState, state, unwrapError))
    {
        errorOrStatus = utf8 ("Plugin state is not QDV-1 compatible: ") + unwrapError;
        return false;
    }

    qverse::QuadraverbProgram program;
    juce::String parseError;
    if (! qverse::Qdv1StateIO::fromStateBlob (state, program, parseError))
    {
        errorOrStatus = utf8 ("Plugin state is not QDV-1 compatible: ") + parseError;
        return false;
    }

    program.flushValuesToBytes();
    if (! program.hasValidBytes)
    {
        errorOrStatus = utf8 ("Could not build Quadraverb program dump from plugin state");
        return false;
    }

    const auto msg = qverse::AlesisCodec::buildLoadProgram (
        qverse::AlesisCodec::kProductQuadraverb,
        qverse::AlesisCodec::kEditBuffer,
        program.bytes.data());

    juce::Array<juce::MidiMessage> messages;
    messages.add (msg);

    if (! engine.sendMidiMessages (messages))
    {
        errorOrStatus = utf8 ("MIDI output not configured — open MIDI Setup");
        return false;
    }

    const auto deviceName = resolveTargetDeviceName();
    errorOrStatus = utf8 ("Sent plugin settings to ")
        + (deviceName.isNotEmpty() ? deviceName : utf8 ("MIDI out"));
    return true;
}
