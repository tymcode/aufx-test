#include "ComparisonReportDialog.h"
#include "Utf8.h"
#include "CapturePipeline.h"
#include "SessionSnap.h"
#include "../formats/Qdv1StateIO.h"
#include "../domain/AlesisCodec.h"

namespace qverse
{

namespace
{

constexpr int kMaxComparePatches = 4;

struct CompareCandidate
{
    int contextIndex = -1;
    juce::String name;
};

class ContextToggleList : public juce::Component
{
public:
    explicit ContextToggleList (const juce::Array<CompareCandidate>& candidates)
    {
        for (const auto& c : candidates)
        {
            auto* tb = toggles.add (new juce::ToggleButton (c.name));
            tb->setToggleState (true, juce::dontSendNotification);
            tb->onClick = [this]
            {
                if (onSelectionChanged)
                    onSelectionChanged();
            };
            contextIndices.add (c.contextIndex);
            addAndMakeVisible (tb);
        }
        setSize (400, juce::jmax (24, candidates.size() * 26));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        for (auto* tb : toggles)
            tb->setBounds (r.removeFromTop (26));
    }

    juce::Array<int> selectedContextIndices() const
    {
        juce::Array<int> out;
        for (int i = 0; i < toggles.size(); ++i)
            if (toggles[i]->getToggleState())
                out.add (contextIndices[i]);
        return out;
    }

    int selectedCount() const { return selectedContextIndices().size(); }

    std::function<void()> onSelectionChanged;

private:
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::Array<int> contextIndices;
};

class ComparisonOptionsPanel : public juce::Component
{
public:
    explicit ComparisonOptionsPanel (const juce::Array<CompareCandidate>& candidates)
        : list (candidates)
    {
        heading.setText (utf8 ("Choose up to ") + juce::String (kMaxComparePatches)
                             + utf8 (" contexts for comparison:"),
                         juce::dontSendNotification);
        heading.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (heading);

        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setColour (juce::Label::textColourId, juce::Colours::orange);
        addAndMakeVisible (hint);

        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        targetLabel.setText (utf8 ("Target"), juce::dontSendNotification);
        targetLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (targetLabel);
        targetBox.addItem (utf8 ("Software"), 1);
        targetBox.addItem (utf8 ("Hardware"), 2);
        targetBox.addItem (utf8 ("Both"), 3);
        targetBox.setSelectedId (3, juce::dontSendNotification);
        addAndMakeVisible (targetBox);

        reportLabel.setText (utf8 ("Plot Spectra"), juce::dontSendNotification);
        reportLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (reportLabel);
        reportBox.addItem (utf8 ("Yes"), 1);
        reportBox.addItem (utf8 ("No"), 2);
        reportBox.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (reportBox);

        list.onSelectionChanged = [this]
        {
            refreshHint();
            if (onSelectionChanged)
                onSelectionChanged();
        };
        refreshHint();

        setSize (460, 400);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        heading.setBounds (r.removeFromTop (22));
        hint.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);

        auto bottom = r.removeFromBottom (64);
        auto row1 = bottom.removeFromTop (28);
        targetLabel.setBounds (row1.removeFromLeft (140));
        targetBox.setBounds (row1.removeFromLeft (160));
        bottom.removeFromTop (8);
        auto row2 = bottom.removeFromTop (28);
        reportLabel.setBounds (row2.removeFromLeft (140));
        reportBox.setBounds (row2.removeFromLeft (160));

        r.removeFromBottom (8);
        viewport.setBounds (r);
        list.setSize (juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness()),
                      list.getHeight());
    }

    juce::Array<int> selectedContextIndices() const { return list.selectedContextIndices(); }
    int selectedCount() const { return list.selectedCount(); }
    bool selectionIsValid() const
    {
        const int n = selectedCount();
        return n >= 1 && n <= kMaxComparePatches;
    }
    int targetIndex() const { return juce::jmax (0, targetBox.getSelectedItemIndex()); }
    bool wantReport() const { return reportBox.getSelectedItemIndex() == 0; }

    std::function<void()> onSelectionChanged;

private:
    void refreshHint()
    {
        const int n = selectedCount();
        if (n > kMaxComparePatches)
        {
            hint.setText (utf8 ("Selected ") + juce::String (n)
                              + utf8 (" — uncheck some to get down to ")
                              + juce::String (kMaxComparePatches)
                              + utf8 (" or fewer."),
                          juce::dontSendNotification);
            hint.setVisible (true);
        }
        else if (n == 0)
        {
            hint.setText (utf8 ("Select at least one context."),
                          juce::dontSendNotification);
            hint.setVisible (true);
        }
        else
        {
            hint.setText ({}, juce::dontSendNotification);
            hint.setVisible (false);
        }
    }

    juce::Label heading, hint;
    ContextToggleList list;
    juce::Viewport viewport;
    juce::Label targetLabel, reportLabel;
    juce::ComboBox targetBox, reportBox;
};

} // namespace

