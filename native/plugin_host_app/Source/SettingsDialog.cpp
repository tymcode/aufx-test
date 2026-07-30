#include "SettingsDialog.h"
#include "HostDialog.h"
#include "HostPreferences.h"
#include "AuPluginScanner.h"
#include "Utf8.h"

SettingsPanel::SettingsPanel (const HostConfig& config, juce::KnownPluginList* knownPluginsIn)
    : dataRoot (config.projectRoot),
      knownPlugins (knownPluginsIn)
{
    dataRootLabel.setText ("Exploration data folder", juce::dontSendNotification);
    addAndMakeVisible (dataRootLabel);
    dataRootEditor.setText (config.projectRoot.getFullPathName(), juce::dontSendNotification);
    addAndMakeVisible (dataRootEditor);
    addAndMakeVisible (chooseDataRootButton);
    addAndMakeVisible (revealDataRootButton);

    configLabel.setText ("Config file override (optional)", juce::dontSendNotification);
    addAndMakeVisible (configLabel);
    configEditor.setText (HostPreferences::get().getConfigPathPref(), juce::dontSendNotification);
    configEditor.setTextToShowWhenEmpty ("(use data folder / bundled default)", juce::Colours::grey);
    addAndMakeVisible (configEditor);
    addAndMakeVisible (chooseConfigButton);
    addAndMakeVisible (clearConfigButton);

    allowInstrumentInputToggle.setButtonText ("Allow input to virtual instruments");
    allowInstrumentInputToggle.setTooltip (
        utf8 ("Enable audio input buses on instruments/samplers and feed the source clip into them "
              "(for sampling). Off by default — some AUs crash if input is enabled without a proper feed."));
    allowInstrumentInputToggle.setToggleState (HostPreferences::get().getAllowInstrumentAudioInput(),
                                               juce::dontSendNotification);
    addAndMakeVisible (allowInstrumentInputToggle);

    scanTimeoutLabel.setText ("Plugin scan timeout", juce::dontSendNotification);
    addAndMakeVisible (scanTimeoutLabel);
    scanTimeoutBox.addItem ("15 seconds", HostPreferences::defaultPluginScanTimeoutMs);
    scanTimeoutBox.addItem ("30 seconds", 30000);
    scanTimeoutBox.addItem ("1 minute", 60000);
    scanTimeoutBox.addItem ("2 minutes", 120000);
    scanTimeoutBox.addItem ("5 minutes", HostPreferences::maxPluginScanTimeoutMs);
    scanTimeoutBox.setTooltip (
        "How long each Audio Unit may take to respond during scan or Retry selected. "
        "Increase this if plugins time out under load; applies to the next scan or retry.");
    scanTimeoutBox.setSelectedId (HostPreferences::get().getPluginScanTimeoutMs(), juce::dontSendNotification);
    addAndMakeVisible (scanTimeoutBox);

    skippedHeading.setText ("Skipped AU plugins (crashed / hung during scan)", juce::dontSendNotification);
    addAndMakeVisible (skippedHeading);

    skippedList.setRowHeight (22);
    skippedList.setMultipleSelectionEnabled (false);
    skippedList.setColour (juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha (0.15f));
    skippedList.setColour (juce::ListBox::outlineColourId, juce::Colours::grey.withAlpha (0.4f));
    skippedList.setOutlineThickness (1);
    addAndMakeVisible (skippedList);

    retrySkippedButton.setTooltip ("Rescan only the selected plugin and add it to the cache if it succeeds.");
    retrySkippedButton.setEnabled (false);
    addAndMakeVisible (retrySkippedButton);
    addAndMakeVisible (resetButton);

    chooseDataRootButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Choose exploration data folder",
                                                            juce::File (dataRootEditor.getText()),
                                                            "*");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto result = fc.getResult();
                                  if (result != juce::File())
                                  {
                                      dataRootEditor.setText (result.getFullPathName(), juce::dontSendNotification);
                                      reloadSkippedList();
                                  }
                              });
    };

    revealDataRootButton.onClick = [this]
    {
        const auto path = juce::File (dataRootEditor.getText().trim());
        if (path != juce::File())
            path.revealToUser();
    };

    chooseConfigButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Choose host.config.json",
                                                            juce::File (configEditor.getText()),
                                                            "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto result = fc.getResult();
                                  if (result != juce::File())
                                      configEditor.setText (result.getFullPathName(), juce::dontSendNotification);
                              });
    };

    clearConfigButton.onClick = [this] { configEditor.clear(); };

    retrySkippedButton.onClick = [this] { retrySelectedSkippedPlugin(); };

    resetButton.onClick = [this]
    {
        resetRequested = true;
        dataRootEditor.setText (HostPreferences::get().defaultExplorationDataRoot().getFullPathName(),
                                juce::dontSendNotification);
        configEditor.clear();
        allowInstrumentInputToggle.setToggleState (false, juce::dontSendNotification);
        scanTimeoutBox.setSelectedId (HostPreferences::defaultPluginScanTimeoutMs, juce::dontSendNotification);
        reloadSkippedList();
    };

    reloadSkippedList();
}

