#include "QvPresetStore.h"

namespace {

const char* kMetaFile = "preset_meta.json";
const char* kTagsFile = "preset_tags.json";

// The five default factory tags, in display order.
const char* kDefaultTags[] = { "reverb", "delay", "reverse", "chorus", "flange" };

// Built-in factory tag map for the 100 stored programs.  Derived from the
// program names, cross-checked against each stored program's configuration,
// block modes and mix levels.  REVERSE-type reverbs tag "reverse", not
// "reverb".  Blank slots (90-99) never reach this table.
// Untagged on purpose (none of the five categories fit): 7 DETUNE +12,
// 8 LEZLY aftr tch, 9 5 BAND PARA EQ, 37 STEREO PITCH, 38 LOOSE LEZLIE,
// 39 P.BEND LEZLIE, 51 AMAZING BASS, 67 SUSPEDAL LEZLY, 85 SAMPLING QV,
// 86 AUTO-PANNING, 87 TREMOLO QV, 88 RNG MOD+4 RHDS.
struct TagGroup { const char* tag; const int programs[64]; };
const TagGroup kFactoryTagMap[] = {
    { "reverb",  { 0, 1, 2, 3, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                   22, 23, 24, 25, 26, 27, 28, 29, 34, 35, 50, 52, 53, 54, 59,
                   60, 62, 64, 66, 69, 70, 71, 72, 73, 74, 75, 78, 79, 89, -1 } },
    { "delay",   { 0, 6, 36, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 55, 56,
                   59, 60, 61, 62, 64, 68, 69, 70, 71, 74, 76, 77, 80, 81, 82,
                   83, 84, -1 } },
    { "reverse", { 48, 49, 72, -1 } },
    { "chorus",  { 0, 4, 33, 34, 35, 36, 54, 57, 58, 61, 62, 63, 64, 65, -1 } },
    { "flange",  { 5, 28, 29, 30, 31, 32, 45, 46, 47, 84, -1 } },
};

juce::StringArray builtinFactoryTags(int program) {
    juce::StringArray out;
    for (const auto& g : kFactoryTagMap)
        for (int i = 0; g.programs[i] >= 0; ++i)
            if (g.programs[i] == program) { out.add(g.tag); break; }
    return out;
}

bool sameTagSet(const juce::StringArray& a, const juce::StringArray& b) {
    if (a.size() != b.size()) return false;
    for (const auto& t : a)
        if (!b.contains(t)) return false;
    return true;
}

juce::var readJson(const juce::File& f) {
    if (!f.existsAsFile()) return {};
    juce::var v;
    if (juce::JSON::parse(f.loadFileAsString(), v).failed()) return {};
    return v;
}

juce::StringArray tagsFromVar(const juce::var& v) {
    juce::StringArray out;
    if (auto* arr = v.getArray())
        for (const auto& t : *arr)
            out.addIfNotAlreadyThere(t.toString());
    return out;
}

juce::var tagsToVar(const juce::StringArray& tags) {
    juce::Array<juce::var> arr;
    for (const auto& t : tags) arr.add(t);
    return arr;
}

bool isBlankName(const juce::String& name) {
    return name.trim().isEmpty() || name.trim().equalsIgnoreCase("BLANK PROGRAM")
        || name.trim().equalsIgnoreCase("SAMPLING QV");   // sampling config dropped
}

}  // namespace

// ---------------------------------------------------------------------------
QvPresetStore::QvPresetStore(const juce::StringArray& factoryNames,
                             const juce::File& baseDirectory)
    : factoryNames_(factoryNames) {
    if (baseDirectory != juce::File()) {
        baseDir_ = baseDirectory;
    } else {
        const auto env = juce::SystemStats::getEnvironmentVariable("QDV1_PRESET_DIR", {});
        baseDir_ = env.isNotEmpty()
                       ? juce::File(env)
                       : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("TemeculaDSP").getChildFile("QDV1");
    }
    reload();
}

