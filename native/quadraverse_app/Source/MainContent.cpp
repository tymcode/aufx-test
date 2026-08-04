#include "MainContent.h"
#include "Utf8.h"
#include "MidiSetupDialog.h"
#include "HardwareAudioSetupDialog.h"
#include "SettingsDialog.h"
#include "HostPreferences.h"
#include "QuadraversePrefs.h"
#include "formats/SyxIO.h"
#include "formats/SsxImporter.h"
#include "formats/Qdv1StateIO.h"
#include "domain/AlesisCodec.h"
#include "domain/PatchTranslator.h"
#include "ui/CompatibilityReviewDialog.h"
#include "ui/SaveToDiskDialog.h"
#include "report/ComparisonReportDialog.h"

QuadraverseMainContent::QuadraverseMainContent (HostConfig cfg)
    : config (std::move (cfg))
{
    project.patchSaveDirectory = QuadraversePrefs::getPatchSaveDirectory();

    addAndMakeVisible (sendButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (dupButton);
    addAndMakeVisible (dropButton);
    addAndMakeVisible (ssxButton);
    addAndMakeVisible (qdv1Button);
    addAndMakeVisible (contextBox);
    addAndMakeVisible (liveEditToggle);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (editor);

    liveEditToggle.setToggleState (QuadraversePrefs::getEditLiveToDevice(), juce::dontSendNotification);
    liveEditToggle.onClick = [this]
    {
        QuadraversePrefs::setEditLiveToDevice (liveEditToggle.getToggleState());
    };

    editor.setManager (&contexts);
    editor.onParamChanged = [this] (const qverse::ParamAddress& a, int v) { onParamChanged (a, v); };
    editor.onParamGestureEnd = [this] (const qverse::ParamAddress&) { flushLiveEdit(); };

    contexts.onChanged = [this] { refreshChrome(); };

    sendButton.onClick = [this] { sendToTarget(); };
    saveButton.onClick = [this] { saveToDisk(); };
    loadButton.onClick = [this] { loadIntoContext(); };
    dupButton.onClick = [this]
    {
        contexts.duplicateActive();
        editor.rebuild();
    };
    dropButton.onClick = [this]
    {
        if (auto* a = contexts.getActive())
        {
            if (a->dirty)
            {
                juce::AlertWindow aw (utf8 ("Drop patch context"),
                                      utf8 ("Context has unsaved changes. Drop anyway?"),
                                      juce::AlertWindow::QuestionIcon,
                                      this);
                aw.addButton (utf8 ("Drop"), 1);
                aw.addButton (utf8 ("Cancel"), 0);
                if (aw.runModalLoop() != 1)
                    return;
                contexts.dropActive (true);
            }
            else
            {
                contexts.dropActive (false);
            }
            editor.rebuild();
        }
    };
    ssxButton.onClick = [this] { importSsx(); };
    qdv1Button.onClick = [this] { loadQdv1Preset(); };

    contextBox.onChange = [this]
    {
        contexts.setActiveIndex (contextBox.getSelectedItemIndex());
        editor.rebuild();
        refreshChrome();
        onActiveContextChanged();
    };

    juce::String err;
    engine.startAudioDevice (err);
    ensurePluginLoaded();
    refreshChrome();
    startTimerHz (20);
}

QuadraverseMainContent::~QuadraverseMainContent()
{
    stopTimer();
    hostWindow.reset();
    metersWindow.reset();
    engine.stopAudioDevice();
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
    sendButton.setBounds (top.removeFromLeft (120).reduced (2));
    saveButton.setBounds (top.removeFromLeft (120).reduced (2));
    loadButton.setBounds (top.removeFromLeft (90).reduced (2));
    dupButton.setBounds (top.removeFromLeft (90).reduced (2));
    dropButton.setBounds (top.removeFromLeft (70).reduced (2));
    ssxButton.setBounds (top.removeFromLeft (110).reduced (2));
    qdv1Button.setBounds (top.removeFromLeft (120).reduced (2));
    contextBox.setBounds (top.removeFromLeft (200).reduced (2));
    liveEditToggle.setBounds (top.removeFromLeft (180).reduced (2));

    statusLabel.setBounds (r.removeFromBottom (24));
    editor.setBounds (r);
}

void QuadraverseMainContent::refreshChrome()
{
    const bool hw = engine.isHardwareMode();
    sendButton.setButtonText (hw ? utf8 ("Send Patch") : utf8 ("Send Preset"));
    liveEditToggle.setVisible (hw);
    contextBox.clear (juce::dontSendNotification);
    const auto names = contexts.getNames();
    for (int i = 0; i < names.size(); ++i)
        contextBox.addItem (names[i], i + 1);
    contextBox.setSelectedItemIndex (contexts.getActiveIndex(), juce::dontSendNotification);
}

bool QuadraverseMainContent::isHardwareMode() const
{
    return engine.isHardwareMode();
}

void QuadraverseMainContent::toggleHardwareMode()
{
    engine.setHardwareMode (! engine.isHardwareMode());
    refreshChrome();
    openPluginHost();
    if (hostWindow != nullptr && hostWindow->getPanel() != nullptr)
        hostWindow->getPanel()->refreshHardwareModeUi();
}

void QuadraverseMainContent::openMidiSetup()
{
    showMidiSetupDialog (engine, config, this);
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

void QuadraverseMainContent::openPluginHost()
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

void QuadraverseMainContent::openComparisonReport()
{
    qverse::runComparisonReportDialog (contexts, engine, config, this);
}

juce::File QuadraverseMainContent::currentPatchDir() const
{
    auto dir = project.patchSaveDirectory;
    if (dir == juce::File())
        dir = QuadraversePrefs::getPatchSaveDirectory();
    dir.createDirectory();
    return dir;
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
    // Coalesce live edits while dragging: send at most 20 Hz.
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

void QuadraverseMainContent::sendToTarget()
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

        // Re-apply after reload so sound matches and the new library entry exists.
        if (! engine.applyPluginState (blob, error))
        {
            statusLabel.setText (utf8 ("Reloaded but could not apply state: ") + error,
                                 juce::dontSendNotification);
            return;
        }

        if (hostWindow != nullptr && hostWindow->getPanel() != nullptr)
            hostWindow->getPanel()->recreateEditorAfterPluginReload();

        statusLabel.setText (utf8 ("Sent preset to QDV-1 (") + presetName + utf8 (")"),
                             juce::dontSendNotification);
    }
}

void QuadraverseMainContent::saveToDisk()
{
    if (auto* ctx = contexts.getActive())
    {
        const auto result = qverse::runSaveToDiskDialog (ctx->program, currentPatchDir(), this);
        statusLabel.setText (result.message.isNotEmpty() ? result.message : utf8 ("Save cancelled"),
                             juce::dontSendNotification);
        if (result.ok)
            contexts.clearActiveDirty();
    }
}

void QuadraverseMainContent::loadIntoContext()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        utf8 ("Load patch"),
        currentPatchDir(),
        "*.syx;*.aupreset;*.ssx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              juce::String error;
                              if (file.hasFileExtension (".ssx"))
                              {
                                  const auto dest = currentPatchDir().getChildFile (file.getFileNameWithoutExtension() + ".syx");
                                  int count = 0;
                                  if (! qverse::SsxImporter::importToSyx (file, dest, true, error, &count))
                                  {
                                      statusLabel.setText (error, juce::dontSendNotification);
                                      return;
                                  }
                                  std::vector<qverse::QuadraverbProgram> programs;
                                  if (! qverse::SyxIO::loadFile (dest, programs, error) || programs.empty())
                                  {
                                      statusLabel.setText (error, juce::dontSendNotification);
                                      return;
                                  }
                                  contexts.addProgram (programs.front(), programs.front().name.trim(), dest);
                                  statusLabel.setText (utf8 ("Imported SSX → ") + dest.getFileName()
                                                           + utf8 (" (") + juce::String (count) + utf8 (" programs)"),
                                                       juce::dontSendNotification);
                              }
                              else if (file.hasFileExtension (".aupreset"))
                              {
                                  qverse::QuadraverbProgram prog;
                                  if (! qverse::Qdv1StateIO::loadAupreset (file, prog, error))
                                  {
                                      statusLabel.setText (error, juce::dontSendNotification);
                                      return;
                                  }
                                  contexts.addProgram (prog, file.getFileNameWithoutExtension(), file);
                              }
                              else
                              {
                                  std::vector<qverse::QuadraverbProgram> programs;
                                  if (! qverse::SyxIO::loadFile (file, programs, error) || programs.empty())
                                  {
                                      statusLabel.setText (error, juce::dontSendNotification);
                                      return;
                                  }
                                  // If bank, load first and note count.
                                  contexts.addProgram (programs.front(),
                                                       programs.front().name.trim().isNotEmpty()
                                                           ? programs.front().name.trim()
                                                           : file.getFileNameWithoutExtension(),
                                                       file);
                                  if (programs.size() > 1)
                                      statusLabel.setText (utf8 ("Loaded program 1 of ") + juce::String ((int) programs.size()),
                                                           juce::dontSendNotification);
                              }
                              editor.rebuild();
                              refreshChrome();
                          });
}

