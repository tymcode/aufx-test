#include "PatchTranslator.h"

namespace qverse
{

bool TranslationReport::isClean() const
{
    for (const auto& item : items)
        if (item.classification != TranslationClass::mapsCleanly)
            return false;
    return true;
}

TranslationReport PatchTranslator::translate (const QuadraverbProgram& source,
                                              DeviceModel targetModel,
                                              QuadraverbProgram& outProgram)
{
    TranslationReport report;
    report.sourceModel = source.model;
    report.targetModel = targetModel;

    if (source.model == targetModel)
    {
        outProgram = source;
        outProgram.model = targetModel;
        // Identity — no report items needed.
        return report;
    }

    // Cross-model: copy what we can by (function, page) identity where both
    // profiles expose the same address; otherwise mark noEquivalent.
    outProgram = {};
    outProgram.model = targetModel;
    outProgram.name = source.name;
    outProgram.inLevel = source.inLevel;
    outProgram.outLevel = source.outLevel;
    outProgram.config = source.config;
    outProgram.setParam (7, 0, source.config);

    const auto& srcProfile = profileFor (source.model);
    const auto& dstProfile = profileFor (targetModel);
    const auto srcParams = srcProfile.parametersForConfig (source.config);
    const auto dstParams = dstProfile.parametersForConfig (source.config);

    auto findDst = [&] (const ParamAddress& addr) -> const ParamMeta*
    {
        for (const auto& p : dstParams)
            if (p.address == addr)
                return &p;
        return nullptr;
    };

    for (const auto& sp : srcParams)
    {
        if (! source.isKnown (sp.address.function, sp.address.page))
            continue;

        TranslationItem item;
        item.sourceAddress = sp.address;
        item.name = sp.name;
        item.section = sp.section;
        item.sourceValue = source.getParam (sp.address.function, sp.address.page);
        item.targetDefault = sp.defaultValue;

        if (const auto* dp = findDst (sp.address))
        {
            item.targetAddress = dp->address;
            int v = item.sourceValue;
            if (v < dp->min || v > dp->max)
            {
                v = juce::jlimit (dp->min, dp->max, v);
                item.classification = TranslationClass::approximated;
            }
            else
            {
                item.classification = TranslationClass::mapsCleanly;
            }
            item.proposedValue = v;
            item.targetDefault = dp->defaultValue;
            outProgram.setParam (dp->address.function, dp->address.page, v);
        }
        else
        {
            item.classification = TranslationClass::noEquivalent;
            item.proposedValue = item.targetDefault;
        }

        if (item.classification != TranslationClass::mapsCleanly)
            report.items.push_back (item);
    }

    outProgram.flushValuesToBytes();
    return report;
}

} // namespace qverse
