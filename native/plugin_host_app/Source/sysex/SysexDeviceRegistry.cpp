#include "SysexDeviceRegistry.h"
#include "DpProSysex.h"
#include "QuadraverbSysex.h"

SysexDeviceRegistry& SysexDeviceRegistry::get()
{
    static SysexDeviceRegistry instance;
    return instance;
}

SysexDeviceRegistry::SysexDeviceRegistry()
{
    registerModule (std::make_unique<QuadraverbSysex>());
    registerModule (std::make_unique<DpProSysex>());
}

void SysexDeviceRegistry::registerModule (std::unique_ptr<SysexDeviceModule> module)
{
    if (module != nullptr)
        modules.push_back (std::move (module));
}

const SysexDeviceModule* SysexDeviceRegistry::findModule (const juce::String& manufacturer,
                                                          const juce::String& model,
                                                          const juce::String& deviceName) const
{
    for (const auto& module : modules)
        if (module->matches (manufacturer, model, deviceName))
            return module.get();

    return nullptr;
}
