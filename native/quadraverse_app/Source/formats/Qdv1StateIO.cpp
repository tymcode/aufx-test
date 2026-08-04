#include "Utf8.h"
#include "Qdv1StateIO.h"
#include "AUpresetLoader.h"
#include "AUpresetSaver.h"
#include "../domain/DeviceProfile.h"

namespace qverse
{

bool Qdv1StateIO::toStateBlob (const QuadraverbProgram& program, juce::MemoryBlock& out, juce::String& error)
{
    juce::XmlElement xml ("QDV1");
    xml.setAttribute ("program", program.programNumber >= 0 ? program.programNumber : 0);
    xml.setAttribute ("config", program.config);
    xml.setAttribute ("inLevel", program.inLevel);
    xml.setAttribute ("outLevel", program.outLevel);

    for (int f = 0; f < QuadraverbProgram::kMaxFunctions; ++f)
        for (int p = 0; p < QuadraverbProgram::kMaxPages; ++p)
            if (program.isKnown (f, p) && ! (f == 7 && p == 0))
            {
                auto* e = xml.createNewChildElement ("P");
                e->setAttribute ("f", f);
                e->setAttribute ("p", p);
                e->setAttribute ("v", program.getParam (f, p));
            }

    // Intentionally omit MIDI_MAP so QDV-1 keeps existing mappings.
    juce::AudioProcessor::copyXmlToBinary (xml, out);
    juce::ignoreUnused (error);
    return true;
}

bool Qdv1StateIO::fromStateBlob (const juce::MemoryBlock& blob, QuadraverbProgram& out, juce::String& error)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    if (xml == nullptr || ! xml->hasTagName ("QDV1"))
    {
        error = utf8 ("Not a QDV1 state blob");
        return false;
    }

    out = {};
    out.model = DeviceModel::quadraverbPlus;
    out.programNumber = xml->getIntAttribute ("program", 0);
    out.config = xml->getIntAttribute ("config", 4);
    out.inLevel = xml->getIntAttribute ("inLevel", 50);
    out.outLevel = xml->getIntAttribute ("outLevel", 84);
    out.setParam (7, 0, out.config);

    for (auto* e : xml->getChildIterator())
        if (e->hasTagName ("P"))
            out.setParam (e->getIntAttribute ("f"),
                          e->getIntAttribute ("p"),
                          e->getIntAttribute ("v"));

    out.flushValuesToBytes();
    return true;
}

bool Qdv1StateIO::loadAupreset (const juce::File& file, QuadraverbProgram& out, juce::String& error)
{
    juce::MemoryBlock state;
    if (! AUpresetLoader::loadStateBytes (file, state, error))
        return false;
    return fromStateBlob (state, out, error);
}

bool Qdv1StateIO::saveAupreset (const juce::File& file, const QuadraverbProgram& program, juce::String& error)
{
    juce::MemoryBlock state;
    if (! toStateBlob (program, state, error))
        return false;
    return AUpresetSaver::saveStateBytes (file, state, error);
}

juce::File Qdv1StateIO::presetLibraryDir()
{
    if (const auto* env = getenv ("QDV1_PRESET_DIR"))
        if (juce::String (env).isNotEmpty())
            return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("TemeculaDSP")
        .getChildFile ("QDV1");
}

bool Qdv1StateIO::saveUserPreset (const juce::String& name,
                                  const QuadraverbProgram& program,
                                  juce::String& outId,
                                  juce::String& error)
{
    juce::MemoryBlock state;
    if (! toStateBlob (program, state, error))
        return false;

    outId = juce::Uuid().toDashedString();
    auto dir = presetLibraryDir().getChildFile ("presets");
    if (! dir.createDirectory() && ! dir.isDirectory())
    {
        error = utf8 ("Could not create QDV1 presets directory");
        return false;
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("id", outId);
    obj->setProperty ("name", name);
    obj->setProperty ("tags", juce::Array<juce::var>());
    obj->setProperty ("state", juce::Base64::toBase64 (state.getData(), state.getSize()));

    const auto json = juce::JSON::toString (juce::var (obj), true);
    const auto file = dir.getChildFile (outId + ".json");
    if (! file.replaceWithText (json))
    {
        error = utf8 ("Could not write QDV1 user preset");
        return false;
    }
    return true;
}

juce::Array<juce::File> Qdv1StateIO::listUserPresetFiles()
{
    return presetLibraryDir().getChildFile ("presets").findChildFiles (juce::File::findFiles, false, "*.json");
}

bool Qdv1StateIO::loadUserPresetFile (const juce::File& jsonFile, QuadraverbProgram& out, juce::String& error)
{
    const auto text = jsonFile.loadFileAsString();
    const auto parsed = juce::JSON::parse (text);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
    {
        error = utf8 ("Invalid QDV1 preset JSON");
        return false;
    }
    const auto b64 = obj->getProperty ("state").toString();
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64))
    {
        error = utf8 ("Invalid base64 state in preset");
        return false;
    }
    juce::MemoryBlock blob (mo.getData(), mo.getDataSize());
    if (! fromStateBlob (blob, out, error))
        return false;
    const auto n = obj->getProperty ("name").toString();
    if (n.isNotEmpty())
        out.setName (n);
    return true;
}

} // namespace qverse
