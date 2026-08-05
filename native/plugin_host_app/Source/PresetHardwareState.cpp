#include "PresetHardwareState.h"
#include "HostFileUtils.h"
#include "HostLog.h"
#include "HostPreferences.h"
#include "SessionArtifactSchema.h"
#include "Utf8.h"
#include "sysex/SysexDeviceRegistry.h"

const SysexDeviceModule* resolveSelectedSysexModule (const MidiEndpointInfo& info)
{
    const auto selectedName = HostPreferences::get().getMidiSysexModule().trim();
    const auto& modules = SysexDeviceRegistry::get().getModules();
    if (selectedName.isNotEmpty())
    {
        for (const auto& module : modules)
            if (module != nullptr && module->getDisplayName() == selectedName)
                return module.get();
    }

    return SysexDeviceRegistry::get().findModule (info.manufacturer, info.model, info.name);
}

PresetHardwareState::PresetHardwareState (PluginAudioEngine& audioEngine,
                                          HostConfig& hostConfig,
                                          juce::Label& presetLabelIn,
                                          juce::ComboBox& presetBoxIn,
                                          juce::TextButton& loadPresetButtonIn,
                                          juce::TextButton& savePresetButtonIn,
                                          juce::TextEditor& savePresetNameEditorIn,
                                          juce::Label& savePresetNameLabelIn,
                                          juce::TextButton& bypassButtonIn,
                                          StatusFn statusFn,
                                          GetPluginIndexFn getPluginIndex,
                                          GetPluginEntryFn getPluginEntry,
                                          GetPluginEditorFn getPluginEditorIn,
                                          VoidFn onShowHardwareMetersIn,
                                          VoidFn onShowPluginEditorAreaIn,
                                          juce::Component* dialogParentIn)
    : engine (audioEngine),
      config (hostConfig),
      presetLabel (presetLabelIn),
      presetBox (presetBoxIn),
      loadPresetButton (loadPresetButtonIn),
      savePresetButton (savePresetButtonIn),
      savePresetNameEditor (savePresetNameEditorIn),
      savePresetNameLabel (savePresetNameLabelIn),
      bypassButton (bypassButtonIn),
      setStatus (std::move (statusFn)),
      getCurrentPluginIndex (std::move (getPluginIndex)),
      getCurrentPlugin (std::move (getPluginEntry)),
      getPluginEditor (std::move (getPluginEditorIn)),
      onShowHardwareMeters (std::move (onShowHardwareMetersIn)),
      onShowPluginEditorArea (std::move (onShowPluginEditorAreaIn)),
      dialogParent (dialogParentIn)
{
}

void PresetHardwareState::refreshHardwareModeUi()
{
    // Hardware and remote transports share the external-monitor UI: the
    // editor area becomes the loop meter panel and presets become HW states
    // (state sends reach remote gear via the network MIDI transport).
    const bool external = engine.isExternalMode();

    if (external)
    {
        presetLabel.setText ("HW State", juce::dontSendNotification);
        loadPresetButton.setButtonText ("Send");
        populateHardwareStates();
        onShowHardwareMeters();
    }
    else
    {
        presetLabel.setText ("Preset", juce::dontSendNotification);
        loadPresetButton.setButtonText ("Load");
        populatePresets();
        onShowPluginEditorArea();
    }

    savePresetButton.setEnabled (! external);
    savePresetNameEditor.setEnabled (! external);
    savePresetNameLabel.setEnabled (! external);
    bypassButton.setEnabled (! external);
}

void PresetHardwareState::mergeExtraFiles (juce::Array<juce::File>& files, const juce::Array<juce::File>& extras)
{
    for (const auto& file : extras)
        if (file.existsAsFile())
            files.addIfNotAlreadyThere (file);
}

juce::File PresetHardwareState::currentExplorationSessionDir() const
{
    if (! isValidPluginSelected())
        return config.sessionsRoot;

    if (config.sessionsRoot == juce::File())
        return {};

    return config.sessionsRoot.getChildFile (HostConfig::slugify (getCurrentPlugin().sessionName));
}

