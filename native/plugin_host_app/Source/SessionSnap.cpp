/**
 * Native re-implementation of `aufx-test session snap`: stage capture
 * artifacts into <session>/artifacts/<stem>/ and append a snapshot entry to
 * session.json. Kept byte-compatible with the Python session module in
 * src/aufx_test/session.py — the pytest tooling reads what this writes, so
 * schema change must land in both places. Role suffixes and aupreset naming
 * live in SessionArtifactSchema.h; Python still mirrors them by convention.
 */
#include "SessionSnap.h"
#include "HostConfig.h"
#include "SessionArtifactSchema.h"

namespace
{
    juce::String utcNowIso()
    {
        return juce::Time::getCurrentTime().toISO8601 (true);
    }

    // The 8-char token appended at capture time (e.g. "drums_3f2a91cc")
    // doubles as the snapshot id so filenames and session.json stay
    // cross-referencable by eye.
    juce::String snapshotIdFromStem (const juce::String& stem)
    {
        if (stem.containsChar ('_'))
            return stem.fromLastOccurrenceOf ("_", false, false);
        return stem;
    }

    juce::var loadOrCreateSession (const juce::File& sessionFile,
                                   const juce::String& sessionName,
                                   const juce::String& pluginPath,
                                   juce::String& error)
    {
        if (sessionFile.existsAsFile())
        {
            const auto parsed = juce::JSON::parse (sessionFile.loadFileAsString());
            if (parsed.isVoid() || parsed.getDynamicObject() == nullptr)
            {
                error = "Failed to parse session.json: " + sessionFile.getFullPathName();
                return {};
            }
            return parsed;
        }

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", sessionName);
        obj->setProperty ("plugin_path", pluginPath);
        obj->setProperty ("description", "");
        const auto now = utcNowIso();
        obj->setProperty ("created_at", now);
        obj->setProperty ("updated_at", now);
        obj->setProperty ("snapshots", juce::var (juce::Array<juce::var>{}));
        return juce::var (obj);
    }

    bool stageIntoStemDir (const juce::File& source,
                           const juce::File& dest,
                           juce::String& error,
                           const juce::String& failMessage)
    {
        if (! source.existsAsFile())
            return true;

        if (source.getFullPathName() == dest.getFullPathName())
            return true;

        dest.getParentDirectory().createDirectory();

        if (source.getParentDirectory() == dest.getParentDirectory())
        {
            if (source.getFileName() != dest.getFileName() && ! source.moveFileTo (dest))
            {
                error = failMessage;
                return false;
            }
            return true;
        }

        if (! source.copyFileTo (dest))
        {
            error = failMessage;
            return false;
        }
        return true;
    }
}

