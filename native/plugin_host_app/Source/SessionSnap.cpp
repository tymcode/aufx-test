#include "SessionSnap.h"
#include "HostConfig.h"

namespace
{
    juce::String utcNowIso()
    {
        return juce::Time::getCurrentTime().toISO8601 (true);
    }

    juce::String parseOutputRole (const juce::File& outputFile)
    {
        const auto stem = outputFile.getFileNameWithoutExtension();
        for (const auto* role : { "gld", "sus", "bkn" })
        {
            const auto suffix = juce::String ("_output_") + role;
            if (stem.endsWith (suffix))
                return role;
        }
        return {};
    }

    juce::String baseStemFromOutput (const juce::File& outputFile)
    {
        auto stem = outputFile.getFileNameWithoutExtension();
        for (const auto* role : { "gld", "sus", "bkn" })
        {
            const auto suffix = juce::String ("_output_") + role;
            if (stem.endsWith (suffix))
                return stem.dropLastCharacters (suffix.length());
        }
        if (stem.endsWith ("_output"))
            return stem.dropLastCharacters (7);
        return stem;
    }

    juce::String snapshotIdFromStem (const juce::String& stem)
    {
        if (stem.containsChar ('_'))
            return stem.fromLastOccurrenceOf ("_", false, false);
        return stem;
    }

    bool expectMatchForRole (const juce::String& role)
    {
        return role != "sus" && role != "bkn";
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
}

bool SessionSnap::registerSnapshot (const SessionSnapRequest& request, juce::String& error)
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
    if (! request.outputFile.existsAsFile())
    {
        error = "Output file missing: " + request.outputFile.getFullPathName();
        return false;
    }

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

    const auto stem = baseStemFromOutput (request.outputFile);
    const auto role = parseOutputRole (request.outputFile);
    const auto snapId = snapshotIdFromStem (stem);

    // Stage input into artifacts when needed.
    juce::String inputRel;
    if (request.inputFile.existsAsFile())
    {
        const auto inputDest = artifactsDir.getChildFile (stem + "_input" + request.inputFile.getFileExtension());
        if (request.inputFile.getParentDirectory() != artifactsDir
            || request.inputFile.getFileName() != inputDest.getFileName())
        {
            if (! request.inputFile.copyFileTo (inputDest))
            {
                error = "Failed to copy input into artifacts";
                return false;
            }
        }
        inputRel = "artifacts/" + inputDest.getFileName();
    }

    juce::String outputRel = "artifacts/" + request.outputFile.getFileName();
    if (request.outputFile.getParentDirectory() != artifactsDir)
    {
        const auto dest = artifactsDir.getChildFile (request.outputFile.getFileName());
        if (! request.outputFile.copyFileTo (dest))
        {
            error = "Failed to copy output into artifacts";
            return false;
        }
        outputRel = "artifacts/" + dest.getFileName();
    }

    juce::String presetRel;
    if (request.presetFile.existsAsFile())
    {
        auto destName = stem + ".aupreset";
        if (request.presetFile.getFileExtension().equalsIgnoreCase (".aupreset"))
            destName = request.presetFile.getFileName().startsWith (stem)
                           ? request.presetFile.getFileName()
                           : destName;
        else
            destName = stem + request.presetFile.getFileExtension();

        const auto dest = artifactsDir.getChildFile (destName);
        if (request.presetFile.getFullPathName() != dest.getFullPathName())
        {
            if (request.presetFile.getParentDirectory() == artifactsDir)
            {
                if (request.presetFile.getFileName() != dest.getFileName())
                    request.presetFile.moveFileTo (dest);
            }
            else if (! request.presetFile.copyFileTo (dest))
            {
                error = "Failed to stage preset into artifacts";
                return false;
            }
        }
        presetRel = "artifacts/" + dest.getFileName();
    }

    auto* snap = new juce::DynamicObject();
    snap->setProperty ("name", request.snapshotName);
    snap->setProperty ("parameters", juce::var (new juce::DynamicObject()));
    if (inputRel.isNotEmpty())
        snap->setProperty ("input_audio", inputRel);
    snap->setProperty ("output_audio", outputRel);
    if (presetRel.isNotEmpty())
        snap->setProperty ("preset_file", presetRel);
    snap->setProperty ("notes", request.notes);
    snap->setProperty ("tags", juce::var (juce::Array<juce::var>{}));
    snap->setProperty ("id", snapId.isNotEmpty() ? snapId : juce::Uuid().toString().substring (0, 8));
    snap->setProperty ("created_at", utcNowIso());
    snap->setProperty ("promoted", false);
    snap->setProperty ("test_name", {});
    snap->setProperty ("thresholds", {});
    snap->setProperty ("expect_match", expectMatchForRole (role));
    if (role.isNotEmpty())
        snap->setProperty ("reference_kind", role);

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

    return true;
}