juce::File PresetHardwareState::fileChooserStartDir() const
{
    const auto sessionDir = currentExplorationSessionDir();
    if (sessionDir != juce::File())
    {
        const auto artifactsDir = sessionDir.getChildFile ("artifacts");
        if (artifactsDir.isDirectory())
            return artifactsDir;
        if (sessionDir.isDirectory())
        {
            // Prefer artifacts even on first capture — create if the session exists.
            artifactsDir.createDirectory();
            if (artifactsDir.isDirectory())
                return artifactsDir;
            return sessionDir;
        }
    }
    if (config.sessionsRoot.isDirectory())
        return config.sessionsRoot;
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

void PresetHardwareState::appendSelectOtherItem()
{
    presetBox.addItem (utf8 ("Select other…"), selectOtherItemId);
}

void PresetHardwareState::restoreSelectionAfterSelectOtherCancelled()
{
    if (lastNonOtherItemId > 0 && lastNonOtherItemId != selectOtherItemId
        && presetBox.indexOfItemId (lastNonOtherItemId) >= 0)
    {
        presetBox.setSelectedId (lastNonOtherItemId, juce::dontSendNotification);
        return;
    }

    for (int i = 0; i < presetBox.getNumItems(); ++i)
    {
        const int id = presetBox.getItemId (i);
        if (id != selectOtherItemId)
        {
            presetBox.setSelectedId (id, juce::dontSendNotification);
            lastNonOtherItemId = id;
            return;
        }
    }

    presetBox.setSelectedId (0, juce::dontSendNotification);
}

void PresetHardwareState::populateHardwareStates()
{
    presetBox.clear (juce::dontSendNotification);
    hardwareStateFiles.clear();

    if (! isValidPluginSelected())
        return;

    const auto sessionDir = currentExplorationSessionDir();
    const auto artifactsDir = sessionDir.getChildFile ("artifacts");
    if (artifactsDir.isDirectory())
    {
        for (const auto& file : artifactsDir.findChildFiles (juce::File::findFiles, false, "*.syx"))
            hardwareStateFiles.add (file);
    }

    if (config.sessionsRoot.isDirectory())
    {
        for (const auto& session : config.sessionsRoot.findChildFiles (juce::File::findDirectories, false))
        {
            const auto art = session.getChildFile ("artifacts");
            if (! art.isDirectory())
                continue;
            for (const auto& file : art.findChildFiles (juce::File::findFiles, false, "*.syx"))
                if (! hardwareStateFiles.contains (file))
                    hardwareStateFiles.add (file);
        }
    }

    mergeExtraFiles (hardwareStateFiles, extraHardwareStateFiles);
    HostFileUtils::sortFilesByName (hardwareStateFiles, HostFileUtils::SortMode::natural);

    for (int i = 0; i < hardwareStateFiles.size(); ++i)
        presetBox.addItem (hardwareStateFiles[i].getFileNameWithoutExtension(), i + 1);

    appendSelectOtherItem();

    if (hardwareStateFiles.size() > 0)
    {
        presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
        lastNonOtherItemId = presetBox.getSelectedId();
    }
}

void PresetHardwareState::sendSelectedHardwareState()
{
    const int index = presetBox.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (index, hardwareStateFiles.size()))
    {
        setStatus ("No hardware state selected", true);
        return;
    }

    juce::String error;
    if (! sendHardwareStateFile (hardwareStateFiles[index], error))
        setStatus (error, true);
}