void QuadraverseMainContent::importSsx()
{
    auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Import SSX"), currentPatchDir(), "*.ssx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              const auto dest = currentPatchDir().getChildFile (file.getFileNameWithoutExtension() + ".syx");
                              juce::String error;
                              int count = 0;
                              if (! qverse::SsxImporter::importToSyx (file, dest, true, error, &count))
                              {
                                  statusLabel.setText (error, juce::dontSendNotification);
                                  return;
                              }
                              statusLabel.setText (utf8 ("Wrote ") + dest.getFullPathName()
                                                       + utf8 (" (") + juce::String (count) + utf8 (" programs)"),
                                                   juce::dontSendNotification);
                              juce::File (dest).revealToUser();
                          });
}

void QuadraverseMainContent::loadQdv1Preset()
{
    const auto files = qverse::Qdv1StateIO::listUserPresetFiles();
    if (files.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                utf8 ("QDV-1 Presets"),
                                                utf8 ("No user presets found in ")
                                                    + qverse::Qdv1StateIO::presetLibraryDir().getFullPathName());
        return;
    }

    juce::StringArray names;
    for (const auto& f : files)
        names.add (f.getFileNameWithoutExtension());

    juce::AlertWindow w (utf8 ("QDV-1 Preset"), utf8 ("Select a user preset:"), juce::AlertWindow::QuestionIcon, this);
    w.addComboBox ("preset", names);
    w.addButton (utf8 ("Load"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (w.runModalLoop() != 1)
        return;

    const int idx = w.getComboBoxComponent ("preset")->getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (idx, files.size()))
        return;

    qverse::QuadraverbProgram prog;
    juce::String error;
    if (! qverse::Qdv1StateIO::loadUserPresetFile (files[idx], prog, error))
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }
    contexts.addProgram (prog, names[idx], files[idx]);
    editor.rebuild();
    refreshChrome();
}

void QuadraverseMainContent::newProject()
{
    contexts = qverse::PatchContextManager();
    project = {};
    project.patchSaveDirectory = QuadraversePrefs::getPatchSaveDirectory();
    projectFile = juce::File();
    editor.setManager (&contexts);
    refreshChrome();
}

void QuadraverseMainContent::openProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (utf8 ("Open Project"),
                                                        QuadraversePrefs::getPatchSaveDirectory(),
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
                              engine.setHardwareMode (project.hardwareMode);
                              if (project.patchSaveDirectory != juce::File())
                                  QuadraversePrefs::setPatchSaveDirectory (project.patchSaveDirectory);
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
    project.patchSaveDirectory = currentPatchDir();
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
                                                        currentPatchDir(),
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
