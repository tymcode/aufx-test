#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"

namespace HostFileUtils
{
    enum class SortMode
    {
        ignoreCase,
        natural
    };

    inline void sortFilesByName (juce::Array<juce::File>& files, SortMode mode = SortMode::ignoreCase)
    {
        struct Comparator
        {
            SortMode mode;

            int compareElements (const juce::File& a, const juce::File& b) const
            {
                if (mode == SortMode::natural)
                    return a.getFileName().compareNatural (b.getFileName());
                return a.getFileName().compareIgnoreCase (b.getFileName());
            }
        };

        Comparator comparator { mode };
        files.sort (comparator);
    }

    inline juce::Array<juce::File> collectFiles (const juce::File& root,
                                                 const juce::String& extension,
                                                 bool recursive,
                                                 SortMode mode = SortMode::ignoreCase)
    {
        juce::Array<juce::File> results;
        juce::Array<juce::File> stack;
        stack.add (root);

        while (! stack.isEmpty())
        {
            const auto dir = stack.removeAndReturn (0);
            for (const auto& entry : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
            {
                if (entry.isDirectory())
                {
                    if (recursive)
                        stack.add (entry);
                }
                else if (entry.hasFileExtension (extension))
                {
                    results.add (entry);
                }
            }
        }

        sortFilesByName (results, mode);
        return results;
    }

    inline juce::String keywordFromDescription (const juce::String& description)
    {
        const auto slug = HostConfig::slugify (description);
        if (slug.isEmpty())
            return {};

        return slug.upToFirstOccurrenceOf ("_", false, false);
    }
}