bool PresetHardwareState::sendHardwareStateFile (const juce::File& sysexFile, juce::String& error)
{
    juce::MemoryBlock data;
    if (! sysexFile.loadFileAsData (data) || data.getSize() < 4)
    {
        error = "Failed to read " + sysexFile.getFileName();
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*> (data.getData());
    int offset = 0;
    int length = (int) data.getSize();
    if (bytes[0] == 0xf0)
    {
        offset = 1;
        length -= 1;
        if (length > 0 && bytes[data.getSize() - 1] == 0xf7)
            --length;
    }

    const auto message = juce::MidiMessage::createSysExMessage (bytes + offset, length);

    const auto outId = HostPreferences::get().getMidiOutIdentifier();
    const auto info = findMidiEndpointInfo (outId, true);
    const auto* module = resolveSelectedSysexModule (info);

    juce::Array<juce::MidiMessage> messages;
    if (module != nullptr)
        messages = module->restoreDump (message);
    else
        messages.add (message);

    if (! engine.sendMidiMessages (messages))
    {
        error = utf8 ("MIDI output not configured — open MIDI Setup");
        return false;
    }

    setStatus ("Sent " + sysexFile.getFileName() + " to hardware", false);
    return true;
}

void PresetHardwareState::populatePresets()
{
    presetBox.clear (juce::dontSendNotification);
    presetFiles.clearQuick();

    if (! isValidPluginSelected())
    {
        presetBox.addItem ("(no plugin)", 1);
        lastNonOtherItemId = 1;
        presetBox.setSelectedId (1, juce::dontSendNotification);
        return;
    }

    auto presetsDir = getCurrentPlugin().presetsDir;
    if (presetsDir != juce::File())
        presetsDir.createDirectory();

    if (presetsDir.isDirectory())
    {
        presetFiles = HostFileUtils::collectFiles (presetsDir, ".aupreset", true);
    }

    mergeExtraFiles (presetFiles, extraPresetFiles);

    for (int i = 0; i < presetFiles.size(); ++i)
    {
        const auto& file = presetFiles.getReference (i);
        // Select-other picks outside presets_dir: basename only (no path).
        const bool showBasenameOnly = ! presetsDir.isDirectory()
                                      || ! file.isAChildOf (presetsDir);
        const auto label = showBasenameOnly
                               ? stripAupresetExtension (file.getFileName())
                               : presetDisplayPath (file, presetsDir);
        presetBox.addItem (label, i + 1);
    }

    if (presetFiles.isEmpty())
    {
        if (! presetsDir.isDirectory())
            presetBox.addItem ("(no presets folder)", 1);
        else
            presetBox.addItem ("(no presets found)", 1);
    }

    appendSelectOtherItem();

    if (presetFiles.size() > 0)
    {
        presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
        lastNonOtherItemId = presetBox.getSelectedId();
    }
    else
    {
        lastNonOtherItemId = 1;
        presetBox.setSelectedId (1, juce::dontSendNotification);
    }
}

juce::String PresetHardwareState::stripAupresetExtension (juce::String name)
{
    return SessionArtifactSchema::stripAupresetExtension (std::move (name));
}

juce::String PresetHardwareState::presetDisplayPath (const juce::File& file, const juce::File& presetsDir)
{
    return stripAupresetExtension (file.getRelativePathFrom (presetsDir));
}

void PresetHardwareState::collectAupresetFiles (const juce::File& file, juce::Array<juce::File>& out)
{
    if (file.isDirectory())
    {
        for (const auto& child : HostFileUtils::collectFiles (file, ".aupreset", true))
            out.addIfNotAlreadyThere (child);
        return;
    }

    if (file.existsAsFile() && file.hasFileExtension (".aupreset"))
        out.addIfNotAlreadyThere (file);
}

bool PresetHardwareState::isValidPluginSelected() const
{
    return juce::isPositiveAndBelow (getCurrentPluginIndex(), config.plugins.size());
}

bool PresetHardwareState::canAcceptPresetDrag() const
{
    if (! isValidPluginSelected())
        return false;

    return getCurrentPlugin().presetsDir != juce::File();
}

void PresetHardwareState::importDroppedAupresets (const juce::StringArray& files)
{
    if (! isValidPluginSelected())
    {
        setStatus ("Load a plugin before importing presets", true);
        return;
    }

    auto presetsDir = getCurrentPlugin().presetsDir;
    if (presetsDir == juce::File())
    {
        setStatus ("This plugin has no presets folder", true);
        return;
    }

    presetsDir.createDirectory();
    if (! presetsDir.isDirectory())
    {
        setStatus ("Could not create presets folder: " + presetsDir.getFullPathName(), true);
        return;
    }

    juce::Array<juce::File> sources;
    for (const auto& path : files)
        collectAupresetFiles (juce::File (path), sources);

    if (sources.isEmpty())
    {
        setStatus ("No .aupreset files in drop", true);
        return;
    }

    enum class ConflictPolicy { ask, replaceAll, skipAll };
    auto policy = ConflictPolicy::ask;

    juce::Array<juce::File> imported;
    int skipped = 0;
    int copyFailures = 0;
    bool cancelled = false;

    for (int i = 0; i < sources.size(); ++i)
    {
        const auto& src = sources.getReference (i);
        const auto dest = presetsDir.getChildFile (src.getFileName());

        if (src.getFullPathName() == dest.getFullPathName())
        {
            imported.add (dest);
            continue;
        }

        if (dest.existsAsFile())
        {
            bool replace = false;

            if (policy == ConflictPolicy::replaceAll)
            {
                replace = true;
            }
            else if (policy == ConflictPolicy::skipAll)
            {
                ++skipped;
                continue;
            }
            else
            {
                const bool moreConflictsAhead = [&]()
                {
                    for (int j = i + 1; j < sources.size(); ++j)
                    {
                        const auto& later = sources.getReference (j);
                        const auto laterDest = presetsDir.getChildFile (later.getFileName());
                        if (later.getFullPathName() != laterDest.getFullPathName()
                            && laterDest.existsAsFile())
                            return true;
                    }
                    return false;
                }();

                juce::ToggleButton applyToAll ("Apply to all");
                applyToAll.setSize (280, 24);
                applyToAll.setVisible (moreConflictsAhead);

                juce::AlertWindow dialog (
                    "Preset already exists",
                    "\"" + dest.getFileName() + "\" already exists in the presets folder.",
                    juce::MessageBoxIconType::QuestionIcon,
                    dialogParent);

                if (moreConflictsAhead)
                    dialog.addCustomComponent (&applyToAll);

                dialog.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                dialog.addButton ("Skip", 2);
                dialog.addButton ("Replace", 1, juce::KeyPress (juce::KeyPress::returnKey));
                if (auto* cancel = dialog.getButton ("Cancel"))
                    cancel->setWantsKeyboardFocus (false);

                const int result = dialog.runModalLoop();
                if (result == 0)
                {
                    cancelled = true;
                    break;
                }

                replace = (result == 1);

                if (moreConflictsAhead && applyToAll.getToggleState())
                    policy = replace ? ConflictPolicy::replaceAll : ConflictPolicy::skipAll;

                if (! replace)
                {
                    ++skipped;
                    continue;
                }
            }

            if (! replace)
                continue;

            if (! dest.deleteFile())
            {
                ++copyFailures;
                HostLog::error ("Failed to replace preset " + dest.getFullPathName());
                continue;
            }
        }

        if (! src.copyFileTo (dest))
        {
            ++copyFailures;
            HostLog::error ("Failed to copy preset " + src.getFullPathName()
                            + " -> " + dest.getFullPathName());
            continue;
        }

        imported.add (dest);
    }

    if (imported.isEmpty())
    {
        if (cancelled)
            setStatus ("Import cancelled", false);
        else if (skipped > 0 && copyFailures == 0)
            setStatus ("Skipped " + juce::String (skipped) + " existing preset"
                       + (skipped == 1 ? "" : "s"),
                       false);
        else
            setStatus ("Failed to import preset(s)", true);
        return;
    }

    populatePresets();
    const auto& first = imported.getReference (0);
    selectPresetInDropdown (first);

    juce::String error;
    if (! engine.loadPreset (first, error))
    {
        setStatus ("Imported " + juce::String (imported.size())
                       + " preset(s) but load failed: " + error,
                   true);
        return;
    }

    if (auto* editor = getPluginEditor())
        editor->repaint();

    savePresetNameEditor.setText (first.getFileNameWithoutExtension(),
                                  juce::dontSendNotification);

    juce::String status = (cancelled ? "Import cancelled after copying " : "Imported ")
                          + juce::String (imported.size()) + " preset"
                          + (imported.size() == 1 ? "" : "s")
                          + "; loaded " + first.getFileNameWithoutExtension();
    if (skipped > 0)
        status += " (" + juce::String (skipped) + " skipped)";
    if (copyFailures > 0)
        status += " (" + juce::String (copyFailures) + " copy failed)";
    setStatus (status, false);
}

void PresetHardwareState::selectPresetInDropdown (const juce::File& presetFile)
{
    for (int i = 0; i < presetFiles.size(); ++i)
    {
        if (presetFiles[i] == presetFile
            || presetFiles[i].getFullPathName() == presetFile.getFullPathName())
        {
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
            lastNonOtherItemId = presetBox.getSelectedId();
            savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                          juce::dontSendNotification);
            return;
        }
    }
}