bool SessionSnap::registerSnapshot (const SessionSnapRequest& request,
                                    juce::String& error,
                                    juce::String* outSnapshotId)
{
    if (request.sessionName.isEmpty())
    {
        error = "Session name is empty";
        return false;
    }
    if (request.snapshotName.isEmpty())
    {
        error = "Snapshot name is empty";
        return false;
    }

    const bool hasPluginOut = request.outputFile.existsAsFile();
    const bool hasHwOut = request.hardwareOutputFile.existsAsFile();
    if (! hasPluginOut && ! hasHwOut)
    {
        error = "No output audio file to register";
        return false;
    }

    const auto primaryOut = hasPluginOut ? request.outputFile : request.hardwareOutputFile;

    const auto sessionDir = request.sessionsRoot.getChildFile (HostConfig::slugify (request.sessionName));
    const auto artifactsDir = sessionDir.getChildFile ("artifacts");
    sessionDir.createDirectory();
    artifactsDir.createDirectory();

    const auto sessionFile = sessionDir.getChildFile ("session.json");
    auto rootVar = loadOrCreateSession (sessionFile, request.sessionName, request.pluginPath, error);
    if (rootVar.isVoid())
        return false;

    auto* root = rootVar.getDynamicObject();
    if (root == nullptr)
    {
        error = "Invalid session.json root";
        return false;
    }

    if (request.pluginPath.isNotEmpty() && root->getProperty ("plugin_path").toString().isEmpty())
        root->setProperty ("plugin_path", request.pluginPath);

    const auto stem = SessionArtifactSchema::baseStemFromOutput (primaryOut);
    const auto role = SessionArtifactSchema::parseOutputRole (primaryOut);
    const auto snapId = snapshotIdFromStem (stem);
    const auto stemDir = artifactsDir.getChildFile (stem);
    stemDir.createDirectory();

    juce::String inputRel;
    if (request.inputFile.existsAsFile())
    {
        const auto inputDest = stemDir.getChildFile (stem + "_input" + request.inputFile.getFileExtension());
        if (! stageIntoStemDir (request.inputFile, inputDest, error, "Failed to copy input into artifacts"))
            return false;
        inputRel = SessionArtifactSchema::relativeArtifactPath (stem, inputDest.getFileName());
    }

    auto stageOutput = [&] (const juce::File& file, juce::String& rel) -> bool
    {
        if (! file.existsAsFile())
            return true;

        const auto dest = stemDir.getChildFile (file.getFileName());
        if (! stageIntoStemDir (file, dest, error, "Failed to copy output into artifacts"))
            return false;
        rel = SessionArtifactSchema::relativeArtifactPath (stem, dest.getFileName());
        return true;
    };

    juce::String outputRel, hwOutputRel;
    if (hasPluginOut && ! stageOutput (request.outputFile, outputRel))
        return false;
    if (hasHwOut && ! stageOutput (request.hardwareOutputFile, hwOutputRel))
        return false;

    juce::String presetRel;
    if (request.presetFile.existsAsFile())
    {
        auto destName = SessionArtifactSchema::ensureAupresetExtension (stem);
        if (request.presetFile.getFileExtension().equalsIgnoreCase (SessionArtifactSchema::aupresetExtension))
            destName = request.presetFile.getFileName().startsWith (stem)
                           ? request.presetFile.getFileName()
                           : destName;
        else
            destName = stem + request.presetFile.getFileExtension();

        const auto dest = stemDir.getChildFile (destName);
        if (! stageIntoStemDir (request.presetFile, dest, error, "Failed to stage preset into artifacts"))
            return false;
        presetRel = SessionArtifactSchema::relativeArtifactPath (stem, dest.getFileName());
    }

    juce::String sysexRel;
    if (request.sysexFile.existsAsFile())
    {
        const auto dest = stemDir.getChildFile (stem + ".syx");
        if (! stageIntoStemDir (request.sysexFile, dest, error, "Failed to stage sysex into artifacts"))
            return false;
        sysexRel = SessionArtifactSchema::relativeArtifactPath (stem, dest.getFileName());
    }

    auto* snap = new juce::DynamicObject();
    snap->setProperty ("name", request.snapshotName);
    if (request.sourceClipName.isNotEmpty())
        snap->setProperty ("source_clip_name", request.sourceClipName);
    snap->setProperty ("parameters", juce::var (new juce::DynamicObject()));
    if (inputRel.isNotEmpty())
        snap->setProperty ("input_audio", inputRel);
    if (outputRel.isNotEmpty())
        snap->setProperty ("output_audio", outputRel);
    if (hwOutputRel.isNotEmpty())
        snap->setProperty ("output_audio_hw", hwOutputRel);
    if (presetRel.isNotEmpty())
        snap->setProperty ("preset_file", presetRel);
    if (sysexRel.isNotEmpty())
        snap->setProperty ("sysex_file", sysexRel);
    snap->setProperty ("notes", request.notes);
    snap->setProperty ("tags", juce::var (juce::Array<juce::var>{}));
    snap->setProperty ("id", snapId.isNotEmpty() ? snapId : juce::Uuid().toString().substring (0, 8));
    snap->setProperty ("created_at", utcNowIso());
    snap->setProperty ("promoted", false);
    snap->setProperty ("test_name", {});
    snap->setProperty ("thresholds", {});
    snap->setProperty ("expect_match", SessionArtifactSchema::expectMatchForRole (role));
    if (role.isNotEmpty())
        snap->setProperty ("reference_kind", role);

    const auto assignedId = snap->getProperty ("id").toString();

    auto snapshots = root->getProperty ("snapshots");
    if (! snapshots.isArray())
        snapshots = juce::var (juce::Array<juce::var>{});
    snapshots.getArray()->add (juce::var (snap));
    root->setProperty ("snapshots", snapshots);
    root->setProperty ("updated_at", utcNowIso());

    if (! sessionFile.replaceWithText (juce::JSON::toString (rootVar, true) + "\n"))
    {
        error = "Failed to write session.json";
        return false;
    }

    if (outSnapshotId != nullptr)
        *outSnapshotId = assignedId;

    return true;
}

