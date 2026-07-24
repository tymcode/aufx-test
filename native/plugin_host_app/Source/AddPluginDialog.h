#pragma once

#include <JuceHeader.h>
#include "HostConfig.h"

/** Searchable picker of cached Audio Units; returns selected PluginDescriptions. */
class AddPluginPanel : public juce::Component,
                       private juce::ListBoxModel,
                       private juce::TextEditor::Listener,
                       private juce::KeyListener
{
public:
    explicit AddPluginPanel (const juce::KnownPluginList& list);
    ~AddPluginPanel() override;

    void resized() override;

    juce::Array<juce::PluginDescription> getSelectedPlugins() const;

private:
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int lastRowSelected) override;
    void textEditorTextChanged (juce::TextEditor&) override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void rebuildFilter();
    void acceptSelectionIfAny();

    const juce::KnownPluginList& knownList;
    juce::Array<int> filteredIndices;
    juce::Label filterLabel;
    juce::TextEditor filterEditor;
    juce::ListBox listBox { "plugins", this };
    bool doubleClicked { false };

public:
    bool wasDoubleClicked() const { return doubleClicked; }
};

/**
 * Shows Add Plugin dialog. On success, appends entries to config, saves, and
 * returns indices of newly added plugins in config.plugins.
 */
bool showAddPluginDialog (HostConfig& config,
                          const juce::KnownPluginList& knownList,
                          juce::Component* centreAround,
                          juce::Array<int>& outAddedIndices);