juce::File PresetHardwareState::getSelectedPresetFile() const
{
    const int index = presetBox.getSelectedItemIndex();
    if (index >= 0 && index < presetFiles.size())
        return presetFiles[index];

    const auto text = presetBox.getText().trim();
    if (text.isNotEmpty() && isValidPluginSelected())
    {
        for (const auto& file : presetFiles)
        {
            if (presetDisplayPath (file, getCurrentPlugin().presetsDir) == text
                || file.getFileName() == text
                || file.getFileNameWithoutExtension() == text)
            {
                return file;
            }
        }
    }

    return {};
}

void PresetHardwareState::loadSelectedPreset()
{
    const auto presetFile = getSelectedPresetFile();
    if (! presetFile.exists())
    {
        setStatus ("Select a preset to load", true);
        return;
    }

    juce::String error;
    if (! loadPresetFile (presetFile, error))
        setStatus (error, true);
}

bool PresetHardwareState::loadPresetFile (const juce::File& presetFile, juce::String& error)
{
    if (! presetFile.existsAsFile())
    {
        error = "Preset file not found: " + presetFile.getFullPathName();
        return false;
    }

    if (! engine.loadPreset (presetFile, error))
    {
        error = "Preset error: " + error;
        return false;
    }

    if (auto* editor = getPluginEditor())
        editor->repaint();

    // Snapshot / artifact presets may sit outside presets_dir — keep them in the
    // dropdown so the main window shows the restored basename.
    bool alreadyListed = false;
    for (const auto& listed : presetFiles)
    {
        if (listed == presetFile
            || listed.getFullPathName() == presetFile.getFullPathName())
        {
            alreadyListed = true;
            break;
        }
    }

    if (! alreadyListed)
    {
        extraPresetFiles.addIfNotAlreadyThere (presetFile);
        populatePresets();
    }

    selectPresetInDropdown (presetFile);
    savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                  juce::dontSendNotification);
    setStatus ("Loaded preset: " + presetFile.getFileNameWithoutExtension(), false);
    return true;
}

