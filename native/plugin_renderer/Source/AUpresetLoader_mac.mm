#import <Foundation/Foundation.h>

#include <JuceHeader.h>
#include "AUpresetLoader.h"

namespace
{
    bool copyNSData (NSData* data, juce::MemoryBlock& outState, juce::String& error)
    {
        if (data == nil || [data length] == 0)
        {
            error = "Preset plist did not contain state bytes";
            return false;
        }

        outState.setSize ((size_t) [data length]);
        [data getBytes: outState.getData() length: outState.getSize()];
        return true;
    }

    bool extractFromPlistObject (id plist, juce::MemoryBlock& outState, juce::String& error)
    {
        if (! [plist isKindOfClass: [NSDictionary class]])
        {
            error = "Preset root is not a dictionary";
            return false;
        }

        NSDictionary* dict = (NSDictionary*) plist;
        NSArray* keys = @[ @"jucePluginState", @"data", @"state" ];

        for (NSString* key in keys)
        {
            id value = dict[key];
            if ([value isKindOfClass: [NSData class]] && [((NSData*) value) length] > 0)
                return copyNSData ((NSData*) value, outState, error);
        }

        error = "Preset plist did not contain a non-empty state blob (jucePluginState/data/state)";
        return false;
    }
}

bool AUpresetLoader::extractStateBytes (const juce::MemoryBlock& hostOrPluginState,
                                        juce::MemoryBlock& outState,
                                        juce::String& error)
{
    if (hostOrPluginState.isEmpty())
    {
        error = "Plugin returned empty state";
        return false;
    }

    // Already the processor blob (e.g. in-process / non-AU wrappers).
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (hostOrPluginState.getData(),
                                                           (int) hostOrPluginState.getSize()))
        if (xml->hasTagName ("QDV1"))
        {
            outState = hostOrPluginState;
            return true;
        }

    @autoreleasepool
    {
        NSData* data = [NSData dataWithBytes: hostOrPluginState.getData()
                                      length: hostOrPluginState.getSize()];
        NSError* nsError = nil;
        id plist = [NSPropertyListSerialization propertyListWithData: data
                                                             options: NSPropertyListImmutable
                                                              format: nil
                                                               error: &nsError];
        if (plist == nil)
        {
            error = nsError != nil ? juce::String ([nsError localizedDescription].UTF8String)
                                   : juce::String ("Host state is neither QDV1 XML nor an AU ClassInfo plist");
            return false;
        }

        return extractFromPlistObject (plist, outState, error);
    }
}

bool AUpresetLoader::loadStateBytes (const juce::File& presetFile,
                                     juce::MemoryBlock& outState,
                                     juce::String& error)
{
    if (presetFile.getFileExtension().toLowerCase() != ".aupreset")
    {
        error = "Expected .aupreset extension";
        return false;
    }

    juce::MemoryBlock fileBytes;
    if (! presetFile.loadFileAsData (fileBytes) || fileBytes.isEmpty())
    {
        error = "Failed to read preset file";
        return false;
    }

    return extractStateBytes (fileBytes, outState, error);
}
