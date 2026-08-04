#include "MainContent.h"
#include "Utf8.h"
#include "AUpresetLoader.h"
#include "MidiSetupDialog.h"
#include "HardwareAudioSetupDialog.h"
#include "SettingsDialog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "PresetHardwareState.h"
#include "QuadraversePrefs.h"
#include "formats/SyxIO.h"
#include "formats/SsxImporter.h"
#include "formats/Qdv1StateIO.h"
#include "domain/AlesisCodec.h"
#include "ui/SysexPatchPickerDialog.h"
#include "ui/SpacePlaybackMonitor.h"
#include "report/ComparisonReportDialog.h"
#include "sysex/SysexDeviceModule.h"

QuadraverseMainContent::QuadraverseMainContent (HostConfig cfg)
    : config (std::move (cfg))
{
    setWantsKeyboardFocus (true);

    project.patchSaveDirectory = QuadraversePrefs::getLibraryDirectory();

    addAndMakeVisible (sendButton);
    addAndMakeVisible (contextBox);
    addAndMakeVisible (compareToggle);
    addAndMakeVisible (liveEditToggle);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (editor);

    // Space must reach Target View playback — not activate these controls.
    sendButton.setWantsKeyboardFocus (false);
    compareToggle.setWantsKeyboardFocus (false);
    liveEditToggle.setWantsKeyboardFocus (false);

    liveEditToggle.setToggleState (QuadraversePrefs::getEditLiveToDevice(), juce::dontSendNotification);
    liveEditToggle.onClick = [this]
    {
        QuadraversePrefs::setEditLiveToDevice (liveEditToggle.getToggleState());
    };

    compareToggle.onClick = [this]
    {
        contexts.setActiveCompare (compareToggle.getToggleState());
    };

    editor.setManager (&contexts);
    editor.onParamChanged = [this] (const qverse::ParamAddress& a, int v) { onParamChanged (a, v); };
    editor.onParamGestureEnd = [this] (const qverse::ParamAddress&) { flushLiveEdit(); };

    contexts.onChanged = [this] { refreshChrome(); };

    sendButton.onClick = [this] { sendPatch(); };

    contextBox.onChange = [this]
    {
        contexts.setActiveIndex (contextBox.getSelectedItemIndex());
        editor.rebuild();
        refreshChrome();
        onActiveContextChanged();
    };

    juce::String err;
    {
        const auto hw = HostPreferences::get().getHardwareLoopSettings();
        engine.setHardwareLoopSettings (hw);

        juce::String midiError;
        engine.setMidiOutputDevice (HostPreferences::get().getMidiOutIdentifier(), midiError);
        applyConfiguredMidiInputs();
    }

    engine.startAudioDevice (err);
    ensurePluginLoaded();
    refreshChrome();
    openTargetView();
    startTimerHz (20);

    SpacePlaybackMonitor::install ([safe = juce::Component::SafePointer<QuadraverseMainContent> (this)]
                                   {
                                       if (safe == nullptr)
                                           return false;
                                       return safe->handleSpacePlayback();
                                   });
}

QuadraverseMainContent::~QuadraverseMainContent()
{
    SpacePlaybackMonitor::uninstall();
    stopTimer();
    if (keyListenerOwner != nullptr)
        keyListenerOwner->removeKeyListener (this);
    hostWindow.reset();
    metersWindow.reset();
    engine.stopAudioDevice();
}

void QuadraverseMainContent::parentHierarchyChanged()
{
    if (keyListenerOwner != nullptr)
    {
        keyListenerOwner->removeKeyListener (this);
        keyListenerOwner = nullptr;
    }

    if (auto* top = getTopLevelComponent())
    {
        top->addKeyListener (this);
        keyListenerOwner = top;
    }
}

bool QuadraverseMainContent::handleSpacePlayback()
{
    if (isEditableFieldFocused())
        return false;
    toggleTargetPlayback();
    return true;
}

bool QuadraverseMainContent::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::spaceKey))
        return handleSpacePlayback();
    return Component::keyPressed (key);
}

bool QuadraverseMainContent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key.isKeyCode (juce::KeyPress::spaceKey))
        return handleSpacePlayback();
    return false;
}