juce::File PresetHardwareState::presetFileForName (const juce::String& presetName) const
{
    auto fileName = juce::File::createLegalFileName (presetName.trim());
    if (fileName.isEmpty())
        fileName = "Untitled";

    if (! fileName.endsWithIgnoreCase (SessionArtifactSchema::aupresetExtension))
        fileName << SessionArtifactSchema::aupresetExtension;

    return getCurrentPlugin().presetsDir.getChildFile (fileName);
}

void PresetHardwareState::commitPresetSave (const juce::File& dest, bool replacing)
{
    juce::String error;
    if (! engine.saveCurrentPreset (dest, error))
    {
        setStatus ("Failed to save preset: " + error, true);
        return;
    }

    populatePresets();
    selectPresetInDropdown (dest);
    setStatus ((replacing ? "Replaced preset: " : "Saved preset: ")
               + dest.getFileNameWithoutExtension(),
               false);
}

void PresetHardwareState::savePresetFromEditor()
{
    if (engine.getPlugin() == nullptr)
    {
        setStatus ("No plugin loaded", true);
        return;
    }

    const auto presetName = savePresetNameEditor.getText().trim();
    if (presetName.isEmpty())
    {
        setStatus ("Enter a preset name before saving", true);
        return;
    }

    const auto dest = presetFileForName (presetName);
    if (dest.existsAsFile())
    {
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Replace Preset")
                           .withMessage ("File " + dest.getFileName()
                                         + " already exists.\nReplace it?")
                           .withButton ("OK")
                           .withButton ("Cancel")
                           .withAssociatedComponent (dialogParent);

        replacePresetDialog = juce::AlertWindow::showScopedAsync (options,
            [safeParent = juce::Component::SafePointer<juce::Component> (dialogParent), dest, this] (int result)
            {
                if (safeParent == nullptr || result != 1)
                    return;
                commitPresetSave (dest, true);
            });
        return;
    }

    commitPresetSave (dest, false);
}

