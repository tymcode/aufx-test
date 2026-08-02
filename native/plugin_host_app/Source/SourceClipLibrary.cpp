#include "SourceClipLibrary.h"
#include "HostFileUtils.h"
#include "Utf8.h"

bool SourceClipLibrary::isSupportedAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    return file.hasFileExtension ("wav")
        || file.hasFileExtension ("aif")
        || file.hasFileExtension ("aiff")
        || file.hasFileExtension ("flac");
}

bool SourceClipLibrary::isWavFile (const juce::File& file)
{
    return file.existsAsFile() && file.hasFileExtension ("wav");
}

void SourceClipLibrary::addLibraryFile (const juce::File& file,
                                        const juce::String& groupName,
                                        const juce::String& menuLabel)
{
    SourceClipEntry entry;
    entry.file = file;
    entry.groupName = groupName;
    entry.menuLabel = menuLabel;
    entry.fromLibrary = true;
    clips.add (std::move (entry));
}

int SourceClipLibrary::addExternalEntry (const juce::File& file)
{
    const int existing = indexOfFile (file);
    if (existing >= 0)
        return existing + 1;

    SourceClipEntry entry;
    entry.file = file;
    entry.groupName = externalGroupName;
    entry.menuLabel = file.getFileNameWithoutExtension();
    entry.fromLibrary = false;
    clips.add (std::move (entry));
    return clips.size();
}

int SourceClipLibrary::addTemporaryTopLevelEntry (const juce::File& file)
{
    const int existing = indexOfFile (file);
    if (existing >= 0)
        return existing + 1;

    SourceClipEntry entry;
    entry.file = file;
    entry.groupName = {};
    entry.menuLabel = file.getFileNameWithoutExtension();
    entry.fromLibrary = false;
    clips.add (std::move (entry));
    return clips.size();
}

void SourceClipLibrary::rescan (const juce::File& fixturesDir)
{
    juce::Array<SourceClipEntry> preservedExternals;
    for (const auto& clip : clips)
        if (! clip.fromLibrary)
            preservedExternals.add (clip);

    clips.clearQuick();
    libraryRoot = fixturesDir;

    if (fixturesDir.isDirectory())
    {
        for (const auto& file : HostFileUtils::collectFiles (fixturesDir, ".wav", false))
            addLibraryFile (file, {}, file.getFileNameWithoutExtension());

        auto folders = fixturesDir.findChildFiles (juce::File::findDirectories, false);
        HostFileUtils::sortFilesByName (folders);

        for (const auto& folder : folders)
        {
            const auto folderClips = HostFileUtils::collectFiles (folder, ".wav", true);
            if (folderClips.isEmpty())
                continue;

            for (const auto& file : folderClips)
            {
                auto display = file.getRelativePathFrom (folder);
                if (display.endsWithIgnoreCase (".wav"))
                    display = display.dropLastCharacters (4);
                addLibraryFile (file, folder.getFileName(), display);
            }
        }
    }

    for (const auto& external : preservedExternals)
    {
        if (indexOfFile (external.file) < 0)
            clips.add (external);
    }
}

void SourceClipLibrary::rebuildComboBox (juce::ComboBox& box, int preferredSelectId) const
{
    box.clear (juce::dontSendNotification);

    int impulseId = 0;
    juce::StringArray groupOrder;
    juce::StringArray seenGroups;

    for (int i = 0; i < clips.size(); ++i)
    {
        const auto& clip = clips.getReference (i);
        const int id = i + 1;

        if (clip.groupName.isEmpty())
        {
            box.addItem (clip.menuLabel, id);
            if (clip.file.getFileName().equalsIgnoreCase ("impulse.wav"))
                impulseId = id;
            continue;
        }

        if (! seenGroups.contains (clip.groupName))
        {
            seenGroups.add (clip.groupName);
            groupOrder.add (clip.groupName);
        }
    }

    for (const auto& group : groupOrder)
    {
        juce::PopupMenu subMenu;
        for (int i = 0; i < clips.size(); ++i)
        {
            const auto& clip = clips.getReference (i);
            if (clip.groupName == group)
                subMenu.addItem (i + 1, clip.menuLabel);
        }
        box.getRootMenu()->addSubMenu (group, subMenu);
    }

    box.addItem (utf8 ("Select Other…"), selectOtherItemId);

    int selectId = preferredSelectId;
    if (selectId == selectOtherItemId || selectId <= 0 || selectId > clips.size())
        selectId = impulseId > 0 ? impulseId : (clips.isEmpty() ? 0 : 1);

    if (selectId > 0)
        box.setSelectedId (selectId, juce::dontSendNotification);
}

juce::File SourceClipLibrary::getFileForId (int comboId) const
{
    const int index = comboId - 1;
    if (! juce::isPositiveAndBelow (index, clips.size()))
        return {};
    return clips.getReference (index).file;
}

juce::File SourceClipLibrary::getSelectedFile (const juce::ComboBox& box) const
{
    return getFileForId (box.getSelectedId());
}

int SourceClipLibrary::indexOfFile (const juce::File& file) const
{
    const auto path = file.getFullPathName();
    for (int i = 0; i < clips.size(); ++i)
        if (clips.getReference (i).file.getFullPathName() == path
            || clips.getReference (i).file == file)
            return i;
    return -1;
}

int SourceClipLibrary::selectOrAddExternal (juce::ComboBox& box, const juce::File& file)
{
    if (! isSupportedAudioFile (file))
        return 0;

    const int previousId = box.getSelectedId();
    const int id = addExternalEntry (file);
    rebuildComboBox (box, id > 0 ? id : previousId);
    if (id > 0)
        box.setSelectedId (id, juce::dontSendNotification);
    return id;
}

int SourceClipLibrary::selectOrAddTemporaryTopLevel (juce::ComboBox& box, const juce::File& file)
{
    if (! isWavFile (file))
        return 0;

    const int previousId = box.getSelectedId();
    const int id = addTemporaryTopLevelEntry (file);
    rebuildComboBox (box, id > 0 ? id : previousId);
    if (id > 0)
        box.setSelectedId (id, juce::dontSendNotification);
    return id;
}
