#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include <vector>

namespace qverse
{

enum class DeviceModel : int
{
    quadraverb = 0,
    quadraverbPlus = 1,
    quadraverb2 = 2
};

inline const char* toString (DeviceModel model)
{
    switch (model)
    {
        case DeviceModel::quadraverb:     return "quadraverb";
        case DeviceModel::quadraverbPlus: return "quadraverb_plus";
        case DeviceModel::quadraverb2:    return "quadraverb_2";
    }
    return "unknown";
}

inline DeviceModel deviceModelFromString (const juce::String& s)
{
    if (s == "quadraverb") return DeviceModel::quadraverb;
    if (s == "quadraverb_2" || s == "quadraverb2") return DeviceModel::quadraverb2;
    return DeviceModel::quadraverbPlus;
}

/** Opaque parameter address — QV1/Plus uses (function, page). */
struct ParamAddress
{
    int function = 0;
    int page = 0;
    int block = 0;   // QV2
    int param = 0;   // QV2

    bool operator== (const ParamAddress& o) const
    {
        return function == o.function && page == o.page
            && block == o.block && param == o.param;
    }

    juce::String key() const
    {
        return juce::String (function) + "." + juce::String (page);
    }
};

struct ParamMeta
{
    ParamAddress address;
    juce::String id;
    juce::String name;
    juce::String unit;
    juce::String section; // block name
    int min = 0;
    int max = 99;
    int defaultValue = 0;
    juce::StringArray choices;
    int byteOffset = -1;   // into 128-byte program; -1 if unknown
    int byteWidth = 1;     // 1 or 2 (MSB/LSB)
};

class DeviceProfile
{
public:
    virtual ~DeviceProfile() = default;

    virtual DeviceModel getModel() const = 0;
    virtual juce::String getDisplayName() const = 0;
    virtual uint8_t getSysexProductId() const = 0;
    virtual int getProgramByteSize() const = 0;
    virtual int getEncodedProgramSize() const = 0;

    virtual std::vector<ParamMeta> parametersForConfig (int config) const = 0;
    virtual juce::StringArray configNames() const = 0;
};

const DeviceProfile& profileFor (DeviceModel model);
const DeviceProfile& defaultProfile(); // Quadraverb Plus

} // namespace qverse