bool PresetHardwareState::loadDefaultOrFirstPreset()
{
    if (! isValidPluginSelected())
        return false;

    const auto& defaultPreset = getCurrentPlugin().defaultPreset;
    if (defaultPreset.existsAsFile())
    {
        selectPresetInDropdown (defaultPreset);
        if (getSelectedPresetFile() != defaultPreset
            && getSelectedPresetFile().getFullPathName() != defaultPreset.getFullPathName())
        {
            juce::String error;
            if (! engine.loadPreset (defaultPreset, error))
            {
                setStatus ("Default preset error: " + error, true);
                return false;
            }

            if (auto* editor = getPluginEditor())
                editor->repaint();

            savePresetNameEditor.setText (defaultPreset.getFileNameWithoutExtension(),
                                          juce::dontSendNotification);
            setStatus ("Loaded default preset: " + defaultPreset.getFileNameWithoutExtension(), false);
            return true;
        }

        loadSelectedPreset();
        return true;
    }

    if (! presetFiles.isEmpty())
    {
        presetBox.setSelectedItemIndex (0, juce::dontSendNotification);
        loadSelectedPreset();
        return true;
    }

    return false;
}

void PresetHardwareState::handleLoadButton()
{
    if (engine.isHardwareMode())
        sendSelectedHardwareState();
    else
        loadSelectedPreset();
}

void PresetHardwareState::browseForOtherPreset()
{
    auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Select AU preset"),
                                                        fileChooserStartDir(),
                                                        "*.aupreset");
    activeFileChooser = chooser;
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              activeFileChooser.reset();
                              const auto result = fc.getResult();
                              if (result == juce::File())
                              {
                                  restoreSelectionAfterSelectOtherCancelled();
                                  return;
                              }
                              applyChosenPresetFile (result);
                          });
}

void PresetHardwareState::browseForOtherHardwareState()
{
    auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Select hardware sysex dump"),
                                                        fileChooserStartDir(),
                                                        "*.syx");
    activeFileChooser = chooser;
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              activeFileChooser.reset();
                              const auto result = fc.getResult();
                              if (result == juce::File())
                              {
                                  restoreSelectionAfterSelectOtherCancelled();
                                  return;
                              }
                              applyChosenHardwareStateFile (result);
                          });
}

void PresetHardwareState::applyChosenPresetFile (const juce::File& file)
{
    if (! file.existsAsFile() || ! file.hasFileExtension (".aupreset"))
    {
        setStatus (utf8 ("Selected file is not an .aupreset"), true);
        restoreSelectionAfterSelectOtherCancelled();
        return;
    }

    extraPresetFiles.addIfNotAlreadyThere (file);
    const auto preferred = file;
    populatePresets();
    selectPresetInDropdown (preferred);
    lastNonOtherItemId = presetBox.getSelectedId();
    savePresetNameEditor.setText (preferred.getFileNameWithoutExtension(),
                                  juce::dontSendNotification);
    setStatus (utf8 ("Selected preset: ") + preferred.getFileNameWithoutExtension(), false);
}

void PresetHardwareState::applyChosenHardwareStateFile (const juce::File& file)
{
    if (! file.existsAsFile() || ! file.hasFileExtension (".syx"))
    {
        setStatus (utf8 ("Selected file is not a .syx dump"), true);
        restoreSelectionAfterSelectOtherCancelled();
        return;
    }

    extraHardwareStateFiles.addIfNotAlreadyThere (file);
    populateHardwareStates();

    for (int i = 0; i < hardwareStateFiles.size(); ++i)
    {
        if (hardwareStateFiles[i] == file
            || hardwareStateFiles[i].getFullPathName() == file.getFullPathName())
        {
            presetBox.setSelectedId (i + 1, juce::dontSendNotification);
            lastNonOtherItemId = i + 1;
            setStatus (utf8 ("Selected HW state: ") + file.getFileNameWithoutExtension(), false);
            return;
        }
    }

    restoreSelectionAfterSelectOtherCancelled();
}

void PresetHardwareState::onPresetBoxChanged()
{
    const int selectedId = presetBox.getSelectedId();
    if (selectedId == selectOtherItemId)
    {
        if (engine.isHardwareMode())
            browseForOtherHardwareState();
        else
            browseForOtherPreset();
        return;
    }

    if (selectedId > 0)
        lastNonOtherItemId = selectedId;

    const auto presetFile = getSelectedPresetFile();
    if (presetFile.exists())
    {
        savePresetNameEditor.setText (presetFile.getFileNameWithoutExtension(),
                                      juce::dontSendNotification);
    }
}
