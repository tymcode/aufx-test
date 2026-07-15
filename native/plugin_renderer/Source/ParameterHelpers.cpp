#include "ParameterHelpers.h"

namespace
{
    juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        for (auto* param : plugin.getParameters())
        {
            if (param == nullptr)
                continue;

            if (param->getName (256).equalsIgnoreCase (name))
                return param;
        }

        return nullptr;
    }

    float parseBoolOrFloat (const juce::String& raw, bool& ok)
    {
        ok = true;
        const auto lower = raw.trim().toLowerCase();

        if (lower == "true" || lower == "on" || lower == "yes")
            return 1.0f;

        if (lower == "false" || lower == "off" || lower == "no")
            return 0.0f;

        return raw.getFloatValue();
    }
}

void ParameterHelpers::applyOverrides (juce::AudioPluginInstance& plugin,
                                       const std::vector<std::pair<juce::String, juce::String>>& overrides,
                                       juce::String& error)
{
    for (const auto& [name, rawValue] : overrides)
    {
        auto* param = findParameter (plugin, name);

        if (param == nullptr)
        {
            error = "Unknown parameter: " + name;
            return;
        }

        bool ok = true;
        const auto value = parseBoolOrFloat (rawValue, ok);

        if (! ok)
        {
            error = "Invalid value for parameter " + name + ": " + rawValue;
            return;
        }

        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
        }
        else
        {
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        }
    }
}

juce::String ParameterHelpers::dumpParametersJson (juce::AudioPluginInstance& plugin)
{
    juce::Array<juce::var> params;

    for (auto* param : plugin.getParameters())
    {
        if (param == nullptr)
            continue;

        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty ("name", param->getName (256));
        obj->setProperty ("value", param->getValue());

        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            obj->setProperty ("text", ranged->getText (ranged->getValue(), 16));

        params.add (juce::var (obj.get()));
    }

    return juce::JSON::toString (juce::var (params), true);
}
