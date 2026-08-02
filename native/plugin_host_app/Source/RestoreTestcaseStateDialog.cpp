#include "RestoreTestcaseStateDialog.h"
#include "HostPreferences.h"
#include "MidiEndpointInfo.h"
#include "PresetHardwareState.h"
#include "Utf8.h"
#include "sysex/SysexDeviceModule.h"

namespace
{
    /** Truncate leading path components so the leaf directory name stays fully visible. */
    float measureTextWidth (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }

    juce::String ellipsizedPathKeepingLeaf (const juce::String& fullPath,
                                            const juce::Font& font,
                                            float maxWidth)
    {
        if (fullPath.isEmpty() || maxWidth <= 0.0f)
            return fullPath;

        if (measureTextWidth (font, fullPath) <= maxWidth)
            return fullPath;

        const juce::String ellipsis = utf8 ("…");
        const auto sep = juce::File::getSeparatorString();
        juce::StringArray parts;
        parts.addTokens (fullPath, sep, {});
        parts.removeEmptyStrings (true);

        if (parts.isEmpty())
            return fullPath;

        const juce::String leaf = parts.getReference (parts.size() - 1);
        if (measureTextWidth (font, ellipsis + sep + leaf) > maxWidth)
            return ellipsis + sep + leaf;

        for (int drop = 1; drop < parts.size(); ++drop)
        {
            juce::String rebuilt = ellipsis;
            for (int i = drop; i < parts.size(); ++i)
                rebuilt << sep << parts.getReference (i);

            if (measureTextWidth (font, rebuilt) <= maxWidth)
                return rebuilt;
        }

        return ellipsis + sep + leaf;
    }

    struct ArtifactFolderContents
    {
        juce::String stem;
        juce::File inputAudio;
        juce::File presetFile;
        juce::File sysexFile;
    };

    ArtifactFolderContents discoverArtifacts (const juce::File& folder)
    {
        ArtifactFolderContents out;
        out.stem = folder.getFileName();

        if (! folder.isDirectory())
            return out;

        if (out.stem.isNotEmpty())
        {
            const auto preferredInput = folder.getChildFile (out.stem + "_input.wav");
            if (preferredInput.existsAsFile())
                out.inputAudio = preferredInput;

            const auto preferredPreset = folder.getChildFile (out.stem + ".aupreset");
            if (preferredPreset.existsAsFile())
                out.presetFile = preferredPreset;

            const auto preferredSysex = folder.getChildFile (out.stem + ".syx");
            if (preferredSysex.existsAsFile())
                out.sysexFile = preferredSysex;
        }

        const auto children = folder.findChildFiles (juce::File::findFiles, false);
        for (const auto& file : children)
        {
            const auto name = file.getFileNameWithoutExtension();
            if (out.inputAudio == juce::File()
                && SourceClipLibrary::isSupportedAudioFile (file)
                && name.endsWithIgnoreCase ("_input"))
            {
                out.inputAudio = file;
            }
            else if (out.presetFile == juce::File()
                     && file.hasFileExtension ("aupreset"))
            {
                out.presetFile = file;
            }
            else if (out.sysexFile == juce::File()
                     && file.hasFileExtension ("syx"))
            {
                out.sysexFile = file;
            }
        }

        return out;
    }

    bool sysexDeviceConfigured (PluginAudioEngine& engine)
    {
        if (! engine.hasHardwareLoopConfigured())
            return false;

        const auto outId = HostPreferences::get().getMidiOutIdentifier();
        if (outId.isEmpty())
            return false;

        const auto info = findMidiEndpointInfo (outId, true);
        return resolveSelectedSysexModule (info) != nullptr;
    }

