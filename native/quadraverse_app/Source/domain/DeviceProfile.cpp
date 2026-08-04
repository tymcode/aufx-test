#include "DeviceProfile.h"
#include "Utf8.h"
#include "QvParameters.h"
#include <JuceHeader.h>

namespace qverse
{

class QuadraverbPlusProfile final : public DeviceProfile
{
public:
    DeviceModel getModel() const override { return DeviceModel::quadraverbPlus; }
    juce::String getDisplayName() const override { return utf8 ("Quadraverb Plus"); }
    uint8_t getSysexProductId() const override { return 0x02; }
    int getProgramByteSize() const override { return 128; }
    int getEncodedProgramSize() const override { return 147; }

    juce::StringArray configNames() const override
    {
        juce::StringArray names;
        for (uint8_t i = 0; i < qv::kNumConfigs; ++i)
            names.add (utf8 (qv::kConfigs[i].name));
        return names;
    }

    std::vector<ParamMeta> parametersForConfig (int config) const override
    {
        std::vector<ParamMeta> out;
        if (config < 0 || config >= (int) qv::kNumConfigs)
            config = 4;

        const auto& cfg = qv::kConfigs[(size_t) config];

        {
            ParamMeta pm;
            pm.address = { qv::kConfigFunction, qv::kConfigPage, 0, 0 };
            pm.id = utf8 ("config");
            pm.name = utf8 ("Configuration");
            pm.section = utf8 ("Config");
            pm.min = 0;
            pm.max = (int) qv::kNumConfigs - 1;
            pm.defaultValue = 4;
            for (uint8_t i = 0; i < qv::kNumConfigs; ++i)
                pm.choices.add (utf8 (qv::kConfigs[i].name));
            pm.byteOffset = 68;
            out.push_back (pm);
        }

        for (uint8_t bi = 0; bi < cfg.blockCount; ++bi)
        {
            const auto& blk = cfg.blocks[bi];
            const juce::String section = utf8 (blk.name);

            if (blk.page0SelectsMode)
            {
                ParamMeta modePm;
                modePm.address = { (int) blk.function, 0, 0, 0 };
                modePm.id = section.toLowerCase() + utf8 ("_mode");
                modePm.name = section + utf8 (" Mode");
                modePm.section = section;
                modePm.min = blk.modeMin;
                modePm.max = blk.modeMax;
                modePm.defaultValue = blk.modeDefault;
                for (uint8_t mi = 0; mi < blk.modeCount; ++mi)
                    modePm.choices.add (utf8 (blk.modes[mi].name));
                out.push_back (modePm);
            }

            for (uint8_t mi = 0; mi < blk.modeCount; ++mi)
            {
                const auto& mode = blk.modes[mi];
                for (uint8_t pi = 0; pi < mode.count; ++pi)
                {
                    const auto& p = mode.params[pi];
                    // Skip analog I/O — not in this table; exclude nothing here
                    // except we never expose in/out level as block params.
                    ParamMeta pm;
                    pm.address = { (int) p.function, (int) p.page, 0, 0 };
                    pm.id = utf8 (p.id);
                    pm.name = utf8 (p.name);
                    pm.unit = p.unit != nullptr ? utf8 (p.unit) : juce::String();
                    pm.section = section;
                    pm.min = p.min;
                    pm.max = p.max;
                    pm.defaultValue = p.defaultValue;
                    if (p.choices != nullptr)
                        for (uint8_t c = 0; c < p.choiceCount; ++c)
                            pm.choices.add (utf8 (p.choices[c]));
                    out.push_back (pm);
                }
            }
        }

        return out;
    }
};

class QuadraverbOriginalProfile final : public DeviceProfile
{
    QuadraverbPlusProfile plus;
public:
    DeviceModel getModel() const override { return DeviceModel::quadraverb; }
    juce::String getDisplayName() const override { return utf8 ("Quadraverb"); }
    uint8_t getSysexProductId() const override { return 0x02; }
    int getProgramByteSize() const override { return 128; }
    int getEncodedProgramSize() const override { return 147; }
    juce::StringArray configNames() const override
    {
        // Original published table lists fewer configs; still seed from Plus table.
        return plus.configNames();
    }
    std::vector<ParamMeta> parametersForConfig (int config) const override
    {
        return plus.parametersForConfig (config);
    }
};

/** Stub profile — full QV2 implementation is a future phase. */
class Quadraverb2Profile final : public DeviceProfile
{
public:
    DeviceModel getModel() const override { return DeviceModel::quadraverb2; }
    juce::String getDisplayName() const override { return utf8 ("Quadraverb 2"); }
    uint8_t getSysexProductId() const override { return 0x0F; }
    int getProgramByteSize() const override { return 256; }
    int getEncodedProgramSize() const override { return 306; }
    juce::StringArray configNames() const override { return { utf8 ("QV2 (routing matrix)") }; }
    std::vector<ParamMeta> parametersForConfig (int) const override { return {}; }
};

const DeviceProfile& profileFor (DeviceModel model)
{
    static QuadraverbPlusProfile plus;
    static QuadraverbOriginalProfile original;
    static Quadraverb2Profile qv2;
    switch (model)
    {
        case DeviceModel::quadraverb: return original;
        case DeviceModel::quadraverb2: return qv2;
        case DeviceModel::quadraverbPlus:
        default: return plus;
    }
}

const DeviceProfile& defaultProfile()
{
    return profileFor (DeviceModel::quadraverbPlus);
}

} // namespace qverse
