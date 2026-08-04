#pragma once

#include <JuceHeader.h>
#include "Utf8.h"
#include "QuadraverbProgram.h"
#include <functional>
#include <vector>

namespace qverse
{

struct PatchContext
{
    juce::String id;
    juce::String name;
    QuadraverbProgram program;
    juce::File sourceFile;
    bool dirty = false;
    /** When true, this context participates in Compare / comparison report. */
    bool compare = true;
};

class PatchContextManager
{
public:
    PatchContextManager();

    int size() const { return (int) contexts.size(); }
    int getActiveIndex() const { return activeIndex; }
    void setActiveIndex (int index);

    PatchContext* getActive();
    const PatchContext* getActive() const;
    PatchContext* get (int index);
    const PatchContext* get (int index) const;

    juce::StringArray getNames() const;

    /** Next unused "Patch Context A/B/…" name. */
    juce::String nextDefaultName() const;

    int addEmpty (const juce::String& name = {});
    int addProgram (QuadraverbProgram program,
                    const juce::String& name,
                    const juce::File& source = {},
                    bool enableCompare = true);
    int duplicateActive();
    bool renameActive (const juce::String& newName);
    void setActiveCompare (bool enabled);
    bool dropActive (bool force = false);
    bool dropAt (int index, bool force = false);

    void markActiveDirty();
    void clearActiveDirty();

    std::vector<ParamAddress> differingParams() const;

    void copyParamFrom (int fromIndex, const ParamAddress& addr);
    void copyParamTo (int toIndex, const ParamAddress& addr);
    void copySectionFrom (int fromIndex, const juce::String& section);
    void copySectionTo (int toIndex, const juce::String& section);

    juce::var toVar() const;
    void fromVar (const juce::var& v);

    std::function<void()> onChanged;

private:
    void notify();
    juce::String makeId() const;

    std::vector<PatchContext> contexts;
    int activeIndex = 0;
};

} // namespace qverse
