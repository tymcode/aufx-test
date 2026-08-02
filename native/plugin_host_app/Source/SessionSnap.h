#pragma once

#include <JuceHeader.h>

/** Native port of `aufx-test session snap` — updates session.json without Python. */
struct SessionSnapRequest
{
    juce::File sessionsRoot;
    juce::String sessionName;
    juce::String snapshotName;
    juce::String sourceClipName;
    juce::File inputFile;
    juce::File outputFile;
    juce::File hardwareOutputFile; // optional latency-corrected hardware capture
    juce::File presetFile;
    juce::File sysexFile; // optional hardware state dump
    juce::String pluginPath;
    juce::String notes { "Captured from AU Effects Explorer" };
};

/**
 * Resolved paths for one snapshot entry — used by Load Testcase to restore
 * input audio, preset, and optional sysex into the host.
 */
struct SessionSnapshotRef
{
    juce::String id;
    juce::String name;
    juce::String sourceClipName;
    juce::File sessionDir;
    juce::File inputAudio;
    juce::File presetFile;
    juce::File sysexFile;
    juce::File outputAudio;
    juce::File hardwareOutputAudio;
    juce::String referenceKind;
    bool expectMatch { true };
};

struct SessionSnap
{
    /** Register a snapshot; optionally returns the assigned snapshot id. */
    static bool registerSnapshot (const SessionSnapRequest& request,
                                  juce::String& error,
                                  juce::String* outSnapshotId = nullptr);

    /** Load and parse session.json under sessionDir. */
    static bool loadSessionRoot (const juce::File& sessionDir, juce::var& outRoot, juce::String& error);

    /** List snapshots with artifact paths resolved relative to sessionDir. */
    static juce::Array<SessionSnapshotRef> listSnapshots (const juce::File& sessionDir, juce::String& error);

    /**
     * Find a snapshot by id or exact name. Returns false if not found.
     * match may be a snapshot id (8-char token) or the snapshot name.
     */
    static bool findSnapshot (const juce::File& sessionDir,
                              const juce::String& match,
                              SessionSnapshotRef& out,
                              juce::String& error);

    /**
     * After moving sessions from oldRoot to newRoot, rewrite absolute artifact
     * paths inside each session.json so they stay relative to the session dir
     * (or point under the new root).
     */
    static bool rewritePathsAfterRootMove (const juce::File& oldRoot,
                                           const juce::File& newRoot,
                                           juce::String& error);
};
