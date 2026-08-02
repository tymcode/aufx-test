#include "InstallSourceClipsDialog.h"
#include "HostDialog.h"
#include "HostFileUtils.h"
#include "HostPreferences.h"
#include "PluginAudioEngine.h"
#include "Utf8.h"

namespace
{
    bool directoryLooksWritable (const juce::File& dir)
    {
        if (! dir.isDirectory())
            return false;

        const auto bundled = HostPreferences::get().bundledFixturesDir();
        if (bundled.isDirectory()
            && dir.getFullPathName() == bundled.getFullPathName())
            return false;

        auto probe = dir.getNonexistentChildFile ("aufx_write_probe", ".tmp");
        if (! probe.create())
            return false;

        probe.deleteFile();
        return true;
    }

    bool directoryHasContent (const juce::File& dir)
    {
        return dir.isDirectory()
               && dir.findChildFiles (juce::File::findFilesAndDirectories, false).size() > 0;
    }

    juce::String basenameNoExtension (const juce::File& file)
    {
        return file.getFileNameWithoutExtension();
    }

    /** Loop-preview one WAV via the host engine; restores prior fixture on stop. */
    class ClipPreviewController
    {
    public:
        explicit ClipPreviewController (PluginAudioEngine& engineIn) : engine (engineIn) {}
        ~ClipPreviewController() { stop(); }

        void toggle (const juce::File& file)
        {
            if (previewFile == file && engine.isPlaying())
            {
                stop();
                return;
            }
            start (file);
        }

        bool isPreviewing (const juce::File& file) const
        {
            return previewFile == file && engine.isPlaying();
        }

        void stop()
        {
            if (! savedState)
                return;

            engine.stopFixture();

            if (previousFixture.existsAsFile())
            {
                juce::String error;
                engine.loadFixture (previousFixture, error);
            }

            engine.setLooping (wasLooping);
            if (wasPlaying)
                engine.playFixture();

            savedState = false;
            previewFile = juce::File();
        }

    private:
        void start (const juce::File& file)
        {
            if (! savedState)
            {
                previousFixture = engine.getCurrentFixtureFile();
                wasLooping = engine.isLooping();
                wasPlaying = engine.isPlaying();
                savedState = true;
            }

            juce::String error;
            if (! engine.loadFixture (file, error))
            {
                stop();
                return;
            }

            engine.setLooping (true);
            engine.playFixture();
            previewFile = file;
        }

        PluginAudioEngine& engine;
        juce::File previousFixture;
        juce::File previewFile;
        bool wasLooping { true };
        bool wasPlaying { false };
        bool savedState { false };
    };

    class FolderTreeItem : public juce::TreeViewItem
    {
    public:
        FolderTreeItem (juce::File folderIn, bool isRootIn)
            : folder (std::move (folderIn)), isRoot (isRootIn)
        {
        }

        bool mightContainSubItems() override
        {
            return folder.findChildFiles (juce::File::findDirectories, false).size() > 0;
        }

        void itemOpennessChanged (bool isNowOpen) override
        {
            if (isNowOpen && getNumSubItems() == 0)
            {
                auto children = folder.findChildFiles (juce::File::findDirectories, false);
                HostFileUtils::sortFilesByName (children);
                for (const auto& child : children)
                    addSubItem (new FolderTreeItem (child, false));
            }
            else if (! isNowOpen)
            {
                clearSubItems();
            }
        }

        void paintItem (juce::Graphics& g, int width, int height) override
        {
            if (isSelected())
                g.fillAll (juce::Colours::white.withAlpha (0.12f));

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (13.0f));
            const auto label = isRoot ? folder.getFileName() + utf8 (" (fixtures root)")
                                      : folder.getFileName();
            g.drawText (label, 4, 0, width - 4, height, juce::Justification::centredLeft, true);
        }

        juce::String getUniqueName() const override { return folder.getFullPathName(); }
        juce::File getFolder() const { return folder; }