    //==============================================================================
    /** Dim non-directory rows so artifact files are visible but not primary targets. */
    class DimFilesLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawFileBrowserRow (juce::Graphics& g, int width, int height,
                                 const juce::File& file, const juce::String& filename,
                                 juce::Image* icon,
                                 const juce::String& fileSizeDescription,
                                 const juce::String& fileTimeDescription,
                                 bool isDirectory, bool isItemSelected,
                                 int itemIndex,
                                 juce::DirectoryContentsDisplayComponent& dcc) override
        {
            juce::Graphics::ScopedSaveState state (g);
            if (! isDirectory)
                g.setOpacity (0.4f);

            juce::LookAndFeel_V4::drawFileBrowserRow (g, width, height, file, filename, icon,
                                                      fileSizeDescription, fileTimeDescription,
                                                      isDirectory, isItemSelected, itemIndex, dcc);
        }
    };

    class FolderChoosePanel : public juce::Component,
                              private juce::FileBrowserListener
    {
    public:
        FolderChoosePanel (const juce::File& startDir,
                           std::function<void()> onChooseIn,
                           std::function<void()> onCancelIn)
            : browser (juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectDirectories
                           | juce::FileBrowserComponent::canSelectFiles,
                       startDir.isDirectory()
                           ? startDir
                           : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                       nullptr,
                       nullptr),
              onChoose (std::move (onChooseIn)),
              onCancel (std::move (onCancelIn))
        {
            browser.setLookAndFeel (&dimFilesLookAndFeel);
            browser.addListener (this);
            addAndMakeVisible (browser);

            openButton.setButtonText (utf8 ("Open"));
            openButton.setEnabled (false);
            openButton.onClick = [this] { openSelectedFolder(); };
            addAndMakeVisible (openButton);

            chooseButton.setButtonText (utf8 ("Choose"));
            chooseButton.onClick = [this]
            {
                if (onChoose)
                    onChoose();
            };
            addAndMakeVisible (chooseButton);

            cancelButton.setButtonText (utf8 ("Cancel"));
            cancelButton.addShortcut (juce::KeyPress (juce::KeyPress::escapeKey));
            cancelButton.onClick = [this]
            {
                if (onCancel)
                    onCancel();
            };
            addAndMakeVisible (cancelButton);

            pathDisplay.setReadOnly (true);
            pathDisplay.setCaretVisible (false);
            pathDisplay.setMultiLine (false);
            pathDisplay.setJustification (juce::Justification::centredLeft);
            addAndMakeVisible (pathDisplay);

            updateOpenEnabled();
            updatePathDisplay();

            setSize (560, 420);
        }

        ~FolderChoosePanel() override
        {
            browser.removeListener (this);
            browser.setLookAndFeel (nullptr);
        }

        juce::File getChosenDirectory() const
        {
            const auto selected = browser.getSelectedFile (0);
            if (selected.isDirectory())
                return selected;
            return browser.getRoot();
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto buttons = area.removeFromBottom (32);
            area.removeFromBottom (6);
            pathDisplay.setBounds (area.removeFromBottom (28));
            area.removeFromBottom (8);
            browser.setBounds (area);

            chooseButton.setBounds (buttons.removeFromRight (88));
            buttons.removeFromRight (8);
            openButton.setBounds (buttons.removeFromRight (88));
            buttons.removeFromRight (8);
            cancelButton.setBounds (buttons.removeFromRight (88));

            refreshEllipsizedPath();
        }

    private:
        void openSelectedFolder()
        {
            const auto selected = browser.getSelectedFile (0);
            if (selected.isDirectory())
                browser.setRoot (selected);
        }

        void updateOpenEnabled()
        {
            openButton.setEnabled (browser.getSelectedFile (0).isDirectory());
        }

        void updatePathDisplay()
        {
            fullPathForChoose = getChosenDirectory().getFullPathName();
            refreshEllipsizedPath();
        }

        void refreshEllipsizedPath()
        {
            const auto font = pathDisplay.getFont();
            const float maxW = (float) juce::jmax (40, pathDisplay.getWidth() - 12);
            pathDisplay.setText (ellipsizedPathKeepingLeaf (fullPathForChoose, font, maxW),
                                 juce::dontSendNotification);
            pathDisplay.setTooltip (fullPathForChoose);
        }

        void selectionChanged() override
        {
            updateOpenEnabled();
            updatePathDisplay();
        }

        void fileClicked (const juce::File&, const juce::MouseEvent&) override {}

        void fileDoubleClicked (const juce::File& file) override
        {
            if (file.isDirectory())
                browser.setRoot (file);
        }

        void browserRootChanged (const juce::File&) override
        {
            updateOpenEnabled();
            updatePathDisplay();
        }

        DimFilesLookAndFeel dimFilesLookAndFeel;
        juce::FileBrowserComponent browser;
        juce::TextButton openButton;
        juce::TextButton chooseButton;
        juce::TextButton cancelButton;
        juce::TextEditor pathDisplay;
        juce::String fullPathForChoose;
        std::function<void()> onChoose;
        std::function<void()> onCancel;
    };

    bool restoreArtifactFolder (const juce::File& folder,
                                PluginAudioEngine& engine,
                                TestCaseLoader& loader,
                                juce::ComboBox& sourceClipBox,
                                juce::String& error,
                                juce::String& outStem,
                                bool& outHardwareConfigured)
    {
        const auto artifacts = discoverArtifacts (folder);
        outStem = artifacts.stem.isNotEmpty() ? artifacts.stem : folder.getFileName();
        outHardwareConfigured = engine.hasHardwareLoopConfigured();

        if (artifacts.inputAudio == juce::File()
            && artifacts.presetFile == juce::File()
            && artifacts.sysexFile == juce::File())
        {
            error = utf8 ("No testcase artifacts found in:\n") + folder.getFullPathName();
            return false;
        }

        if (artifacts.inputAudio.existsAsFile())
        {
            if (! loader.loadSourceClipTemporaryTopLevel (artifacts.inputAudio, sourceClipBox, error))
                return false;
        }

        if (artifacts.presetFile.existsAsFile())
        {
            if (! loader.loadPreset (artifacts.presetFile, error))
                return false;
        }

        if (artifacts.sysexFile.existsAsFile() && sysexDeviceConfigured (engine))
        {
            if (! loader.sendSysex (artifacts.sysexFile, error))
                return false;
        }

        return true;
    }
}

