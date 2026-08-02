#include "AudioUnitSettingsDialog.h"
#include "AuPluginScanner.h"
#include "HostDialog.h"
#include "HostPreferences.h"
#include "Utf8.h"

namespace
{
    class AudioUnitSettingsPanel : public juce::Component,
                                   private juce::ListBoxModel
    {
    public:
        AudioUnitSettingsPanel (const HostConfig& config, juce::KnownPluginList* knownPluginsIn)
            : dataRoot (config.projectRoot),
              knownPlugins (knownPluginsIn)
        {
            allowInstrumentInputToggle.setButtonText (utf8 ("Allow input to virtual instruments"));
            allowInstrumentInputToggle.setTooltip (
                utf8 ("Enable audio input buses on instruments/samplers and feed the source clip into them "
                      "(for sampling). Off by default — some AUs crash if input is enabled without a proper feed."));
            allowInstrumentInputToggle.setToggleState (HostPreferences::get().getAllowInstrumentAudioInput(),
                                                       juce::dontSendNotification);
            addAndMakeVisible (allowInstrumentInputToggle);

            scanTimeoutLabel.setText (utf8 ("Plugin scan timeout"), juce::dontSendNotification);
            addAndMakeVisible (scanTimeoutLabel);
            scanTimeoutBox.addItem (utf8 ("15 seconds"), HostPreferences::defaultPluginScanTimeoutMs);
            scanTimeoutBox.addItem (utf8 ("30 seconds"), 30000);
            scanTimeoutBox.addItem (utf8 ("1 minute"), 60000);
            scanTimeoutBox.addItem (utf8 ("2 minutes"), 120000);
            scanTimeoutBox.addItem (utf8 ("5 minutes"), HostPreferences::maxPluginScanTimeoutMs);
            scanTimeoutBox.setTooltip (
                utf8 ("How long each Audio Unit may take to respond during scan or Retry selected. "
                      "Increase this if plugins time out under load; applies to the next scan or retry."));
            scanTimeoutBox.setSelectedId (HostPreferences::get().getPluginScanTimeoutMs(),
                                          juce::dontSendNotification);
            addAndMakeVisible (scanTimeoutBox);

            skippedHeading.setText (utf8 ("Skipped AU plugins (crashed / hung during scan)"),
                                    juce::dontSendNotification);
            addAndMakeVisible (skippedHeading);

            skippedList.setRowHeight (22);
            skippedList.setMultipleSelectionEnabled (false);
            skippedList.setColour (juce::ListBox::backgroundColourId, juce::Colours::black.withAlpha (0.15f));
            skippedList.setColour (juce::ListBox::outlineColourId, juce::Colours::grey.withAlpha (0.4f));
            skippedList.setOutlineThickness (1);
            addAndMakeVisible (skippedList);

            retrySkippedButton.setButtonText (utf8 ("Retry selected"));
            retrySkippedButton.setTooltip (
                utf8 ("Rescan only the selected plugin and add it to the cache if it succeeds."));
            retrySkippedButton.setEnabled (false);
            addAndMakeVisible (retrySkippedButton);

            retrySkippedButton.onClick = [this] { retrySelectedSkippedPlugin(); };

            reloadSkippedList();
            setSize (560, 360);
        }

        bool getAllowInstrumentAudioInput() const
        {
            return allowInstrumentInputToggle.getToggleState();
        }

        int getPluginScanTimeoutMs() const
        {
            const int selected = scanTimeoutBox.getSelectedId();
            if (selected > 0)
                return selected;
            return HostPreferences::defaultPluginScanTimeoutMs;
        }

        bool didModifyPluginCache() const { return pluginCacheModified; }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4);
            allowInstrumentInputToggle.setBounds (area.removeFromTop (28));
            area.removeFromTop (8);

            auto timeoutRow = area.removeFromTop (28);
            scanTimeoutLabel.setBounds (timeoutRow.removeFromLeft (160));
            scanTimeoutBox.setBounds (timeoutRow.removeFromLeft (180));
            area.removeFromTop (12);

            skippedHeading.setBounds (area.removeFromTop (22));
            auto buttons = area.removeFromBottom (28);
            retrySkippedButton.setBounds (buttons.removeFromLeft (140));
            area.removeFromBottom (8);
            skippedList.setBounds (area);
        }

    private:
        void reloadSkippedList()
        {
            skippedIds = AuPluginScanner::loadSkipList (dataRoot);
            skippedIds.removeEmptyStrings();
            skippedList.updateContent();
            skippedList.deselectAllRows();
            updateRetryEnabled();
        }

        void updateRetryEnabled()
        {
            retrySkippedButton.setEnabled (skippedList.getSelectedRow() >= 0
                                           && skippedList.getSelectedRow() < skippedIds.size());
        }

        void retrySelectedSkippedPlugin()
        {
            const int row = skippedList.getSelectedRow();
            if (row < 0 || row >= skippedIds.size())
                return;

            const auto pluginId = skippedIds[row];
            const auto niceName = AuPluginScanner::displayNameForPluginId (pluginId);

            retrySkippedButton.setEnabled (false);
            retrySkippedButton.setButtonText (utf8 ("Retrying..."));

            juce::KnownPluginList localList;
            juce::KnownPluginList& list = knownPlugins != nullptr ? *knownPlugins : localList;

            juce::String error;
            const bool ok = AuPluginScanner::retrySkippedPlugin (dataRoot, pluginId, list, error);

            retrySkippedButton.setButtonText (utf8 ("Retry selected"));
            reloadSkippedList();

            if (! ok)
            {
                juce::AlertWindow alert (utf8 ("Retry failed"), error,
                                         juce::MessageBoxIconType::WarningIcon, this);
                alert.addButton (utf8 ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
                alert.runModalLoop();
                return;
            }

            pluginCacheModified = true;
            juce::AlertWindow alert (utf8 ("Retry succeeded"),
                                     niceName + utf8 (" was scanned and added to the plugin cache.\n"
                                                      "Use Plugins → Add Plugin… to put it on your list."),
                                     juce::MessageBoxIconType::InfoIcon,
                                     this);
            alert.addButton (utf8 ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            alert.runModalLoop();
        }

        int getNumRows() override { return skippedIds.size(); }

        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                               bool rowIsSelected) override
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

        void selectedRowsChanged (int) override { updateRetryEnabled(); }

        juce::File dataRoot;
        juce::KnownPluginList* knownPlugins { nullptr };
        juce::StringArray skippedIds;
        juce::ToggleButton allowInstrumentInputToggle;
        juce::Label scanTimeoutLabel;
        juce::ComboBox scanTimeoutBox;
        juce::Label skippedHeading;
        juce::ListBox skippedList { "skipped", this };
        juce::TextButton retrySkippedButton;
        bool pluginCacheModified { false };
    };
}

bool showAudioUnitSettingsDialog (HostConfig& config,
                                  juce::KnownPluginList* knownPlugins,
                                  juce::Component* centreAround)
{
    AudioUnitSettingsPanel panel (config, knownPlugins);

    if (HostDialog::runCustomPanelModal (
            utf8 ("Audio Unit Settings"),
            utf8 ("Hosting preferences for Audio Units. Instrument input applies after Save "
                  "(reload the plugin if one is already open)."),
            panel,
            centreAround) != 1)
        return false;

    auto& prefs = HostPreferences::get();
    prefs.setAllowInstrumentAudioInput (panel.getAllowInstrumentAudioInput());
    prefs.setPluginScanTimeoutMs (panel.getPluginScanTimeoutMs());
    juce::ignoreUnused (panel.didModifyPluginCache());
    return true;
}
