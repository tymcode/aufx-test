#pragma once

#include "SysexDeviceModule.h"
#include <memory>
#include <vector>

/**
 * Singleton list of all built-in sysex device modules. findModule() is the
 * CoreMIDI-metadata auto-match fallback; the primary selection path is the
 * user's explicit choice in MIDI Setup (matched by display name), because
 * MIDI interfaces usually mask the identity of the device behind them.
 */
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
