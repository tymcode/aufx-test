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

/** -∞dB mute label (UTF-8 infinity — never pass through juce::String(const char*) alone). */
inline juce::String utf8InfinityDb()
{
    return utf8 ("-\xE2\x88\x9E" "dB");
}

/** Send slider readout: -∞dB when muted, otherwise one decimal with + on boost. */
inline juce::String formatSendLevelDb (double db, double muteDb = -120.0)
{
    if (db <= muteDb + 0.05)
        return utf8InfinityDb();

    juce::String text;

    if (db > 0.0)
        text = "+";

    text += juce::String (db, 1);
    text += "dB";
    return text;
}

/** Mix slider readout. */
inline juce::String formatMixPercent (int percent)
{
    return juce::String (percent) + "%";
}