void QuadraverseMainContent::ensurePluginLoaded()
{
    if (engine.getPlugin() != nullptr)
        return;
    if (auto* p = config.defaultPlugin())
    {
        juce::String error;
        engine.loadPlugin (p->toPluginDescription(), error);
        if (error.isNotEmpty())
            statusLabel.setText (utf8 ("Plugin: ") + error, juce::dontSendNotification);
    }
}

void QuadraverseMainContent::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (32);
    sendButton.setBounds (top.removeFromLeft (110).reduced (2));
    contextBox.setBounds (top.removeFromLeft (220).reduced (2));
    compareToggle.setBounds (top.removeFromLeft (100).reduced (2));
    liveEditToggle.setBounds (top.removeFromLeft (180).reduced (2));

    statusLabel.setBounds (r.removeFromBottom (24));
    editor.setBounds (r);
}

void QuadraverseMainContent::refreshChrome()
{
    const bool hw = engine.isHardwareMode();
    liveEditToggle.setVisible (hw);
    contextBox.clear (juce::dontSendNotification);
    const auto names = contexts.getNames();
    for (int i = 0; i < names.size(); ++i)
        contextBox.addItem (names[i], i + 1);
    contextBox.setSelectedItemIndex (contexts.getActiveIndex(), juce::dontSendNotification);

    if (auto* a = contexts.getActive())
        compareToggle.setToggleState (a->compare, juce::dontSendNotification);

    // Keep Patch → Delete Patch Context enablement in sync with context count.
    if (auto* top = getTopLevelComponent())
        if (auto* menus = dynamic_cast<juce::MenuBarModel*> (top))
            menus->menuItemsChanged();
}

bool QuadraverseMainContent::isHardwareMode() const
{
    return engine.isHardwareMode();
}

juce::String QuadraverseMainContent::configuredDeviceName() const
{
    juce::String device;
    const auto outId = HostPreferences::get().getMidiOutIdentifier();
    if (outId.isNotEmpty())
        device = findMidiEndpointInfo (outId, true).name;
    if (device.isEmpty())
        device = HostPreferences::get().getMidiSysexModule();
    if (device.isEmpty())
        device = utf8 ("Quadraverb");
    return device;
}

void QuadraverseMainContent::toggleHardwareMode()
{
    engine.setHardwareMode (! engine.isHardwareMode());
    refreshChrome();
    openTargetView();
    if (hostWindow != nullptr)
    {
        hostWindow->setVisible (true);
        hostWindow->toFront (true);
        if (hostWindow->getPanel() != nullptr)
            hostWindow->getPanel()->refreshHardwareModeUi();
    }
}

void QuadraverseMainContent::applyConfiguredMidiInputs()
{
    juce::StringArray midiIds;
    const auto available = engine.getMidiInputDevices();
    for (const auto& wantedName : config.defaultMidiInputs)
    {
        for (const auto& device : available)
        {
            if (device.name.equalsIgnoreCase (wantedName)
                || device.name.containsIgnoreCase (wantedName))
                midiIds.addIfNotAlreadyThere (device.identifier);
        }
    }

    const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
    if (dumpIn.isNotEmpty())
        midiIds.addIfNotAlreadyThere (dumpIn);

    engine.setMidiInputDevices (midiIds);
}

void QuadraverseMainContent::openMidiSetup()
{
    showMidiSetupDialog (engine, config, this);
    // Dialog saves controller names into config; open those ports for HUI/Play.
    applyConfiguredMidiInputs();
}

void QuadraverseMainContent::openHardwareAudioSetup()
{
    showHardwareAudioSetupDialog (engine, config.fixturesDir, this);
}

void QuadraverseMainContent::openSettings()
{
    showSettingsDialog (config, this, nullptr);
}

void QuadraverseMainContent::openLevelMeters()
{
    if (metersWindow == nullptr)
    {
        metersWindow = std::make_unique<LevelMetersWindow> (
            engine,
            config.fixturesDir,
            config.projectRoot.getChildFile ("calibration"),
            config.pythonCli,
            [this] { metersWindow.reset(); },
            [] (const juce::KeyPress&) { return false; });
    }
    metersWindow->setVisible (true);
    metersWindow->toFront (true);
}

void QuadraverseMainContent::openTargetView()
{
    if (hostWindow == nullptr)
    {
        hostWindow = std::make_unique<qverse::PluginHostWindow> (
            engine,
            config,
            [this] { hostWindow.reset(); });
    }
    hostWindow->setVisible (true);
    hostWindow->toFront (true);
}