void SettingsPanel::reloadSkippedList()
{
    const auto root = getSelectedDataRoot() != juce::File() ? getSelectedDataRoot() : dataRoot;
    skippedIds = AuPluginScanner::loadSkipList (root);
    skippedIds.removeEmptyStrings();
    skippedList.updateContent();
    skippedList.deselectAllRows();
    updateRetryEnabled();
}

void SettingsPanel::updateRetryEnabled()
{
    retrySkippedButton.setEnabled (skippedList.getSelectedRow() >= 0
                                   && skippedList.getSelectedRow() < skippedIds.size());
}

void SettingsPanel::retrySelectedSkippedPlugin()
{
    const int row = skippedList.getSelectedRow();
    if (row < 0 || row >= skippedIds.size())
        return;

    const auto pluginId = skippedIds[row];
    const auto root = getSelectedDataRoot() != juce::File() ? getSelectedDataRoot() : dataRoot;
    const auto niceName = AuPluginScanner::displayNameForPluginId (pluginId);

    retrySkippedButton.setEnabled (false);
    retrySkippedButton.setButtonText ("Retrying...");

    juce::KnownPluginList localList;
    juce::KnownPluginList& list = knownPlugins != nullptr ? *knownPlugins : localList;

    juce::String error;
    const bool ok = AuPluginScanner::retrySkippedPlugin (root, pluginId, list, error);

    retrySkippedButton.setButtonText ("Retry selected");
    reloadSkippedList();

    if (! ok)
    {
        juce::AlertWindow alert ("Retry failed", error, juce::MessageBoxIconType::WarningIcon, this);
        alert.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert.runModalLoop();
        return;
    }

    pluginCacheModified = true;
    juce::AlertWindow alert ("Retry succeeded",
                             niceName + utf8 (" was scanned and added to the plugin cache.\n"
                                              "Use Plugins → Add Plugin… to put it on your list."),
                             juce::MessageBoxIconType::InfoIcon,
                             this);
    alert.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert.runModalLoop();
}

int SettingsPanel::getNumRows()
{
    return skippedIds.size();
}

void SettingsPanel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= skippedIds.size())
        return;

    if (rowIsSelected)
        g.fillAll (juce::Colours::lightblue.withAlpha (0.35f));

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (AuPluginScanner::displayNameForPluginId (skippedIds[rowNumber]),
                8, 0, width - 16, height,
                juce::Justification::centredLeft, true);
}

