#include "PatchContextManager.h"
#include "Utf8.h"
#include "DeviceProfile.h"

namespace qverse
{

PatchContextManager::PatchContextManager()
{
    addEmpty (utf8 ("Patch Context A"));
    addEmpty (utf8 ("Patch Context B"));
    activeIndex = 0;
}

void PatchContextManager::notify()
{
    if (onChanged)
        onChanged();
}

juce::String PatchContextManager::makeId() const
{
    return juce::Uuid().toDashedString();
}

juce::String PatchContextManager::nextDefaultName() const
{
    for (int n = 0; n < 26; ++n)
    {
        const auto candidate = utf8 ("Patch Context ")
            + juce::String::charToString ((juce::juce_wchar) ('A' + n));
        bool used = false;
        for (const auto& c : contexts)
            if (c.name == candidate)
            {
                used = true;
                break;
            }
        if (! used)
            return candidate;
    }
    return utf8 ("Patch Context ") + juce::String ((int) contexts.size() + 1);
}

void PatchContextManager::setActiveIndex (int index)
{
    if (index >= 0 && index < (int) contexts.size())
    {
        activeIndex = index;
        notify();
    }
}

PatchContext* PatchContextManager::getActive()
{
    return get (activeIndex);
}

const PatchContext* PatchContextManager::getActive() const
{
    return get (activeIndex);
}

PatchContext* PatchContextManager::get (int index)
{
    if (index < 0 || index >= (int) contexts.size())
        return nullptr;
    return &contexts[(size_t) index];
}

const PatchContext* PatchContextManager::get (int index) const
{
    if (index < 0 || index >= (int) contexts.size())
        return nullptr;
    return &contexts[(size_t) index];
}

juce::StringArray PatchContextManager::getNames() const
{
    juce::StringArray names;
    for (const auto& c : contexts)
        names.add (c.name + (c.dirty ? utf8 (" *") : juce::String()));
    return names;
}

int PatchContextManager::addEmpty (const juce::String& name)
{
    PatchContext ctx;
    ctx.id = makeId();
    ctx.name = name.isNotEmpty() ? name : nextDefaultName();
    ctx.program.model = DeviceModel::quadraverbPlus;
    ctx.program.config = 4;
    ctx.program.setParam (7, 0, 4);
    ctx.program.setName (ctx.name);
    ctx.program.flushValuesToBytes();
    ctx.compare = true;
    contexts.push_back (std::move (ctx));
    activeIndex = (int) contexts.size() - 1;
    notify();
    return activeIndex;
}

int PatchContextManager::addProgram (QuadraverbProgram program,
                                     const juce::String& name,
                                     const juce::File& source,
                                     bool enableCompare)
{
    PatchContext ctx;
    ctx.id = makeId();
    ctx.name = name.isNotEmpty() ? name : nextDefaultName();
    ctx.program = std::move (program);
    ctx.sourceFile = source;
    ctx.compare = enableCompare;
    if (ctx.program.name.isEmpty())
        ctx.program.setName (ctx.name);
    contexts.push_back (std::move (ctx));
    activeIndex = (int) contexts.size() - 1;
    notify();
    return activeIndex;
}

int PatchContextManager::duplicateActive()
{
    if (auto* a = getActive())
    {
        auto copy = a->program;
        const int idx = addProgram (std::move (copy), a->name + utf8 (" copy"), a->sourceFile, a->compare);
        return idx;
    }
    return -1;
}

bool PatchContextManager::renameActive (const juce::String& newName)
{
    auto* a = getActive();
    if (a == nullptr)
        return false;
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return false;
    a->name = trimmed;
    a->program.setName (trimmed);
    a->dirty = true;
    notify();
    return true;
}

void PatchContextManager::setActiveCompare (bool enabled)
{
    if (auto* a = getActive())
    {
        if (a->compare != enabled)
        {
            a->compare = enabled;
            notify();
        }
    }
}

bool PatchContextManager::dropActive (bool force)
{
    return dropAt (activeIndex, force);
}

bool PatchContextManager::dropAt (int index, bool force)
{
    if (contexts.size() <= 1)
        return false;
    if (auto* c = get (index))
    {
        if (c->dirty && ! force)
            return false;
        contexts.erase (contexts.begin() + index);
        if (activeIndex >= (int) contexts.size())
            activeIndex = (int) contexts.size() - 1;
        notify();
        return true;
    }
    return false;
}

void PatchContextManager::markActiveDirty()
{
    if (auto* a = getActive())
    {
        a->dirty = true;
        notify();
    }
}

void PatchContextManager::clearActiveDirty()
{
    if (auto* a = getActive())
    {
        a->dirty = false;
        notify();
    }
}

std::vector<ParamAddress> PatchContextManager::differingParams() const
{
    std::vector<ParamAddress> diffs;
    const auto* active = getActive();
    if (active == nullptr || contexts.size() < 2)
        return diffs;

    const auto params = profileFor (active->program.model).parametersForConfig (active->program.config);
    for (const auto& pm : params)
    {
        const int av = active->program.getParam (pm.address.function, pm.address.page, pm.defaultValue);
        for (size_t i = 0; i < contexts.size(); ++i)
        {
            if ((int) i == activeIndex)
                continue;
            if (! contexts[i].compare)
                continue;
            const int ov = contexts[i].program.getParam (pm.address.function, pm.address.page, pm.defaultValue);
            if (av != ov)
            {
                diffs.push_back (pm.address);
                break;
            }
        }
    }
    return diffs;
}

void PatchContextManager::copyParamFrom (int fromIndex, const ParamAddress& addr)
{
    auto* active = getActive();
    auto* from = get (fromIndex);
    if (active == nullptr || from == nullptr)
        return;
    active->program.setParam (addr.function, addr.page,
                              from->program.getParam (addr.function, addr.page));
    active->dirty = true;
    notify();
}

void PatchContextManager::copyParamTo (int toIndex, const ParamAddress& addr)
{
    auto* active = getActive();
    auto* to = get (toIndex);
    if (active == nullptr || to == nullptr)
        return;
    to->program.setParam (addr.function, addr.page,
                          active->program.getParam (addr.function, addr.page));
    to->dirty = true;
    notify();
}

void PatchContextManager::copySectionFrom (int fromIndex, const juce::String& section)
{
    auto* active = getActive();
    if (active == nullptr)
        return;
    const auto params = profileFor (active->program.model).parametersForConfig (active->program.config);
    for (const auto& pm : params)
        if (pm.section == section)
            copyParamFrom (fromIndex, pm.address);
}

void PatchContextManager::copySectionTo (int toIndex, const juce::String& section)
{
    auto* active = getActive();
    if (active == nullptr)
        return;
    const auto params = profileFor (active->program.model).parametersForConfig (active->program.config);
    for (const auto& pm : params)
        if (pm.section == section)
            copyParamTo (toIndex, pm.address);
}

juce::var PatchContextManager::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("activeIndex", activeIndex);
    juce::Array<juce::var> arr;
    for (const auto& c : contexts)
    {
        auto* ce = new juce::DynamicObject();
        ce->setProperty ("id", c.id);
        ce->setProperty ("name", c.name);
        ce->setProperty ("dirty", c.dirty);
        ce->setProperty ("compare", c.compare);
        ce->setProperty ("sourceFile", c.sourceFile.getFullPathName());
        ce->setProperty ("program", c.program.toVar());
        arr.add (ce);
    }
    obj->setProperty ("contexts", arr);
    return obj;
}