bool QuadraverseMainContent::isTargetViewOpen() const
{
    return hostWindow != nullptr && hostWindow->isVisible();
}

bool QuadraverseMainContent::isLevelMetersOpen() const
{
    return metersWindow != nullptr && metersWindow->isVisible();
}

void QuadraverseMainContent::toggleTargetPlayback()
{
    openTargetView();
    if (hostWindow != nullptr && hostWindow->getPanel() != nullptr)
        hostWindow->getPanel()->togglePlayback();
}

bool QuadraverseMainContent::isEditableFieldFocused()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr)
        return false;
    if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
        return true;
    if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
        return true;
    if (dynamic_cast<juce::TextInputTarget*> (focused) != nullptr)
        return true;
    return false;
}

void QuadraverseMainContent::openComparisonReport()
{
    qverse::runComparisonReportDialog (contexts, engine, config, this);
}

juce::File QuadraverseMainContent::currentLibraryDir() const
{
    auto dir = project.patchSaveDirectory;
    if (dir == juce::File())
        dir = QuadraversePrefs::getLibraryDirectory();
    dir.createDirectory();
    return dir;
}

juce::String QuadraverseMainContent::sanitizedStem (const juce::String& name) const
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty())
        safe = "patch";
    return safe;
}

void QuadraverseMainContent::onParamChanged (const qverse::ParamAddress& addr, int value)
{
    pendingLiveAddr = addr;
    pendingLiveValue = value;
    hasPendingLive = true;
}

void QuadraverseMainContent::flushLiveEdit()
{
    if (! hasPendingLive)
        return;
    hasPendingLive = false;

    if (! engine.isHardwareMode() || ! liveEditToggle.getToggleState())
        return;

    const auto msg = qverse::AlesisCodec::buildChangeParameter (
        0x02,
        (uint8_t) pendingLiveAddr.function,
        (uint8_t) pendingLiveAddr.page,
        (uint16_t) juce::jlimit (0, 0xffff, pendingLiveValue));
    juce::Array<juce::MidiMessage> messages;
    messages.add (msg);
    engine.sendMidiMessages (messages);
}

void QuadraverseMainContent::timerCallback()
{
    if (hasPendingLive && engine.isHardwareMode() && liveEditToggle.getToggleState())
        flushLiveEdit();
}

void QuadraverseMainContent::sendActivePatchToHardware()
{
    auto* ctx = contexts.getActive();
    if (ctx == nullptr)
        return;

    auto prog = ctx->program;
    prog.flushValuesToBytes();
    if (! prog.hasValidBytes)
    {
        juce::Array<juce::MidiMessage> messages;
        for (int f = 0; f < qverse::QuadraverbProgram::kMaxFunctions; ++f)
            for (int p = 0; p < qverse::QuadraverbProgram::kMaxPages; ++p)
                if (prog.isKnown (f, p))
                    messages.add (qverse::AlesisCodec::buildChangeParameter (
                        0x02, (uint8_t) f, (uint8_t) p, (uint16_t) prog.getParam (f, p)));
        engine.sendMidiMessages (messages);
        statusLabel.setText (utf8 ("Sent parameter edits to hardware"), juce::dontSendNotification);
        return;
    }

    const auto msg = qverse::AlesisCodec::buildLoadProgram (
        0x02, qverse::AlesisCodec::kEditBuffer, prog.bytes.data());
    juce::Array<juce::MidiMessage> messages;
    messages.add (msg);
    engine.sendMidiMessages (messages);
    statusLabel.setText (utf8 ("Sent patch to hardware"), juce::dontSendNotification);
}

void QuadraverseMainContent::onActiveContextChanged()
{
    if (engine.isHardwareMode() && liveEditToggle.getToggleState())
        sendActivePatchToHardware();
}

