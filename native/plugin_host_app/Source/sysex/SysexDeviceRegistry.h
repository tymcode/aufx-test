#pragma once

#include "SysexDeviceModule.h"
#include <memory>
#include <vector>

class SysexDeviceRegistry
{
public:
    static SysexDeviceRegistry& get();

    void registerModule (std::unique_ptr<SysexDeviceModule> module);

    const SysexDeviceModule* findModule (const juce::String& manufacturer,
                                         const juce::String& model,
                                         const juce::String& deviceName) const;

    const std::vector<std::unique_ptr<SysexDeviceModule>>& getModules() const { return modules; }

private:
    SysexDeviceRegistry();
    std::vector<std::unique_ptr<SysexDeviceModule>> modules;
};