    private:
        juce::File folder;
        bool isRoot { false };
    };

    //==============================================================================
    class WavBrowseColumn : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Timer
    {
    public:
        WavBrowseColumn (PluginAudioEngine& engineIn, std::function<void()> selectionChangedIn)
            : preview (engineIn),
              onSelectionChanged (std::move (selectionChangedIn))
        {
            goUpButton.setButtonText (utf8 ("↑"));
            goUpButton.setTooltip (utf8 ("Go to parent folder"));
            goUpButton.onClick = [this]
            {
                if (currentDir.getParentDirectory().isDirectory()
                    && currentDir.getParentDirectory() != currentDir)
                    setDirectory (currentDir.getParentDirectory());
            };
            addAndMakeVisible (goUpButton);

            pathBox.setEditableText (false);
            pathBox.onChange = [this]
            {
                const int id = pathBox.getSelectedId();
                if (id > 0 && id <= pathChoices.size())
                    setDirectory (pathChoices.getReference (id - 1));
            };
            addAndMakeVisible (pathBox);

            fileList.setModel (this);
            fileList.setMultipleSelectionEnabled (true);
            fileList.setRowHeight (26);
            addAndMakeVisible (fileList);

            selectionEditor.setMultiLine (false);
            selectionEditor.setReadOnly (true);
            selectionEditor.setCaretVisible (false);
            selectionEditor.setTextToShowWhenEmpty (utf8 ("No files selected"),
                                                    juce::Colours::grey);
            addAndMakeVisible (selectionEditor);

            startTimerHz (8);
        }

        ~WavBrowseColumn() override
        {
            stopTimer();
            preview.stop();
        }

        void setDirectory (juce::File dir)
        {
            if (! dir.isDirectory())
                return;

            preview.stop();
            currentDir = std::move (dir);
            HostPreferences::get().setLastSourceClipBrowseDir (currentDir);
            rebuildPathBox();
            rescanFiles();
            fileList.deselectAllRows();
            updateSelectionEditor();
            goUpButton.setEnabled (currentDir.getParentDirectory().isDirectory()
                                   && currentDir.getParentDirectory() != currentDir);
        }

        juce::File getDirectory() const { return currentDir; }

        juce::Array<juce::File> getSelectedWavFiles() const
        {
            juce::Array<juce::File> files;
            const auto selected = fileList.getSelectedRows();
            for (int i = 0; i < selected.size(); ++i)
            {
                const int row = selected[i];
                if (juce::isPositiveAndBelow (row, wavFiles.size()))
                    files.add (wavFiles.getReference (row));
            }
            return files;
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto pathRow = area.removeFromTop (28);
            goUpButton.setBounds (pathRow.removeFromLeft (32));
            pathRow.removeFromLeft (4);
            pathBox.setBounds (pathRow);

            area.removeFromTop (6);
            selectionEditor.setBounds (area.removeFromBottom (28));
            area.removeFromBottom (6);
            fileList.setBounds (area);
        }

    private:
        class RowComponent : public juce::Component
        {
        public:
            RowComponent (WavBrowseColumn& ownerIn) : owner (ownerIn)
            {
                playButton.setButtonText ({});
                playButton.setTooltip (utf8 ("Loop preview"));
                playButton.setClickingTogglesState (true);
                playButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
                playButton.setColour (juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
                playButton.setColour (juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
                playButton.onClick = [this]
                {
                    if (file.existsAsFile())
                        owner.preview.toggle (file);
                    owner.fileList.updateContent();
                    owner.fileList.repaint();
                };
                addAndMakeVisible (playButton);

                nameLabel.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (nameLabel);
            }

            void setFile (const juce::File& f, bool rowSelected)
            {
                file = f;
                selected = rowSelected;
                nameLabel.setText (basenameNoExtension (file), juce::dontSendNotification);
                nameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
                nameLabel.setColour (juce::Label::textColourId,
                                     selected ? juce::Colours::white
                                              : juce::Colours::white.withAlpha (0.9f));
                refreshPlayState();
                repaint();
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (2, 1);
                playButton.setBounds (area.removeFromLeft (24));
                area.removeFromLeft (4);
                nameLabel.setBounds (area);
            }

            void paint (juce::Graphics& g) override
            {
                if (selected)
                    g.fillAll (juce::Colours::white.withAlpha (0.14f));
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                const bool playing = owner.preview.isPreviewing (file);
                auto r = playButton.getBounds().toFloat().reduced (5.0f);
                g.setColour (juce::Colours::white.withAlpha (playing ? 1.0f : 0.85f));
                if (playing)
                {
                    g.fillRect (r.reduced (2.0f));
                }
                else
                {
                    juce::Path triangle;
                    triangle.addTriangle (r.getX(), r.getY(),
                                          r.getX(), r.getBottom(),
                                          r.getRight(), r.getCentreY());
                    g.fillPath (triangle);
                }
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (playButton.getBounds().contains (e.getPosition()))
                    return;

                owner.fileList.selectRowsBasedOnModifierKeys (
                    owner.wavFiles.indexOf (file), e.mods, true);
            }

        private:
            void refreshPlayState()
            {
                playButton.setToggleState (owner.preview.isPreviewing (file),
                                           juce::dontSendNotification);
                repaint();
            }

            WavBrowseColumn& owner;
            juce::File file;
            bool selected { false };
            juce::TextButton playButton;
            juce::Label nameLabel;
        };

        void rebuildPathBox()
        {
            pathChoices.clearQuick();
            pathBox.clear (juce::dontSendNotification);

            juce::Array<juce::File> chain;
            for (auto dir = currentDir; dir.isDirectory(); dir = dir.getParentDirectory())
            {
                chain.add (dir);
                if (dir.getParentDirectory() == dir)
                    break;
            }

            // Root → … → current in the menu; display text is basename only.
            for (int i = chain.size(); --i >= 0;)
            {
                pathChoices.add (chain.getReference (i));
                auto name = chain.getReference (i).getFileName();
                if (name.isEmpty())
                    name = juce::File::getSeparatorString();
                pathBox.addItem (name, pathChoices.size());
            }

            pathBox.setSelectedId (pathChoices.size(), juce::dontSendNotification);
            // Ensure the closed combo shows basename, not a stale full path.
            const juce::String closedLabel = currentDir.getFileName().isNotEmpty()
                                                ? currentDir.getFileName()
                                                : juce::String (juce::File::getSeparatorString());
            pathBox.setText (closedLabel, juce::dontSendNotification);
        }

        void rescanFiles()
        {
            wavFiles.clearQuick();
            if (currentDir.isDirectory())
            {
                for (const auto& file : HostFileUtils::collectFiles (currentDir, ".wav", false))
                    wavFiles.add (file);
            }
            fileList.updateContent();
            fileList.repaint();
        }

        void updateSelectionEditor()
        {
            juce::StringArray names;
            for (const auto& file : getSelectedWavFiles())
                names.add (basenameNoExtension (file));
            selectionEditor.setText (names.joinIntoString (", "), juce::dontSendNotification);
            if (onSelectionChanged)
                onSelectionChanged();
        }

        int getNumRows() override { return wavFiles.size(); }

        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

        juce::Component* refreshComponentForRow (int row,
                                                 bool isSelected,
                                                 juce::Component* existing) override
        {
            auto* rowComp = existing != nullptr ? dynamic_cast<RowComponent*> (existing)
                                                : nullptr;
            if (rowComp == nullptr)
            {
                delete existing;
                rowComp = new RowComponent (*this);
            }

            if (juce::isPositiveAndBelow (row, wavFiles.size()))
                rowComp->setFile (wavFiles.getReference (row), isSelected);
            return rowComp;
        }

        void selectedRowsChanged (int) override
        {
            // Custom row components only learn selection via refreshComponentForRow.
            fileList.updateContent();
            updateSelectionEditor();
        }

        void timerCallback() override
        {
            // Keep play-toggle glyphs in sync if preview stops externally.
            fileList.repaint();
        }

        ClipPreviewController preview;
        std::function<void()> onSelectionChanged;
        juce::File currentDir;
        juce::Array<juce::File> wavFiles;
        juce::Array<juce::File> pathChoices;
        juce::TextButton goUpButton;
        juce::ComboBox pathBox;
        juce::ListBox fileList;
        juce::TextEditor selectionEditor;
    };

    //==============================================================================
    class InstallSourceClipsPanel : public juce::Component
    {
    public:
        InstallSourceClipsPanel (HostConfig& hostConfig, PluginAudioEngine& engineIn)
            : config (hostConfig),
              wavBrowser (engineIn, [] {})
        {
            leftHeading.setText (utf8 ("Select one or more WAV files."), juce::dontSendNotification);
            leftHeading.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (leftHeading);

            rightHeading.setText (utf8 ("Select a target folder."), juce::dontSendNotification);
            rightHeading.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (rightHeading);

            auto startDir = HostPreferences::get().getLastSourceClipBrowseDir();
            if (! startDir.isDirectory())
            {
                if (config.fixturesDir.isDirectory())
                    startDir = config.fixturesDir;
                else
                    startDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            }
            wavBrowser.setDirectory (startDir);
            addAndMakeVisible (wavBrowser);

            folderTree.setRootItemVisible (true);
            folderTree.setDefaultOpenness (true);
            folderTree.setMultiSelectEnabled (false);
            addAndMakeVisible (folderTree);

            newFolderButton.setButtonText (utf8 ("New Folder…"));
            newFolderButton.setTooltip (utf8 ("Create a subfolder under the selected target folder"));
            newFolderButton.onClick = [this] { createNewFolder(); };
            addAndMakeVisible (newFolderButton);

            rebuildFolderTree();
            setSize (820, 480);
        }

        ~InstallSourceClipsPanel() override
        {
            folderTree.deleteRootItem();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4);
            auto headings = area.removeFromTop (22);
            const int gap = 12;
            const int half = (headings.getWidth() - gap) / 2;
            leftHeading.setBounds (headings.removeFromLeft (half));
            headings.removeFromLeft (gap);
            rightHeading.setBounds (headings);

            area.removeFromTop (6);
            auto body = area;
            auto left = body.removeFromLeft ((body.getWidth() - gap) / 2);
            body.removeFromLeft (gap);
            auto right = body;

            wavBrowser.setBounds (left);

            auto rightButtons = right.removeFromBottom (28);
            right.removeFromBottom (6);
            newFolderButton.setBounds (rightButtons.removeFromLeft (120));
            folderTree.setBounds (right);
        }

        juce::Array<juce::File> getSelectedWavFiles() const
        {
            return wavBrowser.getSelectedWavFiles();
        }

        juce::File getSelectedTargetFolder() const
        {
            if (auto* item = dynamic_cast<FolderTreeItem*> (folderTree.getSelectedItem (0)))
                return item->getFolder();
            if (auto* root = dynamic_cast<FolderTreeItem*> (folderTree.getRootItem()))
                return root->getFolder();
            return config.fixturesDir;
        }

        juce::File getCurrentBrowseDirectory() const { return wavBrowser.getDirectory(); }

        void rebuildFolderTree()
        {
            folderTree.deleteRootItem();
            if (! config.fixturesDir.isDirectory())
                config.fixturesDir.createDirectory();

            auto* root = new FolderTreeItem (config.fixturesDir, true);
            folderTree.setRootItem (root);
            root->setOpen (true);
            root->setSelected (true, true);
        }

        bool ensureFixturesWritable (juce::Component* centreAround, juce::String& error)
        {
            if (directoryLooksWritable (config.fixturesDir))
                return true;

            auto chooser = juce::FileChooser (utf8 ("Select a folder for the collection."),
                                              config.projectRoot.isDirectory()
                                                  ? config.projectRoot
                                                  : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                              "*");
            if (! chooser.browseForDirectory())
            {
                error = utf8 ("Installation cancelled — Source Clips folder is not writable.");
                return false;
            }

            const auto newFixtures = chooser.getResult();
            if (newFixtures == juce::File())
            {
                error = utf8 ("No collection folder selected.");
                return false;
            }

            if (! newFixtures.createDirectory() && ! newFixtures.isDirectory())
            {
                error = utf8 ("Could not create collection folder:\n") + newFixtures.getFullPathName();
                return false;
            }

            auto& prefs = HostPreferences::get();
            const auto previous = config.fixturesDir;
            const auto bundled = prefs.bundledFixturesDir();
            const bool previousIsBundled = previous.isDirectory()
                                           && bundled.isDirectory()
                                           && previous.getFullPathName() == bundled.getFullPathName();

            juce::String migrateMessage;
            if (previous.isDirectory() && directoryHasContent (previous) && ! previousIsBundled)
            {
                if (! prefs.relocateDirectoryContents (previous, newFixtures, migrateMessage))
                {
                    error = migrateMessage;
                    return false;
                }
            }
            else if (bundled.isDirectory())
            {
                if (! prefs.copyDirectoryContents (bundled, newFixtures, migrateMessage))
                {
                    error = migrateMessage;
                    return false;
                }
            }

            config.fixturesDir = newFixtures;
            juce::String saveError;
            if (! config.saveToFile (saveError))
            {
                error = utf8 ("Moved collection, but failed to write host.config.json:\n") + saveError;
                return false;
            }

            rebuildFolderTree();
            juce::ignoreUnused (centreAround);
            return true;
        }

        bool installSelected (juce::Component* centreAround, juce::String& error, bool& relocated)
        {
            relocated = false;
            const auto beforeFixtures = config.fixturesDir.getFullPathName();

            if (! ensureFixturesWritable (centreAround, error))
                return false;

            if (config.fixturesDir.getFullPathName() != beforeFixtures)
                relocated = true;

            const auto sources = getSelectedWavFiles();
            if (sources.isEmpty())
            {
                error = utf8 ("Select one or more WAV files.");
                return false;
            }

            auto target = getSelectedTargetFolder();
            if (! target.isDirectory())
            {
                error = utf8 ("Select a target folder.");
                return false;
            }

            if (! target.isAChildOf (config.fixturesDir)
                && target.getFullPathName() != config.fixturesDir.getFullPathName())
            {
                error = utf8 ("Target folder must be inside the Source Clips collection.");
                return false;
            }

            int installed = 0;
            int failed = 0;
            for (const auto& src : sources)
            {
                const auto dest = target.getChildFile (src.getFileName());
                if (src.getFullPathName() == dest.getFullPathName())
                {
                    ++installed;
                    continue;
                }

                if (dest.existsAsFile() && ! dest.deleteFile())
                {
                    ++failed;
                    continue;
                }

                if (src.moveFileTo (dest) || (src.copyFileTo (dest) && src.deleteFile()))
                    ++installed;
                else if (src.copyFileTo (dest))
                    ++installed;
                else
                    ++failed;
            }

            if (installed == 0)
            {
                error = utf8 ("Could not install WAV file(s) into:\n") + target.getFullPathName();
                return false;
            }

            if (failed > 0)
                error = utf8 ("Installed ") + juce::String (installed)
                        + utf8 (" file(s); ") + juce::String (failed) + utf8 (" failed.");
            else
                error = utf8 ("Installed ") + juce::String (installed) + utf8 (" file(s) into ")
                        + target.getFileName() + ".";

            return true;
        }

    private:
        void createNewFolder()
        {
            juce::String ensureError;
            const auto before = config.fixturesDir.getFullPathName();
            if (! ensureFixturesWritable (getTopLevelComponent(), ensureError))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("Install New Source Clips"),
                                                        ensureError);
                return;
            }
            if (config.fixturesDir.getFullPathName() != before)
                rebuildFolderTree();

            auto parent = getSelectedTargetFolder();
            if (! parent.isDirectory())
                parent = config.fixturesDir;

            juce::AlertWindow dialog (utf8 ("New Folder"),
                                      utf8 ("Name for the new folder under \"")
                                          + parent.getFileName() + "\":",
                                      juce::MessageBoxIconType::QuestionIcon,
                                      this);
            dialog.addTextEditor ("name", utf8 ("New Clips"), utf8 ("Folder name"));
            dialog.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            dialog.addButton (utf8 ("Create"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            if (auto* cancel = dialog.getButton (utf8 ("Cancel")))
                cancel->setWantsKeyboardFocus (false);

            if (dialog.runModalLoop() != 1)
                return;

            auto name = juce::File::createLegalFileName (dialog.getTextEditorContents ("name").trim());
            if (name.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("New Folder"),
                                                        utf8 ("Enter a folder name."));
                return;
            }

            const auto created = parent.getChildFile (name);
            if (! created.createDirectory())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        utf8 ("New Folder"),
                                                        utf8 ("Could not create:\n") + created.getFullPathName());
                return;
            }

            rebuildFolderTree();
            selectFolderInTree (created);
        }

        void selectFolderInTree (const juce::File& folder)
        {
            struct Finder
            {
                static FolderTreeItem* find (juce::TreeViewItem* item, const juce::File& target)
                {
                    if (auto* folderItem = dynamic_cast<FolderTreeItem*> (item))
                    {
                        if (folderItem->getFolder().getFullPathName() == target.getFullPathName())
                            return folderItem;

                        folderItem->setOpen (true);
                        for (int i = 0; i < folderItem->getNumSubItems(); ++i)
                            if (auto* found = find (folderItem->getSubItem (i), target))
                                return found;
                    }
                    return nullptr;
                }
            };

            if (auto* found = Finder::find (folderTree.getRootItem(), folder))
                found->setSelected (true, true);
        }

        HostConfig& config;
        juce::Label leftHeading;
        juce::Label rightHeading;
        WavBrowseColumn wavBrowser;
        juce::TreeView folderTree;
        juce::TextButton newFolderButton;
    };
}

