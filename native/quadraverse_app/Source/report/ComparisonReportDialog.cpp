#include "ComparisonReportDialog.h"
#include "Utf8.h"
#include "CapturePipeline.h"
#include "SessionSnap.h"
#include "../formats/Qdv1StateIO.h"
#include "../domain/AlesisCodec.h"

namespace qverse
{

void runComparisonReportDialog (PatchContextManager& contexts,
                                PluginAudioEngine& engine,
                                HostConfig& config,
                                juce::Component* parent)
{
    juce::AlertWindow w (utf8 ("Comparison Report"),
                         utf8 ("Capture selected patch contexts on software and/or hardware, "
                              "then generate an aufx-test compare report (with spectrograms)."),
                         juce::AlertWindow::InfoIcon,
                         parent);

    const auto names = contexts.getNames();
    for (int i = 0; i < names.size(); ++i)
    {
        w.addComboBox ("ctx" + juce::String (i),
                       { utf8 ("Skip"), utf8 ("Include") },
                       names[i]);
        if (auto* ctx = contexts.get (i))
            if (auto* cb = w.getComboBoxComponent ("ctx" + juce::String (i)))
                cb->setSelectedItemIndex (ctx->compare ? 1 : 0);
    }

    w.addComboBox ("target",
                   { utf8 ("Software"), utf8 ("Hardware"), utf8 ("Both") },
                   utf8 ("Target"));
    w.addComboBox ("report", { utf8 ("Yes"), utf8 ("No") }, utf8 ("Generate CLI report"));
    w.addButton (utf8 ("Run"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (utf8 ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (w.runModalLoop() != 1)
        return;

    const int targetIdx = w.getComboBoxComponent ("target")->getSelectedItemIndex();
    const bool wantReport = w.getComboBoxComponent ("report")->getSelectedItemIndex() == 0;
    CaptureSource source = CaptureSource::plugin;
    if (targetIdx == 1) source = CaptureSource::hardware;
    if (targetIdx == 2) source = CaptureSource::both;

    juce::Array<int> selected;
    for (int i = 0; i < contexts.size(); ++i)
        if (auto* cb = w.getComboBoxComponent ("ctx" + juce::String (i)))
            if (cb->getSelectedItemIndex() == 1)
                selected.add (i);

    if (selected.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                utf8 ("Comparison Report"),
                                                utf8 ("Select at least one patch context."));
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
        req.roleIndex = 0;
        req.source = source;
        req.fixtureFile = fixture;
        req.progressParent = parent;
        req.calibrateNoiseFloor = false;
        req.captureSoftwareSettings = false;
        req.captureHardwareSettings = false;

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

    if (wantReport && ! capturedStems.isEmpty() && config.pythonCli.existsAsFile())
    {
        juce::StringArray args;
        args.add (config.pythonCli.getFullPathName());
        args.add ("compare");
        args.add ("--root");
        args.add (config.sessionsRoot.getFullPathName());
        args.add (sessionName);
        args.add (capturedStems[0]);
        args.add ("--write-report");
        args.add ("--spectrogram-diff");

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

    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::InfoIcon,
        utf8 ("Comparison Report"),
        utf8 ("Done. Artifacts under sessions/") + juce::String (sessionName)
            + utf8 ("\nStems: ") + capturedStems.joinIntoString (", "));
}

} // namespace qverse