bool showRestoreTestcaseStateDialog (HostConfig& config,
                                     PluginAudioEngine& engine,
                                     TestCaseLoader& loader,
                                     SourceClipLibrary& /*sourceClips*/,
                                     juce::ComboBox& sourceClipBox,
                                     const juce::File& startDirectory,
                                     juce::Component* centreAround)
{
    juce::File startDir = startDirectory;
    if (! startDir.isDirectory())
    {
        if (config.sessionsRoot.isDirectory())
            startDir = config.sessionsRoot;
        else
            startDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    juce::AlertWindow window (utf8 ("Restore Testcase State"),
                              utf8 ("Select a capture folder."),
                              juce::MessageBoxIconType::NoIcon,
                              centreAround);

    int modalResult = 0;
    FolderChoosePanel panel (startDir,
                             [&window, &modalResult]
                             {
                                 modalResult = 1;
                                 window.exitModalState (1);
                             },
                             [&window, &modalResult]
                             {
                                 modalResult = 0;
                                 window.exitModalState (0);
                             });

    window.addCustomComponent (&panel);
    window.setEscapeKeyCancels (true);

    const int result = window.runModalLoop();
    if (result != 1 && modalResult != 1)
        return false;

    const auto chosen = panel.getChosenDirectory();
    if (! chosen.isDirectory())
        return false;

    juce::String error;
    juce::String stem;
    bool hardwareConfigured = false;
    if (! restoreArtifactFolder (chosen, engine, loader, sourceClipBox, error, stem, hardwareConfigured))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                utf8 ("Restore Testcase State"),
                                                error,
                                                utf8 ("OK"),
                                                centreAround);
        return false;
    }

    juce::String message = stem + utf8 (" state restored.");
    if (hardwareConfigured)
        message += utf8 ("\nBe sure to adjust any physical gain controls appropriately.");

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                            utf8 ("Restore Testcase State"),
                                            message,
                                            utf8 ("OK"),
                                            centreAround);
    return true;
}