void SettingsPanel::selectedRowsChanged (int)
{
    updateRetryEnabled();
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    dataRootLabel.setBounds (area.removeFromTop (22));
    auto row = area.removeFromTop (28);
    chooseDataRootButton.setBounds (row.removeFromRight (88));
    row.removeFromRight (6);
    revealDataRootButton.setBounds (row.removeFromRight (72));
    row.removeFromRight (6);
    dataRootEditor.setBounds (row);
    area.removeFromTop (12);

    configLabel.setBounds (area.removeFromTop (22));
    row = area.removeFromTop (28);
    chooseConfigButton.setBounds (row.removeFromRight (88));
    row.removeFromRight (6);
    clearConfigButton.setBounds (row.removeFromRight (72));
    row.removeFromRight (6);
    configEditor.setBounds (row);
    area.removeFromTop (14);

    allowInstrumentInputToggle.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    auto timeoutRow = area.removeFromTop (28);
    scanTimeoutLabel.setBounds (timeoutRow.removeFromLeft (160));
    scanTimeoutBox.setBounds (timeoutRow.removeFromLeft (180));
    area.removeFromTop (12);

    skippedHeading.setBounds (area.removeFromTop (22));
    auto buttons = area.removeFromBottom (28);
    retrySkippedButton.setBounds (buttons.removeFromLeft (140));
    buttons.removeFromLeft (12);
    resetButton.setBounds (buttons.removeFromLeft (160));
    area.removeFromBottom (8);
    skippedList.setBounds (area);
}

juce::File SettingsPanel::getSelectedDataRoot() const
{
    return juce::File (dataRootEditor.getText().trim());
}

juce::File SettingsPanel::getSelectedConfigOverride() const
{
    const auto text = configEditor.getText().trim();
    return text.isEmpty() ? juce::File() : juce::File (text);
}

bool SettingsPanel::getAllowInstrumentAudioInput() const
{
    return allowInstrumentInputToggle.getToggleState();
}

int SettingsPanel::getPluginScanTimeoutMs() const
{
    const int selected = scanTimeoutBox.getSelectedId();
    if (selected > 0)
        return selected;

    return HostPreferences::defaultPluginScanTimeoutMs;
}

bool showSettingsDialog (HostConfig& config,
                         juce::KnownPluginList* knownPlugins,
                         juce::Component* centreAround)
{
    SettingsPanel panel (config, knownPlugins);
    panel.setSize (560, 450);

    if (HostDialog::runCustomPanelModal (
            "Settings",
            "Changing the exploration folder moves its data (including the AU plugin cache) to the new location. "
            "Folder/config override changes apply after relaunch.",
            panel,
            centreAround) != 1)
        return false;

    auto& prefs = HostPreferences::get();
    const auto previousDataRoot = config.projectRoot;

    if (panel.wantsResetToDefaults())
    {
        const auto defaultRoot = prefs.defaultExplorationDataRoot();
        juce::String migrateMessage;
        if (previousDataRoot != juce::File()
            && previousDataRoot.getFullPathName() != defaultRoot.getFullPathName())
        {
            if (! prefs.relocateExplorationData (previousDataRoot, defaultRoot, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not move exploration data",
                                                        migrateMessage);
                return false;
            }
        }

        prefs.clearPrefs();
        return true;
    }

    const auto dataRoot = panel.getSelectedDataRoot();
    if (dataRoot != juce::File())
    {
        if (! dataRoot.createDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Settings",
                                                    "Could not create exploration data folder:\n"
                                                        + dataRoot.getFullPathName());
            return false;
        }

        if (previousDataRoot != juce::File()
            && previousDataRoot.getFullPathName() != dataRoot.getFullPathName())
        {
            juce::String migrateMessage;
            if (! prefs.relocateExplorationData (previousDataRoot, dataRoot, migrateMessage))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not move exploration data",
                                                        migrateMessage);
                return false;
            }
        }

        prefs.setExplorationDataRootPref (dataRoot.getFullPathName());
    }

    const auto configOverride = panel.getSelectedConfigOverride();
    prefs.setConfigPathPref (configOverride != juce::File() ? configOverride.getFullPathName()
                                                            : juce::String());
    prefs.setAllowInstrumentAudioInput (panel.getAllowInstrumentAudioInput());
    prefs.setPluginScanTimeoutMs (panel.getPluginScanTimeoutMs());
    return true;
}