void QuadraverseMainContent::sendPatch()
{
    auto* ctx = contexts.getActive();
    if (ctx == nullptr)
        return;

    juce::String error;
    if (engine.isHardwareMode())
    {
        sendActivePatchToHardware();
    }
    else
    {
        ensurePluginLoaded();

        juce::String presetId;
        const auto presetName = ctx->name.isNotEmpty() ? ctx->name : utf8 ("Quadraverse Patch");
        if (! qverse::Qdv1StateIO::saveUserPreset (presetName, ctx->program, presetId, error))
        {
            statusLabel.setText (error, juce::dontSendNotification);
            return;
        }

        juce::MemoryBlock blob;
        if (! qverse::Qdv1StateIO::toStateBlob (ctx->program, blob, error))
        {
            statusLabel.setText (error, juce::dontSendNotification);
            return;
        }

        if (hostWindow != nullptr && hostWindow->getPanel() != nullptr)
            hostWindow->getPanel()->destroyPluginEditor();

        if (! engine.reloadCurrentPlugin (error))
        {
            statusLabel.setText (utf8 ("Preset saved but reload failed: ") + error,
                                 juce::dontSendNotification);
            return;
        }

        if (! engine.applyPluginState (blob, error))
        {
            statusLabel.setText (utf8 ("Reloaded but could not apply state: ") + error,
                                 juce::dontSendNotification);
            return;
        }

        if (hostWindow != nullptr && hostWindow->getPanel() != nullptr)
            hostWindow->getPanel()->recreateEditorAfterPluginReload();

        statusLabel.setText (utf8 ("Sent patch to QDV-1 (") + presetName + utf8 (")"),
                             juce::dontSendNotification);
    }
}

void QuadraverseMainContent::newPatchContext()
{
    contexts.addEmpty();
    editor.rebuild();
    refreshChrome();
}

void QuadraverseMainContent::duplicatePatchContext()
{
    contexts.duplicateActive();
    editor.rebuild();
    refreshChrome();
}

