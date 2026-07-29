#include "MidiEndpointInfo_mac.h"

#if defined(__APPLE__)
#import <CoreMIDI/CoreMIDI.h>
#include <string.h>

namespace
{
    void copyCfString (CFStringRef ref, char* out, size_t outSize)
    {
        if (out == nullptr || outSize == 0)
            return;
        out[0] = '\0';
        if (ref == nullptr)
            return;
        CFStringGetCString (ref, out, (CFIndex) outSize, kCFStringEncodingUTF8);
    }

    void copyMidiProperty (MIDIObjectRef object,
                           CFStringRef property,
                           char* out,
                           size_t outSize)
    {
        if (out == nullptr || outSize == 0 || object == 0)
            return;

        CFStringRef valueRef = nullptr;
        if (MIDIObjectGetStringProperty (object, property, &valueRef) == noErr && valueRef != nullptr)
        {
            copyCfString (valueRef, out, outSize);
            CFRelease (valueRef);
        }

    }

    bool matchesEndpointName (const char* lhs, const char* rhs)
    {
        if (lhs == nullptr || rhs == nullptr)
            return false;
        return strcmp (lhs, rhs) == 0;
    }

    /**
     * Find the endpoint with the given display name and pull manufacturer/
     * model from the most specific CoreMIDI object that has them: endpoint
     * first, then its entity, then the parent device. Endpoints frequently
     * have neither property set (the driver puts them on the device object),
     * and multi-port interfaces may expose nothing meaningful at all — in
     * which case the strings stay empty and callers show "(unknown)".
     *
     * Matching is by name because that is all juce::MidiDeviceInfo exposes;
     * duplicate endpoint names would return the first hit. Acceptable for a
     * single-interface test rig. TODO: match on kMIDIPropertyUniqueID if
     * multi-interface setups ever matter.
     */
    bool lookupInList (bool sources, const char* endpointName,
                       char* manufacturerOut, size_t manufacturerSize,
                       char* modelOut, size_t modelSize)
    {
        const ItemCount n = sources ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < n; ++i)
        {
            MIDIEndpointRef endpoint = sources ? MIDIGetSource (i) : MIDIGetDestination (i);
            CFStringRef nameRef = nullptr;
            if (MIDIObjectGetStringProperty (endpoint, kMIDIPropertyName, &nameRef) != noErr || nameRef == nullptr)
                continue;

            char name[512];
            copyCfString (nameRef, name, sizeof (name));
            CFRelease (nameRef);

            if (! matchesEndpointName (name, endpointName))
                continue;

            if (manufacturerOut != nullptr && manufacturerSize > 0)
                manufacturerOut[0] = '\0';
            if (modelOut != nullptr && modelSize > 0)
                modelOut[0] = '\0';

            copyMidiProperty (endpoint, kMIDIPropertyManufacturer,
                              manufacturerOut, manufacturerSize);
            copyMidiProperty (endpoint, kMIDIPropertyModel,
                              modelOut, modelSize);

            MIDIEntityRef entity = 0;
            if (MIDIEndpointGetEntity (endpoint, &entity) == noErr && entity != 0)
            {
                if (manufacturerOut != nullptr && manufacturerOut[0] == '\0')
                    copyMidiProperty (entity, kMIDIPropertyManufacturer,
                                      manufacturerOut, manufacturerSize);
                if (modelOut != nullptr && modelOut[0] == '\0')
                    copyMidiProperty (entity, kMIDIPropertyModel,
                                      modelOut, modelSize);

                MIDIDeviceRef device = 0;
                if (MIDIEntityGetDevice (entity, &device) == noErr && device != 0)
                {
                    if (manufacturerOut != nullptr && manufacturerOut[0] == '\0')
                        copyMidiProperty (device, kMIDIPropertyManufacturer,
                                          manufacturerOut, manufacturerSize);
                    if (modelOut != nullptr && modelOut[0] == '\0')
                        copyMidiProperty (device, kMIDIPropertyModel,
                                          modelOut, modelSize);
                }
            }

            return true;
        }
        return false;
    }
}

int midiEndpointLookupMeta (const char* endpointName,
                            char* manufacturerOut, size_t manufacturerSize,
                            char* modelOut, size_t modelSize)
{
    if (endpointName == nullptr)
        return 0;

    if (lookupInList (true, endpointName, manufacturerOut, manufacturerSize, modelOut, modelSize))
        return 1;
    if (lookupInList (false, endpointName, manufacturerOut, manufacturerSize, modelOut, modelSize))
        return 1;
    return 0;
}

#else

// Non-Apple stub: no CoreMIDI, so metadata is simply unavailable.
int midiEndpointLookupMeta (const char*, char* manufacturerOut, size_t manufacturerSize,
                            char* modelOut, size_t modelSize)
{
    if (manufacturerOut != nullptr && manufacturerSize > 0)
        manufacturerOut[0] = '\0';
    if (modelOut != nullptr && modelSize > 0)
        modelOut[0] = '\0';
    return 0;
}

#endif
