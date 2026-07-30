#pragma once

#include <JuceHeader.h>

/**
 * Shared session artifact naming — kept in sync with src/aufx_test/session.py.
 * Role codes: gld = golden, sus = suspect, bkn = broken.
 * Filenames: <stem>_output_<role>.wav  and  <stem>_output_hw_<role>.wav
 */
namespace SessionArtifactSchema
{
    inline constexpr const char* roleGolden  = "gld";
    inline constexpr const char* roleSuspect = "sus";
    inline constexpr const char* roleBroken  = "bkn";
    inline constexpr const char* aupresetExtension = ".aupreset";

    inline juce::String roleCodeFromIndex (int roleIndex)
    {
        switch (roleIndex)
        {
            case 0:  return roleGolden;
            case 1:  return roleSuspect;
            default: return roleBroken;
        }
    }

    inline juce::String softwareOutputSuffix (const juce::String& role)
    {
        return "_output_" + role;
    }

    inline juce::String hardwareOutputSuffix (const juce::String& role)
    {
        return "_output_hw_" + role;
    }

    inline juce::String parseOutputRole (const juce::File& outputFile)
    {
        const auto stem = outputFile.getFileNameWithoutExtension();
        for (const auto* role : { roleGolden, roleSuspect, roleBroken })
        {
            if (stem.endsWith (hardwareOutputSuffix (role)))
                return role;
            if (stem.endsWith (softwareOutputSuffix (role)))
                return role;
        }
        return {};
    }

    inline juce::String baseStemFromOutput (const juce::File& outputFile)
    {
        auto stem = outputFile.getFileNameWithoutExtension();
        for (const auto* role : { roleGolden, roleSuspect, roleBroken })
        {
            const auto hwSuffix = hardwareOutputSuffix (role);
            if (stem.endsWith (hwSuffix))
                return stem.dropLastCharacters (hwSuffix.length());

            const auto suffix = softwareOutputSuffix (role);
            if (stem.endsWith (suffix))
                return stem.dropLastCharacters (suffix.length());
        }
        if (stem.endsWith ("_output"))
            return stem.dropLastCharacters (7);
        return stem;
    }

    /** Only golden captures are expected to match in regression runs. */
    inline bool expectMatchForRole (const juce::String& role)
    {
        return role != roleSuspect && role != roleBroken;
    }

    inline juce::String stripAupresetExtension (juce::String name)
    {
        if (name.endsWithIgnoreCase (aupresetExtension))
            return name.dropLastCharacters (9);
        return name;
    }

    inline juce::String ensureAupresetExtension (juce::String fileName)
    {
        if (! fileName.endsWithIgnoreCase (aupresetExtension))
            fileName << aupresetExtension;
        return fileName;
    }
}
