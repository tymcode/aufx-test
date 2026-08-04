#include "PatchContextManager.h"
#include "Utf8.h"
#include "DeviceProfile.h"

namespace qverse
{

PatchContextManager::PatchContextManager()
{
    addEmpty (utf8 ("Context A"));
    addEmpty (utf8 ("Context B"));
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
    ctx.name = name;
    ctx.program.model = DeviceModel::quadraverbPlus;
    ctx.program.config = 4;
    ctx.program.setParam (7, 0, 4);
    ctx.program.setName (name);
    ctx.program.flushValuesToBytes();
    contexts.push_back (std::move (ctx));
    activeIndex = (int) contexts.size() - 1;
    notify();
    return activeIndex;
}

int PatchContextManager::addProgram (QuadraverbProgram program, const juce::String& name, const juce::File& source)
{
    PatchContext ctx;
    ctx.id = makeId();
    ctx.name = name;
    ctx.program = std::move (program);
    ctx.sourceFile = source;
    if (ctx.program.name.isEmpty())
        ctx.program.setName (name);
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
        return addProgram (std::move (copy), a->name + utf8 (" copy"), a->sourceFile);
    }
    return -1;
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
        ce->setProperty ("sourceFile", c.sourceFile.getFullPathName());
        ce->setProperty ("program", c.program.toVar());
        // Persist protect-from-adjustment with the context representation.
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
        addEmpty (utf8 ("Context A"));
        addEmpty (utf8 ("Context B"));
    }
    notify();
}

} // namespace qverse