void QuadraverseMainContent::renamePatchContext()
{
    auto* a = contexts.getActive();
    if (a == nullptr)
        return;

    juce::AlertWindow w (utf8 ("Rename Patch Context"),
                         utf8 ("Enter a new name:"),
                         juce::AlertWindow::QuestionIcon,
                         this);
    w.addTextEditor ("name", a->name, utf8 ("Name"));
    w.addButton (utf8 ("Rename"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (w.runModalLoop() != 1)
        return;

    if (contexts.renameActive (w.getTextEditorContents ("name")))
    {
        editor.rebuild();
        refreshChrome();
    }
}

bool QuadraverseMainContent::canDeletePatchContext() const
{
    return contexts.size() > 1;
}

void QuadraverseMainContent::deletePatchContext()
{
    if (! canDeletePatchContext())
        return;

    auto* a = contexts.getActive();
    if (a == nullptr)
        return;

    juce::AlertWindow aw (utf8 ("Delete Patch Context"),
                          utf8 ("Delete \"") + a->name + utf8 ("\"? This cannot be undone."),
                          juce::AlertWindow::WarningIcon,
                          this);
    aw.addButton (utf8 ("Delete"), 1);
    aw.addButton (utf8 ("Cancel"), 0);
    if (aw.runModalLoop() != 1)
        return;

    contexts.dropActive (true);
    editor.rebuild();
    refreshChrome();
}

void QuadraverseMainContent::importProgramsAsContexts (std::vector<qverse::QuadraverbProgram> programs,
                                                       const juce::File& source,
                                                       bool batchCompareOff)
{
    if (programs.empty())
        return;

    for (auto& prog : programs)
    {
        auto name = prog.name.trim();
        if (name.isEmpty())
            name = source.existsAsFile() ? source.getFileNameWithoutExtension()
                                         : contexts.nextDefaultName();
        contexts.addProgram (std::move (prog), name, source, ! batchCompareOff);
    }
    editor.rebuild();
    refreshChrome();
}

void QuadraverseMainContent::importSysexFile (const juce::File& file)
{
    juce::String error;
    std::vector<qverse::QuadraverbProgram> programs;
    if (! qverse::SyxIO::loadFile (file, programs, error) || programs.empty())
    {
        statusLabel.setText (error.isNotEmpty() ? error : utf8 ("No patches found"),
                             juce::dontSendNotification);
        return;
    }

    juce::StringArray names;
    for (size_t i = 0; i < programs.size(); ++i)
    {
        auto n = programs[i].name.trim();
        if (n.isEmpty())
            n = utf8 ("Patch ") + juce::String ((int) i + 1);
        names.add (n);
    }

    if (programs.size() > 1)
    {
        const auto pick = qverse::runSysexPatchPicker (names, this, true);
        if (! pick.ok)
            return;

        std::vector<qverse::QuadraverbProgram> chosen;
        for (int idx : pick.selectedIndices)
            if (juce::isPositiveAndBelow (idx, (int) programs.size()))
                chosen.push_back (programs[(size_t) idx]);

        const bool batch = chosen.size() > 1;
        importProgramsAsContexts (std::move (chosen), file, batch);
        statusLabel.setText (utf8 ("Imported ") + juce::String (pick.selectedIndices.size())
                                 + utf8 (" patch context(s) from Library"),
                             juce::dontSendNotification);
        return;
    }

    importProgramsAsContexts (std::move (programs), file, false);
    statusLabel.setText (utf8 ("Loaded ") + names[0], juce::dontSendNotification);
}

void QuadraverseMainContent::loadPatchFromDevice()
{
    const auto outId = HostPreferences::get().getMidiOutIdentifier();
    if (outId.isEmpty())
    {
        statusLabel.setText (utf8 ("No MIDI out configured (Session → MIDI Setup)"),
                             juce::dontSendNotification);
        return;
    }

    const auto info = findMidiEndpointInfo (outId, true);
    const auto* module = resolveSelectedSysexModule (info);
    if (module == nullptr)
    {
        statusLabel.setText (utf8 ("No sysex module for ") + info.name,
                             juce::dontSendNotification);
        return;
    }

    juce::String openError;
    if (! engine.setMidiOutputDevice (outId, openError))
    {
        statusLabel.setText (openError, juce::dontSendNotification);
        return;
    }

    const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
    if (dumpIn.isNotEmpty())
    {
        auto ids = engine.getSelectedMidiInputIdentifiers();
        if (! ids.contains (dumpIn))
        {
            ids.add (dumpIn);
            engine.setMidiInputDevices (ids);
        }
    }

    if (! engine.sendMidiMessage (module->buildDumpRequest()))
    {
        statusLabel.setText (utf8 ("Failed to send dump request"), juce::dontSendNotification);
        return;
    }

    juce::MidiMessage dump;
    juce::String error;
    if (! engine.waitForSysexDump (
            [module] (const juce::MidiMessage& m) { return module->isDumpResponse (m); },
            dump, 5000, error))
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    if (! module->validateDump (dump))
    {
        statusLabel.setText (utf8 ("Received sysex failed validation"), juce::dontSendNotification);
        return;
    }

    std::vector<qverse::QuadraverbProgram> programs;
    if (! qverse::SyxIO::loadFromMemory (dump.getRawData(), (size_t) dump.getRawDataSize(),
                                         programs, error)
        || programs.empty())
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    auto name = programs.front().name.trim();
    if (name.isEmpty())
        name = utf8 ("From ") + configuredDeviceName();
    contexts.addProgram (std::move (programs.front()), name, {}, true);
    editor.rebuild();
    refreshChrome();
    statusLabel.setText (utf8 ("Captured patch from ") + configuredDeviceName(),
                         juce::dontSendNotification);
}

void QuadraverseMainContent::loadPatchFromPlugin()
{
    ensurePluginLoaded();
    auto* plugin = engine.getPlugin();
    if (plugin == nullptr)
    {
        statusLabel.setText (utf8 ("No plugin loaded"), juce::dontSendNotification);
        return;
    }

    juce::MemoryBlock hostState;
    plugin->getStateInformation (hostState);
    juce::MemoryBlock state;
    juce::String error;
    if (! AUpresetLoader::extractStateBytes (hostState, state, error))
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    qverse::QuadraverbProgram prog;
    if (! qverse::Qdv1StateIO::fromStateBlob (state, prog, error))
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    auto name = prog.name.trim();
    if (name.isEmpty())
        name = utf8 ("From Plugin");
    contexts.addProgram (std::move (prog), name, {}, true);
    editor.rebuild();
    refreshChrome();
    statusLabel.setText (utf8 ("Captured patch from plugin"), juce::dontSendNotification);
}

void QuadraverseMainContent::loadPatchFromPresetFile()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Load Patch from Preset File"),
        currentLibraryDir(),
        "*.aupreset");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              qverse::QuadraverbProgram prog;
                              juce::String error;
                              if (! qverse::Qdv1StateIO::loadAupreset (file, prog, error))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }
                              auto name = prog.name.trim();
                              if (name.isEmpty())
                                  name = file.getFileNameWithoutExtension();
                              contexts.addProgram (std::move (prog), name, file, true);
                              editor.rebuild();
                              refreshChrome();
                              statusLabel.setText (utf8 ("Loaded ") + file.getFileName()
                                                       + utf8 (" into Library context"),
                                                   juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::loadPatchFromSysexDump()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Load Patch from Sysex Dump"),
        currentLibraryDir(),
        "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              importSysexFile (file);
                          });
}

