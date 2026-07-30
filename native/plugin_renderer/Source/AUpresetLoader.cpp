#include "AUpresetLoader.h"

#if defined(JUCE_PLUGIN_RENDERER_NO_MAC_PRESET)
namespace
{
    juce::MemoryBlock decodeBase64DataElement (const juce::String& encoded)
    {
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64 (decoded, encoded))
            return decoded.getMemoryBlock();

        return {};
    }

    bool extractStateFromXmlPlist (const juce::XmlElement& root, juce::MemoryBlock& outState, juce::String& error)
    {
        const juce::XmlElement* dict = root.getChildByName ("dict");
        if (dict == nullptr)
        {
            error = "Plist root does not contain a <dict> element";
            return false;
        }

        for (int i = 0; i < dict->getNumChildElements(); ++i)
        {
            const auto* keyElement = dict->getChildElement (i);
            if (keyElement == nullptr || ! keyElement->hasTagName ("key"))
                continue;

            const auto key = keyElement->getAllSubText().trim();
            const auto* valueElement = dict->getChildElement (++i);
            if (valueElement == nullptr)
                continue;

            if (key == "jucePluginState" || key == "data" || key == "state")
            {
                if (valueElement->hasTagName ("data"))
                {
                    outState = decodeBase64DataElement (valueElement->getAllSubText());
                    if (outState.getSize() > 0)
                        return true;
                }
                else if (valueElement->hasTagName ("string"))
                {
                    outState = decodeBase64DataElement (valueElement->getAllSubText());
                    if (outState.getSize() > 0)
                        return true;
                }
            }
        }

        error = "No plugin state blob found in plist (expected data/jucePluginState/state key)";
        return false;
    }

    bool loadXmlPlist (const juce::File& presetFile, juce::MemoryBlock& outState, juce::String& error)
    {
        const auto xml = juce::parseXML (presetFile);
        if (xml == nullptr)
        {
            error = "Failed to parse plist XML: " + presetFile.getFullPathName();
            return false;
        }

        return extractStateFromXmlPlist (*xml, outState, error);
    }
}

bool AUpresetLoader::loadStateBytes (const juce::File& presetFile,
                                     juce::MemoryBlock& outState,
                                     juce::String& error)
{
    if (presetFile.getFileExtension().toLowerCase() != ".aupreset")
    {
        error = "Only .aupreset files are supported on this platform build";
        return false;
    }

    return loadXmlPlist (presetFile, outState, error);
}
#endif
