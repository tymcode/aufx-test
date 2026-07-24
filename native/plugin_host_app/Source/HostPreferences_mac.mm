#import <Foundation/Foundation.h>

#include "HostPreferences.h"

juce::String HostPreferences::readSystemPreference (const juce::String& key) const
{
    @autoreleasepool
    {
        NSString* appId = [NSString stringWithUTF8String: bundleId];
        NSString* keyStr = [NSString stringWithUTF8String: key.toRawUTF8()];
        if (appId == nil || keyStr == nil)
            return {};

        CFPropertyListRef value = CFPreferencesCopyValue ((__bridge CFStringRef) keyStr,
                                                          (__bridge CFStringRef) appId,
                                                          kCFPreferencesAnyUser,
                                                          kCFPreferencesCurrentHost);
        if (value == nullptr)
            return {};

        juce::String result;
        if (CFGetTypeID (value) == CFStringGetTypeID())
            result = juce::String::fromCFString ((CFStringRef) value).trim();

        CFRelease (value);
        return result;
    }
}