void QuadraverseMainContent::savePatchAsPreset()
{
    auto* ctx = contexts.getActive();
    if (ctx == nullptr)
        return;

    const auto stem = sanitizedStem (ctx->name);
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Save Patch as Preset"),
        currentLibraryDir().getChildFile (stem + ".aupreset"),
        "*.aupreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser, stem] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".aupreset"))
                                  file = file.withFileExtension (".aupreset");
                              if (file.getFileNameWithoutExtension().isEmpty())
                                  file = file.getSiblingFile (stem + ".aupreset");

                              QuadraversePrefs::setLibraryDirectory (file.getParentDirectory());
                              project.patchSaveDirectory = file.getParentDirectory();

                              auto* c = contexts.getActive();
                              if (c == nullptr)
                                  return;
                              juce::String error;
                              if (! qverse::Qdv1StateIO::saveAupreset (file, c->program, error))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }
                              contexts.clearActiveDirty();
                              statusLabel.setText (utf8 ("Saved to Library: ") + file.getFileName(),
                                                   juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::savePatchAsSysex()
{
    auto* ctx = contexts.getActive();
    if (ctx == nullptr)
        return;

    const auto stem = sanitizedStem (ctx->name);
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Save Patch as Sysex"),
        currentLibraryDir().getChildFile (stem + ".syx"),
        "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser, stem] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".syx"))
                                  file = file.withFileExtension (".syx");
                              if (file.getFileNameWithoutExtension().isEmpty())
                                  file = file.getSiblingFile (stem + ".syx");

                              QuadraversePrefs::setLibraryDirectory (file.getParentDirectory());
                              project.patchSaveDirectory = file.getParentDirectory();

                              auto* c = contexts.getActive();
                              if (c == nullptr)
                                  return;
                              auto prog = c->program;
                              prog.setName (c->name);
                              juce::String error;
                              if (! qverse::SyxIO::saveSingle (file, prog, qverse::AlesisCodec::kEditBuffer, error))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }
                              contexts.clearActiveDirty();
                              statusLabel.setText (utf8 ("Saved to Library: ") + file.getFileName(),
                                                   juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::importSysexBank()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Import Sysex Bank"),
        currentLibraryDir(),
        "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              importSysexFile (file);
                          });
}

