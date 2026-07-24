#pragma once

#include <JuceHeader.h>

/**
 * Build a juce::String from a UTF-8 narrow literal.
 *
 * juce::String (const char*) only accepts ASCII (bytes <= 127). Passing a UTF-8
 * multi-byte sequence (em dash, ellipsis, arrows, copyright, etc.) through that
 * constructor produces mojibake. Wrap those literals with utf8(...).
 *
 * Source files should be saved as UTF-8. Prefer readable characters in the
 * literal; escape sequences (\xE2\x80\x94) also work if needed.
 */
inline juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}
