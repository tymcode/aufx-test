#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>

// ---------------------------------------------------------------------------
// QvPresetStore -- the data/persistence layer behind the preset browser,
// modeled on DeepZ's presets::PresetLibrary.  UI-agnostic; the browser
// listens (ChangeBroadcaster) and rebuilds on every mutation.
//
// Two kinds of preset:
//   * FACTORY -- the unit's stored programs (blank slots dropped).  Read-only
//     content; the user may TAG them but not rename/delete.  "Loading" one is
//     a program change.
//   * USER    -- a saved snapshot of the plugin's state blob
//     (getStateInformation).  Save-as / delete / tag.
//
// ON-DISK LAYOUT (all under the base directory, default
// ~/Library/Application Support/TemeculaDSP/QDV1, overridable for tests via
// the QDV1_PRESET_DIR environment variable):
//   presets/<uuid>.json   one file per USER preset:
//       { "id","name","tags":[..],"state":"<base64 state blob>" }
//   preset_meta.json      factory TAG OVERRIDES, keyed by id ("factory:12"):
//       an entry is written only when a factory preset's tags differ from
//       the built-in defaults (an empty override list is meaningful).
//   preset_tags.json      the tag catalog + the one-shot defaults flag:
//       { "tags":[..], "defaultsSeeded":bool }
//
// Default factory tags (seeded once; deletable like any tag): reverb, delay,
// reverse, chorus, flange.  The built-in per-program map lives in the .cpp.
//
// THREADING: message-thread only.  No internal locking.
// ---------------------------------------------------------------------------
struct QvPreset {
    juce::String      id;          // "factory:<n>" or a user uuid
    juce::String      name;
    bool              isFactory = false;
    int               program   = -1;    // factory: program number; user: -1
    juce::MemoryBlock stateBlob;         // user only
    juce::StringArray tags;
};

class QvPresetStore : public juce::ChangeBroadcaster {
public:
    // factoryNames: all 100 stored program names (blank slots are dropped
    // here).  baseDirectory empty => env override or the standard app-data
    // location.
    explicit QvPresetStore(const juce::StringArray& factoryNames,
                           const juce::File& baseDirectory = {});

    void reload();                       // re-read everything from disk

    // The tab's list in display order (factory by program number, user
    // alphabetical), AND-filtered by tagFilter (empty = all).
    std::vector<const QvPreset*> list(bool user, const juce::StringArray& tagFilter) const;
    const QvPreset* byId(const juce::String& id) const;
    static juce::String factoryId(int program) { return "factory:" + juce::String(program); }

    // User preset CRUD.  saveUserPreset returns the new id ("" on failure).
    juce::String saveUserPreset(const juce::String& name, const juce::MemoryBlock& stateBlob);
    bool deleteUserPreset(const juce::String& id);

    // Tags (factory and user presets alike).
    bool addTag(const juce::String& id, const juce::String& tag);
    bool removeTag(const juce::String& id, const juce::String& tag);

    // Tag catalog: catalog order (defaults first), then tags merely in use.
    juce::StringArray allTags() const;
    void createTag(const juce::String& name);
    void deleteTag(const juce::String& name);   // removed everywhere

    juce::File getBaseDirectory() const { return baseDir_; }

private:
    QvPreset* find(const juce::String& id);
    void sortPresets();

    void loadFactory(const juce::StringArray& names);
    void loadFactoryMeta();
    void loadUserPresets();
    void loadTagCatalog();
    void seedDefaultTags();

    void writeUserPreset(const QvPreset&);
    void writeFactoryMeta();
    void writeTagCatalog();
    juce::File presetFileFor(const juce::String& id) const;
    juce::File presetsDir() const { return baseDir_.getChildFile("presets"); }

    juce::File            baseDir_;
    juce::StringArray     factoryNames_;
    std::vector<QvPreset> presets_;
    juce::StringArray     tagCatalog_;
    bool                  defaultsSeeded_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QvPresetStore)
};