void QuadraverseMainContent::saveBulkDump()
{
    const auto outId = HostPreferences::get().getMidiOutIdentifier();
    if (outId.isEmpty())
    {
        statusLabel.setText (utf8 ("No MIDI out configured (Session → MIDI Setup)"),
                             juce::dontSendNotification);
        return;
    }

    const auto info = findMidiEndpointInfo (outId, true);
    const auto* module = resolveSelectedSysexModule (info);
    if (module == nullptr)
    {
        statusLabel.setText (utf8 ("No sysex module for ") + info.name,
                             juce::dontSendNotification);
        return;
    }

    const auto bulkRequest = module->buildBulkDumpRequest();
    if (bulkRequest.getRawDataSize() == 0)
    {
        statusLabel.setText (utf8 ("Bulk dump is not supported for ") + module->getDisplayName(),
                             juce::dontSendNotification);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Save Bulk Dump"),
        currentLibraryDir().getChildFile ("Quadraverb Bank.syx"),
        "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser, module, outId, bulkRequest] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".syx"))
                                  file = file.withFileExtension (".syx");

                              QuadraversePrefs::setLibraryDirectory (file.getParentDirectory());
                              project.patchSaveDirectory = file.getParentDirectory();

                              juce::String openError;
                              if (! engine.setMidiOutputDevice (outId, openError))
                              {
                                  statusLabel.setText (openError, juce::dontSendNotification);
                                  return;
                              }

                              const auto dumpIn = HostPreferences::get().getMidiDumpInIdentifier();
                              if (dumpIn.isNotEmpty())
                              {
                                  auto ids = engine.getSelectedMidiInputIdentifiers();
                                  if (! ids.contains (dumpIn))
                                  {
                                      ids.add (dumpIn);
                                      engine.setMidiInputDevices (ids);
                                  }
                              }

                              statusLabel.setText (utf8 ("Requesting bulk dump from ")
                                                       + configuredDeviceName() + utf8 ("…"),
                                                   juce::dontSendNotification);

                              if (! engine.sendMidiMessage (bulkRequest))
                              {
                                  statusLabel.setText (utf8 ("Failed to send bulk dump request"),
                                                       juce::dontSendNotification);
                                  return;
                              }

                              juce::MidiMessage dump;
                              juce::String error;
                              // Full bank is ~15 KB over MIDI — allow plenty of time.
                              if (! engine.waitForSysexDump (
                                      [module] (const juce::MidiMessage& m)
                                      { return module->isBulkDumpResponse (m); },
                                      dump, 60000, error))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }

                              if (! file.replaceWithData (dump.getRawData(),
                                                          (size_t) dump.getRawDataSize()))
                              {
                                  statusLabel.setText (utf8 ("Could not write ") + file.getFullPathName(),
                                                       juce::dontSendNotification);
                                  return;
                              }

                              statusLabel.setText (utf8 ("Saved bulk dump to Library: ")
                                                       + file.getFileName()
                                                       + utf8 (" (")
                                                       + juce::String (dump.getRawDataSize())
                                                       + utf8 (" bytes)"),
                                                   juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::convertSsx()
{
    auto openChooser = std::make_shared<juce::FileChooser> (
        utf8 ("Convert SSX"),
        currentLibraryDir(),
        "*.ssx");
    openChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, openChooser] (const juce::FileChooser& fc)
                              {
                                  const auto ssx = fc.getResult();
                                  if (! ssx.existsAsFile())
                                      return;

                                  juce::ToggleButton importAfterToggle;
                                  importAfterToggle.setButtonText (utf8 ("Import after converting"));
                                  importAfterToggle.setName ({}); // AlertWindow draws getName() as a header
                                  importAfterToggle.setToggleState (QuadraversePrefs::getImportAfterConvertSsx(),
                                                                    juce::dontSendNotification);
                                  importAfterToggle.setSize (320, 24);

                                  juce::AlertWindow opts (utf8 ("Convert SSX"),
                                                          utf8 ("Choose where to save the converted .syx in your Library."),
                                                          juce::AlertWindow::QuestionIcon,
                                                          this);
                                  opts.addTextEditor ("path",
                                                      QuadraversePrefs::getConvertSsxDirectory()
                                                          .getChildFile (ssx.getFileNameWithoutExtension() + ".syx")
                                                          .getFullPathName(),
                                                      utf8 ("Output .syx path"));
                                  opts.addCustomComponent (&importAfterToggle);
                                  opts.addButton (utf8 ("Choose…"), 2);
                                  opts.addButton (utf8 ("Convert"), 1, juce::KeyPress (juce::KeyPress::returnKey));
                                  opts.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

                                  for (;;)
                                  {
                                      const int r = opts.runModalLoop();
                                      if (r == 0)
                                          return;

                                      if (r == 2)
                                      {
                                          juce::FileChooser saveChooser (
                                              utf8 ("Save Converted Sysex"),
                                              juce::File (opts.getTextEditorContents ("path")),
                                              "*.syx");
                                          if (saveChooser.browseForFileToSave (true))
                                          {
                                              auto out = saveChooser.getResult();
                                              if (! out.hasFileExtension (".syx"))
                                                  out = out.withFileExtension (".syx");
                                              if (auto* te = opts.getTextEditor ("path"))
                                                  te->setText (out.getFullPathName());
                                          }
                                          continue;
                                      }

                                      auto dest = juce::File (opts.getTextEditorContents ("path").trim());
                                      if (dest == juce::File())
                                          return;
                                      if (! dest.hasFileExtension (".syx"))
                                          dest = dest.withFileExtension (".syx");

                                      const bool doImport = importAfterToggle.getToggleState();
                                      QuadraversePrefs::setImportAfterConvertSsx (doImport);
                                      QuadraversePrefs::setConvertSsxDirectory (dest.getParentDirectory());

                                      juce::String error;
                                      int count = 0;
                                      if (! qverse::SsxImporter::importToSyx (ssx, dest, true, error, &count))
                                      {
                                          statusLabel.setText (error, juce::dontSendNotification);
                                          return;
                                      }

                                      statusLabel.setText (utf8 ("Converted to Library: ") + dest.getFileName()
                                                               + utf8 (" (") + juce::String (count)
                                                               + utf8 (" programs)"),
                                                           juce::dontSendNotification);

                                      if (doImport)
                                          importSysexFile (dest);
                                      return;
                                  }
                              });
}

