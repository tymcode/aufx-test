#pragma once

#include <JuceHeader.h>
#include "HostPreferences.h"

/** Quadraverse-specific preference keys stored alongside HostPreferences. */
class QuadraversePrefs
{
public:
    static constexpr const char* keyEditLiveToDevice = "qvEditLiveToDevice";
    static constexpr const char* keyPatchSaveDirectory = "qvPatchSaveDirectory";
    static constexpr const char* keySaveQdv1Default = "qvSaveQdv1Default";
    static constexpr const char* keySaveSyxDefault = "qvSaveSyxDefault";
    static constexpr const char* keySaveAupresetDefault = "qvSaveAupresetDefault";
    static constexpr const char* keyRecentProjects = "qvRecentProjects";
    static constexpr const char* keyEmitIndividualSyxFromSsx = "qvEmitIndividualSyxFromSsx";

    static bool getEditLiveToDevice()
    {
        if (auto* s = HostPreferences::get().settings())
            return s->getBoolValue (keyEditLiveToDevice, true);
        return true;
    }

    static void setEditLiveToDevice (bool v)
    {
        if (auto* s = HostPreferences::get().settings())
            s->setValue (keyEditLiveToDevice, v);
    }

    static juce::File getPatchSaveDirectory()
    {
        if (auto* s = HostPreferences::get().settings())
        {
            const auto p = s->getValue (keyPatchSaveDirectory);
            if (p.isNotEmpty())
                return juce::File (p);
        }
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Quadraverse")
            .getChildFile ("Patches");
    }

    static void setPatchSaveDirectory (const juce::File& dir)
    {
        if (auto* s = HostPreferences::get().settings())
            s->setValue (keyPatchSaveDirectory, dir.getFullPathName());
    }

    static bool getSaveQdv1Default()
    {
        if (auto* s = HostPreferences::get().settings())
            return s->getBoolValue (keySaveQdv1Default, true);
        return true;
    }
    static void setSaveQdv1Default (bool v)
    {
        if (auto* s = HostPreferences::get().settings())
            s->setValue (keySaveQdv1Default, v);
    }

    static bool getSaveSyxDefault()
    {
        if (auto* s = HostPreferences::get().settings())
            return s->getBoolValue (keySaveSyxDefault, true);
        return true;
    }
    static void setSaveSyxDefault (bool v)
    {
        if (auto* s = HostPreferences::get().settings())
            s->setValue (keySaveSyxDefault, v);
    }

    static bool getSaveAupresetDefault()
    {
        if (auto* s = HostPreferences::get().settings())
            return s->getBoolValue (keySaveAupresetDefault, false);
        return false;
    }
    static void setSaveAupresetDefault (bool v)
    {
        if (auto* s = HostPreferences::get().settings())
            s->setValue (keySaveAupresetDefault, v);
    }

    static void addRecentProject (const juce::File& file)
    {
        auto* s = HostPreferences::get().settings();
        if (s == nullptr)
            return;
        auto list = juce::StringArray::fromLines (s->getValue (keyRecentProjects));
        list.removeString (file.getFullPathName());
        list.insert (0, file.getFullPathName());
        while (list.size() > 10)
            list.remove (list.size() - 1);
        s->setValue (keyRecentProjects, list.joinIntoString ("\n"));
    }

    static juce::StringArray getRecentProjects()
    {
        if (auto* s = HostPreferences::get().settings())
            return juce::StringArray::fromLines (s->getValue (keyRecentProjects));
        return {};
    }
};
