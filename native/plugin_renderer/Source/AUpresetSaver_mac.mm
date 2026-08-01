#import <Foundation/Foundation.h>

#include <JuceHeader.h>
#include "AUpresetSaver.h"

bool AUpresetSaver::saveStateBytes (const juce::File& presetFile,
                                    const juce::MemoryBlock& state,
                                    juce::String& error)
{
    if (state.getSize() == 0)
    {
        error = "Plugin state is empty";
        return false;
    }

    @autoreleasepool
    {
        NSData* blob = [NSData dataWithBytes: state.getData() length: state.getSize()];
        NSDictionary* dict = @{
            @"jucePluginState": blob,
            @"data": blob,
        };

        NSError* nsError = nil;
        NSData* plistData = [NSPropertyListSerialization dataWithPropertyList: dict
                                                                       format: NSPropertyListBinaryFormat_v1_0
                                                                      options: 0
                                                                        error: &nsError];
        if (plistData == nil)
        {
            error = nsError != nil ? juce::String ([nsError localizedDescription].UTF8String)
                                   : juce::String ("Failed to serialize preset plist");
            return false;
        }

        presetFile.getParentDirectory().createDirectory();
        NSString* path = [NSString stringWithUTF8String: presetFile.getFullPathName().toRawUTF8()];
        if (! [plistData writeToFile: path options: NSDataWritingAtomic error: &nsError])
        {
            error = nsError != nil ? juce::String ([nsError localizedDescription].UTF8String)
                                   : juce::String ("Failed to write preset file");
            return false;
        }
    }

    return true;
}
