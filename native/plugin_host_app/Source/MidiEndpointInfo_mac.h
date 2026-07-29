#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Look up CoreMIDI manufacturer/model for an endpoint display name.
 * Plain-C interface (char buffers, int return) so the Objective-C++ .mm
 * implementation can be used from pure C++ without leaking CoreFoundation
 * types into JUCE headers. Returns 1 if the endpoint was found.
 */
int midiEndpointLookupMeta (const char* endpointName,
                            char* manufacturerOut, size_t manufacturerSize,
                            char* modelOut, size_t modelSize);

#ifdef __cplusplus
}
#endif
