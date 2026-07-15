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

    @autoreleasepool
    {
        NSString* path = [NSString stringWithUTF8String: presetFile.getFullPathName().toRawUTF8()];
        NSError* nsError = nil;
        NSData* fileData = [NSData dataWithContentsOfFile: path options: 0 error: &nsError];

        if (fileData == nil)
        {
            error = nsError != nil ? juce::String ([nsError localizedDescription].UTF8String)
                                   : juce::String ("Failed to read preset file");
            return false;
        }

        id plist = [NSPropertyListSerialization propertyListWithData: fileData
                                                             options: NSPropertyListImmutable
                                                              format: nil
                                                               error: &nsError];
        if (plist == nil)
        {
            error = nsError != nil ? juce::String ([nsError localizedDescription].UTF8String)
                                   : juce::String ("Failed to parse preset plist");
            return false;
        }

        if ([plist isKindOfClass: [NSDictionary class]])
        {
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

        error = "Preset root is not a dictionary";
        return false;
    }
}