void runComparisonReportDialog (PatchContextManager& contexts,
                                PluginAudioEngine& engine,
                                HostConfig& config,
                                juce::Component* parent)
{
    juce::Array<CompareCandidate> candidates;
    for (int i = 0; i < contexts.size(); ++i)
        if (auto* ctx = contexts.get (i))
            if (ctx->compare)
                candidates.add ({ i, ctx->name });

    if (candidates.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            utf8 ("Comparison Report"),
            utf8 ("No patch contexts are marked Compare. "
                 "Enable Compare on the contexts you want to include."));
        return;
    }

    juce::AlertWindow w (utf8 ("Comparison Report"),
                         utf8 ("You can choose up to ") + juce::String (kMaxComparePatches)
                             + utf8 (" contexts for comparison. Captures software and/or hardware, "
                                     "then writes one HTML gallery (waveform + spectrogram, "
                                     "switchable by patch and target)."),
                         juce::AlertWindow::InfoIcon,
                         parent);

    ComparisonOptionsPanel panel (candidates);
    w.addCustomComponent (&panel);
    w.addButton (utf8 ("Run"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto syncRunEnabled = [&w, &panel]
    {
        if (auto* run = w.getButton (0))
            run->setEnabled (panel.selectionIsValid());
    };
    panel.onSelectionChanged = syncRunEnabled;
    syncRunEnabled();

    if (w.runModalLoop() != 1)
        return;

    const int targetIdx = panel.targetIndex();
    const bool wantReport = panel.wantReport();
    CaptureSource source = CaptureSource::plugin;
    if (targetIdx == 1) source = CaptureSource::hardware;
    if (targetIdx == 2) source = CaptureSource::both;

    const auto selected = panel.selectedContextIndices();
    if (! panel.selectionIsValid())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            utf8 ("Comparison Report"),
            utf8 ("Select between 1 and ") + juce::String (kMaxComparePatches)
                + utf8 (" patch contexts."));
        return;
    }

    const auto fixture = engine.getCurrentFixtureFile();
    if (! fixture.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                utf8 ("Comparison Report"),
                                                utf8 ("Load a source clip first."));
        return;
    }

    config.ensureSessions();
    const auto sessionName = "quadraverse_compare";
    auto* pluginEntry = config.defaultPlugin();
    if (pluginEntry == nullptr && ! config.plugins.isEmpty())
        pluginEntry = &config.plugins.getReference (0);
    if (pluginEntry == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                utf8 ("Comparison Report"),
                                                utf8 ("No plugin configured."));
        return;
    }

    juce::StringArray capturedStems;
    juce::String error;
    CapturePipeline pipeline (engine, config, {});

    for (int idx : selected)
    {
        auto* ctx = contexts.get (idx);
        if (ctx == nullptr)
            continue;

        if (source == CaptureSource::plugin || source == CaptureSource::both)
        {
            juce::MemoryBlock blob;
            juce::String blobErr;
            if (Qdv1StateIO::toStateBlob (ctx->program, blob, blobErr))
                if (auto* plugin = engine.getPlugin())
                    plugin->setStateInformation (blob.getData(), (int) blob.getSize());
        }

        if (source == CaptureSource::hardware || source == CaptureSource::both)
        {
            auto prog = ctx->program;
            prog.flushValuesToBytes();
            if (prog.hasValidBytes)
            {
                const auto msg = AlesisCodec::buildLoadProgram (
                    0x02, AlesisCodec::kEditBuffer, prog.bytes.data());
                juce::Array<juce::MidiMessage> messages;
                messages.add (msg);
                engine.sendMidiMessages (messages);
            }
        }

        CapturePipelineRequest req;
        req.description = ctx->name;
        req.roleIndex = -1; // no _gld / role suffix
        req.source = source;
        req.fixtureFile = fixture;
        req.progressParent = parent;
        req.calibrateNoiseFloor = false;
        req.captureSoftwareSettings = false;
        req.captureHardwareSettings = false;
        req.sessionNameOverride = sessionName;

        CapturePipelineResult result;
        if (! pipeline.run (req, *pluginEntry, result, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                    utf8 ("Capture failed"),
                                                    error);
            return;
        }

        capturedStems.add (result.paths.stem);

        SessionSnapRequest snap;
        snap.sessionsRoot = config.sessionsRoot;
        snap.sessionName = sessionName;
        snap.snapshotName = ctx->name;
        snap.sourceClipName = fixture.getFileName();
        snap.inputFile = result.paths.captureDir.getChildFile (result.paths.stem + "_input.wav");
        if (! snap.inputFile.existsAsFile())
            snap.inputFile = fixture;
        snap.outputFile = result.paths.softwareOutput;
        snap.hardwareOutputFile = result.paths.hardwareOutput;
        snap.notes = utf8 ("Captured from Quadraverse");
        juce::String snapErr;
        SessionSnap::registerSnapshot (snap, snapErr);
    }

    const auto sessionDir = config.sessionsRoot.getChildFile (HostConfig::slugify (sessionName));
    const auto galleryHtml = sessionDir.getChildFile ("compare_gallery.html");

    if (wantReport && ! capturedStems.isEmpty() && config.pythonCli.existsAsFile())
    {
        juce::StringArray args;
        args.add (config.pythonCli.getFullPathName());
        args.add ("compare-gallery");
        args.add ("--root");
        args.add (config.sessionsRoot.getFullPathName());
        args.add (sessionName);
        args.add ("--limit");
        args.add (juce::String (kMaxComparePatches));
        args.add ("--stems");
        args.add (capturedStems.joinIntoString (","));

        juce::ChildProcess proc;
        if (proc.start (args))
            proc.waitForProcessToFinish (600000);
    }
    else if (wantReport && ! config.pythonCli.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            utf8 ("Comparison Report"),
            utf8 ("aufx-test CLI not configured (Settings → python_cli). Captures were saved."));
        return;
    }

    if (galleryHtml.existsAsFile())
        galleryHtml.revealToUser();
    else if (sessionDir.isDirectory())
        sessionDir.revealToUser();
    else if (config.sessionsRoot.isDirectory())
        config.sessionsRoot.revealToUser();
}

} // namespace qverse