namespace
{
    juce::File resolveSessionRelative (const juce::File& sessionDir, const juce::String& relative)
    {
        if (relative.isEmpty())
            return {};

        const juce::File asAbsolute (relative);
        if (juce::File::isAbsolutePath (relative) && asAbsolute.existsAsFile())
            return asAbsolute;

        return sessionDir.getChildFile (relative);
    }

    SessionSnapshotRef snapshotFromVar (const juce::File& sessionDir, const juce::var& snapVar)
    {
        SessionSnapshotRef ref;
        ref.sessionDir = sessionDir;

        if (auto* obj = snapVar.getDynamicObject())
        {
            ref.id = obj->getProperty ("id").toString();
            ref.name = obj->getProperty ("name").toString();
            ref.sourceClipName = obj->getProperty ("source_clip_name").toString();
            ref.inputAudio = resolveSessionRelative (sessionDir, obj->getProperty ("input_audio").toString());
            ref.presetFile = resolveSessionRelative (sessionDir, obj->getProperty ("preset_file").toString());
            ref.sysexFile = resolveSessionRelative (sessionDir, obj->getProperty ("sysex_file").toString());
            ref.outputAudio = resolveSessionRelative (sessionDir, obj->getProperty ("output_audio").toString());
            ref.hardwareOutputAudio = resolveSessionRelative (sessionDir, obj->getProperty ("output_audio_hw").toString());
            ref.referenceKind = obj->getProperty ("reference_kind").toString();
            if (obj->hasProperty ("expect_match"))
                ref.expectMatch = (bool) obj->getProperty ("expect_match");
        }

        return ref;
    }
}

bool SessionSnap::loadSessionRoot (const juce::File& sessionDir, juce::var& outRoot, juce::String& error)
{
    const auto sessionFile = sessionDir.getChildFile ("session.json");
    if (! sessionFile.existsAsFile())
    {
        error = "session.json not found: " + sessionFile.getFullPathName();
        return false;
    }

    outRoot = juce::JSON::parse (sessionFile.loadFileAsString());
    if (outRoot.isVoid() || outRoot.getDynamicObject() == nullptr)
    {
        error = "Failed to parse session.json: " + sessionFile.getFullPathName();
        return false;
    }

    return true;
}

juce::Array<SessionSnapshotRef> SessionSnap::listSnapshots (const juce::File& sessionDir, juce::String& error)
{
    juce::Array<SessionSnapshotRef> results;
    juce::var root;
    if (! loadSessionRoot (sessionDir, root, error))
        return results;

    const auto snapshots = root.getProperty ("snapshots", juce::var());
    if (! snapshots.isArray())
        return results;

    for (const auto& snapVar : *snapshots.getArray())
        results.add (snapshotFromVar (sessionDir, snapVar));

    return results;
}

bool SessionSnap::findSnapshot (const juce::File& sessionDir,
                                const juce::String& match,
                                SessionSnapshotRef& out,
                                juce::String& error)
{
    if (match.isEmpty())
    {
        error = "Snapshot id/name is empty";
        return false;
    }

    const auto snaps = listSnapshots (sessionDir, error);
    if (error.isNotEmpty() && snaps.isEmpty())
        return false;

    for (const auto& snap : snaps)
    {
        if (snap.id == match || snap.name == match)
        {
            out = snap;
            return true;
        }
    }

    error = "Snapshot not found: " + match;
    return false;
}