void QvPresetStore::reload() {
    presets_.clear();
    tagCatalog_.clear();
    defaultsSeeded_ = false;

    loadFactory(factoryNames_);   // built-in defaults from the map above
    loadFactoryMeta();            // then any user overrides from disk
    loadUserPresets();
    loadTagCatalog();
    if (!defaultsSeeded_) seedDefaultTags();

    sortPresets();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
void QvPresetStore::loadFactory(const juce::StringArray& names) {
    for (int n = 0; n < names.size() && n < 100; ++n) {
        if (isBlankName(names[n])) continue;   // blank slots dropped entirely
        QvPreset p;
        p.id        = factoryId(n);
        p.name      = names[n].trim();
        p.isFactory = true;
        p.program   = n;
        p.tags      = builtinFactoryTags(n);
        presets_.push_back(std::move(p));
    }
}

void QvPresetStore::loadFactoryMeta() {
    const auto v = readJson(baseDir_.getChildFile(kMetaFile));
    auto* obj = v.getDynamicObject();
    if (obj == nullptr) return;
    for (const auto& prop : obj->getProperties())
        if (auto* p = find(prop.name.toString()))
            if (auto* entry = prop.value.getDynamicObject())
                p->tags = tagsFromVar(entry->getProperty("tags"));
}

void QvPresetStore::loadUserPresets() {
    auto dir = presetsDir();
    if (!dir.isDirectory()) return;
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto v = readJson(f);
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) continue;
        QvPreset p;
        p.id = obj->getProperty("id").toString();
        if (p.id.isEmpty()) p.id = f.getFileNameWithoutExtension();
        p.name      = obj->getProperty("name").toString();
        p.isFactory = false;
        p.tags      = tagsFromVar(obj->getProperty("tags"));
        juce::MemoryOutputStream mos;
        if (juce::Base64::convertFromBase64(mos, obj->getProperty("state").toString()))
            p.stateBlob.append(mos.getData(), mos.getDataSize());
        presets_.push_back(std::move(p));
    }
}

void QvPresetStore::loadTagCatalog() {
    const auto v = readJson(baseDir_.getChildFile(kTagsFile));
    if (auto* obj = v.getDynamicObject()) {
        tagCatalog_    = tagsFromVar(obj->getProperty("tags"));
        defaultsSeeded_ = (bool) obj->getProperty("defaultsSeeded");
    }
    // Every tag actually in use belongs in the catalog.
    for (const auto& p : presets_)
        for (const auto& t : p.tags)
            tagCatalog_.addIfNotAlreadyThere(t);
}

// One-shot: the five defaults enter the catalog the first time a library
// loads; the flag keeps a user's deletion from resurrecting them later.
void QvPresetStore::seedDefaultTags() {
    for (const auto* t : kDefaultTags)
        tagCatalog_.addIfNotAlreadyThere(t);
    defaultsSeeded_ = true;
    writeTagCatalog();
}

// ---------------------------------------------------------------------------
QvPreset* QvPresetStore::find(const juce::String& id) {
    for (auto& p : presets_)
        if (p.id == id) return &p;
    return nullptr;
}

const QvPreset* QvPresetStore::byId(const juce::String& id) const {
    for (const auto& p : presets_)
        if (p.id == id) return &p;
    return nullptr;
}

void QvPresetStore::sortPresets() {
    std::sort(presets_.begin(), presets_.end(), [](const QvPreset& a, const QvPreset& b) {
        if (a.isFactory != b.isFactory) return a.isFactory;   // factory first
        if (a.isFactory) return a.program < b.program;
        const int byName = a.name.compareIgnoreCase(b.name);
        if (byName != 0) return byName < 0;                   // user: alphabetical
        return a.id < b.id;
    });
}

std::vector<const QvPreset*> QvPresetStore::list(bool user,
                                                 const juce::StringArray& tagFilter) const {
    std::vector<const QvPreset*> out;
    for (const auto& p : presets_) {
        if (p.isFactory == user) continue;
        bool hasAll = true;
        for (const auto& t : tagFilter)
            if (!p.tags.contains(t)) { hasAll = false; break; }
        if (hasAll) out.push_back(&p);
    }
    return out;
}

// ---------------------------------------------------------------------------
juce::File QvPresetStore::presetFileFor(const juce::String& id) const {
    return presetsDir().getChildFile(id + ".json");
}

