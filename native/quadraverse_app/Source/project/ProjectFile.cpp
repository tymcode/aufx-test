#include "ProjectFile.h"

namespace qverse
{

bool ProjectFile::save (const juce::File& file, const ProjectState& state, juce::String& error)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("format", "quadraverse.project");
    obj->setProperty ("version", 1);
    obj->setProperty ("patchSaveDirectory", state.patchSaveDirectory.getFullPathName());
    obj->setProperty ("hardwareMode", state.hardwareMode);
    obj->setProperty ("windowState", state.windowState);
    obj->setProperty ("contexts", state.contexts.toVar());
    obj->setProperty ("randomizationSettings", state.randomizationSettings);

    const auto text = juce::JSON::toString (juce::var (obj), true);
    if (! file.replaceWithText (text))
    {
        error = "Could not write project file";
        return false;
    }
    return true;
}

bool ProjectFile::load (const juce::File& file, ProjectState& state, juce::String& error)
{
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr || obj->getProperty ("format").toString() != "quadraverse.project")
    {
        error = "Not a Quadraverse project";
        return false;
    }

    state.patchSaveDirectory = juce::File (obj->getProperty ("patchSaveDirectory").toString());
    state.hardwareMode = (bool) obj->getProperty ("hardwareMode");
    state.windowState = obj->getProperty ("windowState").toString();
    state.randomizationSettings = obj->getProperty ("randomizationSettings");
    state.contexts.fromVar (obj->getProperty ("contexts"));
    return true;
}

} // namespace qverse
