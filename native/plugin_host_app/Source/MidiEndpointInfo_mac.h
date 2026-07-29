#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Look up CoreMIDI manufacturer/model for an endpoint display name. */
int midiEndpointLookupMeta (const char* endpointName,
                            char* manufacturerOut, size_t manufacturerSize,
                            char* modelOut, size_t modelSize);

#ifdef __cplusplus
}
#endif