void PatchContextManager::fromVar (const juce::var& v)
{
    contexts.clear();
    if (auto* obj = v.getDynamicObject())
    {
        if (auto* arr = obj->getProperty ("contexts").getArray())
        {
            for (const auto& item : *arr)
                if (auto* ce = item.getDynamicObject())
                {
                    PatchContext c;
                    c.id = ce->getProperty ("id").toString();
                    if (c.id.isEmpty())
                        c.id = makeId();
                    c.name = ce->getProperty ("name").toString();
                    c.dirty = (bool) ce->getProperty ("dirty");
                    if (ce->hasProperty ("compare"))
                        c.compare = (bool) ce->getProperty ("compare");
                    else
                        c.compare = true;
                    c.sourceFile = juce::File (ce->getProperty ("sourceFile").toString());
                    c.program = QuadraverbProgram::fromVar (ce->getProperty ("program"));
                    contexts.push_back (std::move (c));
                }
        }
        activeIndex = juce::jlimit (0, juce::jmax (0, (int) contexts.size() - 1),
                                    (int) obj->getProperty ("activeIndex"));
    }
    if (contexts.empty())
    {
        addEmpty (utf8 ("Patch Context A"));
        addEmpty (utf8 ("Patch Context B"));
    }
    notify();
}

} // namespace qverse
