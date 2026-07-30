#include "AddPluginDialog.h"
#include "HostDialog.h"
#include "Utf8.h"

AddPluginPanel::AddPluginPanel (const juce::KnownPluginList& list)
{
    cachedTypes = list.getTypes();

    filterLabel.setText ("Search", juce::dontSendNotification);
    addAndMakeVisible (filterLabel);
    filterEditor.setTextToShowWhenEmpty ("Name or manufacturer...", juce::Colours::grey);
    filterEditor.addListener (this);
    filterEditor.addKeyListener (this);
    addAndMakeVisible (filterEditor);

    listBox.setMultipleSelectionEnabled (true);
    listBox.setRowHeight (24);
    addAndMakeVisible (listBox);

    rebuildFilter();
}

AddPluginPanel::~AddPluginPanel()
{
    filterEditor.removeKeyListener (this);
    filterEditor.removeListener (this);
}

void AddPluginPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    auto filterRow = area.removeFromTop (28);
    filterLabel.setBounds (filterRow.removeFromLeft (56));
    filterEditor.setBounds (filterRow);
    area.removeFromTop (8);
    listBox.setBounds (area);
}

void AddPluginPanel::rebuildFilter()
{
    const auto query = filterEditor.getText().trim().toLowerCase();
    filteredIndices.clear();

    for (int i = 0; i < cachedTypes.size(); ++i)
    {
        const auto& desc = cachedTypes.getReference (i);
        if (query.isNotEmpty())
        {
            const auto hay = (desc.name + " " + desc.manufacturerName + " " + desc.fileOrIdentifier).toLowerCase();
            if (! hay.contains (query))
                continue;
        }
        filteredIndices.add (i);
    }

    listBox.updateContent();
    listBox.deselectAllRows();
}

int AddPluginPanel::getNumRows()
{
    return filteredIndices.size();
}

void AddPluginPanel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, filteredIndices.size()))
        return;

    const int typeIndex = filteredIndices.getUnchecked (rowNumber);
    if (! juce::isPositiveAndBelow (typeIndex, cachedTypes.size()))
        return;

    const auto& desc = cachedTypes.getReference (typeIndex);
    juce::String name = desc.name;
    if (desc.manufacturerName.isNotEmpty())
        name = desc.manufacturerName + " - " + desc.name;

    if (rowIsSelected)
        g.fillAll (juce::Colours::dodgerblue.withAlpha (0.35f));

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (14.0f);
    g.drawText (name, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
}

void AddPluginPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    acceptSelectionIfAny();
}

void AddPluginPanel::returnKeyPressed (int)
{
    acceptSelectionIfAny();
}

void AddPluginPanel::textEditorTextChanged (juce::TextEditor&)
{
    rebuildFilter();
}

void AddPluginPanel::acceptSelectionIfAny()
{
    if (listBox.getNumSelectedRows() <= 0)
        return;

    doubleClicked = true;
    if (auto* dw = findParentComponentOfClass<juce::AlertWindow>())
        dw->exitModalState (1);
}

bool AddPluginPanel::keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent)
{
    if (originatingComponent != &filterEditor)
        return false;

    if (key.isKeyCode (juce::KeyPress::returnKey))
    {
        if (listBox.getNumSelectedRows() > 0)
        {
            acceptSelectionIfAny();
            return true;
        }
        return false;
    }

    if (! key.isKeyCode (juce::KeyPress::downKey))
        return false;

    // Only leave the filter when the caret is already at the end of the text.
    if (filterEditor.getCaretPosition() < filterEditor.getText().length())
        return false;

    if (filteredIndices.isEmpty())
        return false;

    listBox.selectRow (0);
    listBox.grabKeyboardFocus();
    return true;
}

juce::Array<juce::PluginDescription> AddPluginPanel::getSelectedPlugins() const
{
    juce::Array<juce::PluginDescription> selected;
    auto rows = listBox.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
    {
        const int filteredRow = rows[i];
        if (! juce::isPositiveAndBelow (filteredRow, filteredIndices.size()))
            continue;

        const int typeIndex = filteredIndices.getUnchecked (filteredRow);
        if (juce::isPositiveAndBelow (typeIndex, cachedTypes.size()))
            selected.add (cachedTypes.getReference (typeIndex));
    }
    return selected;
}

bool showAddPluginDialog (HostConfig& config,
                          const juce::KnownPluginList& knownList,
                          juce::Component* centreAround,
                          juce::Array<int>& outAddedIndices)
{
    outAddedIndices.clear();

    if (knownList.getTypes().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                "Add Plugin",
                                                utf8 ("No Audio Units are in the cache. The scan may have been cancelled — try again, or use Plugins → Rescan Audio Units…"));
        return false;
    }

    AddPluginPanel panel (knownList);
    panel.setSize (560, 360);

    if (HostDialog::runCustomPanelModal (
            "Add Plugin",
            "Select one or more Audio Units to add to the plugin list.",
            panel,
            centreAround,
            "Add") != 1)
        return false;

    const auto selected = panel.getSelectedPlugins();
    if (selected.isEmpty())
        return false;

    for (const auto& desc : selected)
    {
        bool exists = false;
        for (const auto& plugin : config.plugins)
        {
            if (plugin.identifierForLoad() == desc.fileOrIdentifier
                || (plugin.name == desc.name && plugin.manufacturer == desc.manufacturerName))
            {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;

        HostPluginEntry entry;
        entry.name = desc.name;
        entry.manufacturer = desc.manufacturerName;
        entry.fileOrIdentifier = desc.fileOrIdentifier;
        entry.pluginFormatName = desc.pluginFormatName;
        entry.id = HostConfig::makeUniquePluginId (entry.name, config.plugins);
        entry.sessionName = entry.name + " exploration";

        // Prefer a real .component path when the scanner gave one; otherwise keep the AU id.
        const juce::File asFile (desc.fileOrIdentifier);
        if (asFile.exists())
        {
            entry.path = asFile;
            entry.installed = true;
        }
        else if (desc.fileOrIdentifier.startsWithIgnoreCase ("AudioUnit:")
                 || desc.pluginFormatName.containsIgnoreCase ("AudioUnit"))
        {
            entry.path = juce::File();
            entry.installed = true; // present in the AU registry / scan cache
        }
        else
        {
            entry.path = asFile;
            entry.installed = false;
        }

        if (entry.pluginFormatName.isEmpty() && entry.installed)
            entry.pluginFormatName = "AudioUnit";

        if (entry.manufacturer.isNotEmpty())
        {
            entry.presetsDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Library/Audio/Presets")
                                   .getChildFile (entry.manufacturer)
                                   .getChildFile (entry.name);
        }

        config.plugins.add (std::move (entry));
        outAddedIndices.add (config.plugins.size() - 1);
    }

    if (outAddedIndices.isEmpty())
        return false;

    if (config.defaultPluginId.isEmpty())
        config.defaultPluginId = config.plugins.getReference (outAddedIndices[0]).id;

    juce::String error;
    if (! config.saveToFile (error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Add Plugin",
                                                "Failed to save config: " + error);
        return false;
    }

    config.ensureSessions();
    return true;
}