void QuadraverseMainContent::saveBankAsSysex()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Save Bank as Sysex"),
        currentLibraryDir().getChildFile ("bank.syx"),
        "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".syx"))
                                  file = file.withFileExtension (".syx");

                              QuadraversePrefs::setLibraryDirectory (file.getParentDirectory());
                              project.patchSaveDirectory = file.getParentDirectory();

                              std::vector<qverse::QuadraverbProgram> programs;
                              for (int i = 0; i < contexts.size(); ++i)
                              {
                                  if (auto* c = contexts.get (i))
                                  {
                                      auto prog = c->program;
                                      prog.setName (c->name);
                                      programs.push_back (std::move (prog));
                                  }
                              }

                              juce::String error;
                              if (! qverse::SyxIO::savePrograms (file, programs, error))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }
                              statusLabel.setText (utf8 ("Saved bank to Library: ") + file.getFileName()
                                                       + utf8 (" (") + juce::String ((int) programs.size())
                                                       + utf8 (" patches)"),
                                                   juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::saveBankAsPresets()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Save Bank as Presets"),
        currentLibraryDir(),
        "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir == juce::File())
                                  return;
                              dir.createDirectory();
                              QuadraversePrefs::setLibraryDirectory (dir);
                              project.patchSaveDirectory = dir;

                              int wrote = 0;
                              juce::String error;
                              for (int i = 0; i < contexts.size(); ++i)
                              {
                                  if (auto* c = contexts.get (i))
                                  {
                                      const auto file = dir.getChildFile (sanitizedStem (c->name) + ".aupreset");
                                      if (qverse::Qdv1StateIO::saveAupreset (file, c->program, error))
                                          ++wrote;
                                  }
                              }
                              statusLabel.setText (utf8 ("Saved ") + juce::String (wrote)
                                                       + utf8 (" presets to Library folder"),
                                                   juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::newProject()
{
    contexts = qverse::PatchContextManager();
    contexts.onChanged = [this] { refreshChrome(); };
    project = {};
    project.patchSaveDirectory = QuadraversePrefs::getLibraryDirectory();
    projectFile = juce::File();
    editor.setManager (&contexts);
    refreshChrome();
}

void QuadraverseMainContent::openProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Open Project"),
                                                        QuadraversePrefs::getLibraryDirectory(),
                                                        "*.qvproj");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              qverse::ProjectState state;
                              juce::String error;
                              if (! qverse::ProjectFile::load (file, state, error))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }
                              projectFile = file;
                              project.patchSaveDirectory = state.patchSaveDirectory;
                              project.hardwareMode = state.hardwareMode;
                              project.windowState = state.windowState;
                              project.randomizationSettings = state.randomizationSettings;
                              contexts = std::move (state.contexts);
                              contexts.onChanged = [this] { refreshChrome(); };
                              engine.setHardwareMode (project.hardwareMode);
                              if (project.patchSaveDirectory != juce::File())
                                  QuadraversePrefs::setLibraryDirectory (project.patchSaveDirectory);
                              QuadraversePrefs::addRecentProject (file);
                              editor.setManager (&contexts);
                              refreshChrome();
                              statusLabel.setText (utf8 ("Opened ") + file.getFileName(), juce::dontSendNotification);
                          });
}

void QuadraverseMainContent::saveProject()
{
    if (projectFile == juce::File())
    {
        saveProjectAs();
        return;
    }
    project.contexts = contexts;
    project.hardwareMode = engine.isHardwareMode();
    project.patchSaveDirectory = currentLibraryDir();
    juce::String error;
    if (qverse::ProjectFile::save (projectFile, project, error))
    {
        QuadraversePrefs::addRecentProject (projectFile);
        statusLabel.setText (utf8 ("Saved ") + projectFile.getFileName(), juce::dontSendNotification);
    }
    else
        statusLabel.setText (error, juce::dontSendNotification);
}

void QuadraverseMainContent::saveProjectAs()
{
    auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Save Project"),
                                                        currentLibraryDir(),
                                                        "*.qvproj");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (".qvproj"))
                                  file = file.withFileExtension (".qvproj");
                              projectFile = file;
                              saveProject();
                          });
}