bool showInstallSourceClipsDialog (HostConfig& config,
                                   PluginAudioEngine& engine,
                                   juce::Component* centreAround,
                                   bool* outFixturesRelocated)
{
    InstallSourceClipsPanel panel (config, engine);
    bool anyRelocation = false;

    for (;;)
    {
        const int result = HostDialog::runCustomPanelModal (utf8 ("Install New Source Clips"),
                                                            {},
                                                            panel,
                                                            centreAround,
                                                            utf8 ("Install"),
                                                            true);
        if (result != 1)
        {
            const auto browseDir = panel.getCurrentBrowseDirectory();
            if (browseDir.isDirectory())
                HostPreferences::get().setLastSourceClipBrowseDir (browseDir);
            if (outFixturesRelocated != nullptr)
                *outFixturesRelocated = anyRelocation;
            return false;
        }

        juce::String message;
        bool relocated = false;
        if (! panel.installSelected (centreAround, message, relocated))
        {
            anyRelocation = anyRelocation || relocated;
            juce::AlertWindow::showMessageBox (juce::MessageBoxIconType::WarningIcon,
                                               utf8 ("Install New Source Clips"),
                                               message,
                                               utf8 ("OK"),
                                               centreAround);
            continue;
        }

        anyRelocation = anyRelocation || relocated;

        const auto browseDir = panel.getCurrentBrowseDirectory();
        if (browseDir.isDirectory())
            HostPreferences::get().setLastSourceClipBrowseDir (browseDir);

        if (outFixturesRelocated != nullptr)
            *outFixturesRelocated = anyRelocation;

        juce::AlertWindow::showMessageBox (juce::MessageBoxIconType::InfoIcon,
                                           utf8 ("Install New Source Clips"),
                                           message,
                                           utf8 ("OK"),
                                           centreAround);
        return true;
    }
}