void QvPresetStore::writeUserPreset(const QvPreset& p) {
    presetsDir().createDirectory();
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id",    p.id);
    obj->setProperty("name",  p.name);
    obj->setProperty("tags",  tagsToVar(p.tags));
    obj->setProperty("state", juce::Base64::toBase64(p.stateBlob.getData(), p.stateBlob.getSize()));
    presetFileFor(p.id).replaceWithText(juce::JSON::toString(juce::var(obj)));
}

void QvPresetStore::writeFactoryMeta() {
    auto* root = new juce::DynamicObject();
    for (const auto& p : presets_) {
        if (!p.isFactory) continue;
        if (sameTagSet(p.tags, builtinFactoryTags(p.program))) continue;   // default: no entry
        auto* entry = new juce::DynamicObject();
        entry->setProperty("tags", tagsToVar(p.tags));
        root->setProperty(juce::Identifier(p.id), juce::var(entry));
    }
    baseDir_.createDirectory();
    baseDir_.getChildFile(kMetaFile).replaceWithText(juce::JSON::toString(juce::var(root)));
}

void QvPresetStore::writeTagCatalog() {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("tags", tagsToVar(allTags()));
    obj->setProperty("defaultsSeeded", defaultsSeeded_);
    baseDir_.createDirectory();
    baseDir_.getChildFile(kTagsFile).replaceWithText(juce::JSON::toString(juce::var(obj)));
}

// ---------------------------------------------------------------------------
juce::String QvPresetStore::saveUserPreset(const juce::String& name,
                                           const juce::MemoryBlock& stateBlob) {
    if (name.trim().isEmpty()) return {};
    QvPreset p;
    p.id        = juce::Uuid().toString();   // filesystem-safe
    p.name      = name.trim();
    p.isFactory = false;
    p.stateBlob = stateBlob;
    writeUserPreset(p);
    presets_.push_back(std::move(p));
    const juce::String newId = presets_.back().id;
    sortPresets();
    sendChangeMessage();
    return newId;
}

bool QvPresetStore::deleteUserPreset(const juce::String& id) {
    auto* p = find(id);
    if (p == nullptr || p->isFactory) return false;
    presetFileFor(id).deleteFile();
    presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
                                  [&](const QvPreset& x) { return x.id == id; }),
                   presets_.end());
    sendChangeMessage();
    return true;
}

// ---------------------------------------------------------------------------
bool QvPresetStore::addTag(const juce::String& id, const juce::String& tag) {
    auto* p = find(id);
    if (p == nullptr || tag.trim().isEmpty()) return false;
    p->tags.addIfNotAlreadyThere(tag.trim());
    tagCatalog_.addIfNotAlreadyThere(tag.trim());
    if (p->isFactory) writeFactoryMeta();
    else              writeUserPreset(*p);
    writeTagCatalog();
    sendChangeMessage();
    return true;
}

bool QvPresetStore::removeTag(const juce::String& id, const juce::String& tag) {
    auto* p = find(id);
    if (p == nullptr) return false;
    p->tags.removeString(tag);
    if (p->isFactory) writeFactoryMeta();
    else              writeUserPreset(*p);
    sendChangeMessage();
    return true;
}

juce::StringArray QvPresetStore::allTags() const {
    juce::StringArray out(tagCatalog_);
    for (const auto& p : presets_)
        for (const auto& t : p.tags)
            out.addIfNotAlreadyThere(t);
    return out;
}

void QvPresetStore::createTag(const juce::String& name) {
    const auto t = name.trim();
    if (t.isEmpty() || tagCatalog_.contains(t)) return;
    tagCatalog_.add(t);
    writeTagCatalog();
    sendChangeMessage();
}

void QvPresetStore::deleteTag(const juce::String& name) {
    tagCatalog_.removeString(name);
    bool touchedFactory = false;
    for (auto& p : presets_) {
        if (!p.tags.contains(name)) continue;
        p.tags.removeString(name);
        if (p.isFactory) touchedFactory = true;
        else             writeUserPreset(p);
    }
    if (touchedFactory) writeFactoryMeta();
    writeTagCatalog();
    sendChangeMessage();
}
