// The Ensoniq DP/Pro's System/MIDI parameter space and MIDI control map.
//
// Sourced from docs/EnsoniqDP-ProReferenceManual.pdf (Feb 1997). The byte-
// level SysEx protocol was a separate mail-order document that never became
// public, so this table captures everything the reference manual *does*
// document in a form ready for a future preset / system editor:
//
//   - every System/MIDI parameter (number, range, default, choices)
//   - MIDI Bank Select / Program Change layout for effect selection
//   - assignable bypass / tweak-knob controller conventions
//   - SysEx dump-type enum (parameter 19)
//
// Shape mirrors qv::Param in Quadraverse's QvParameters.h where it fits:
// id / name / unit / min / max / default / choices. The address is a
// System/MIDI parameter number rather than a (function, page) pair, since
// that is how the DP/Pro front panel and its (undocumented) SysEx address
// them.
#pragma once

#include <cstdint>
#include <cstring>

namespace dppro {

// ---------------------------------------------------------------------------
// MIDI identity / control map (reference manual Ch. 2 + MIDI Implementation)
// ---------------------------------------------------------------------------

inline constexpr uint8_t kManufacturerId = 0x0f; // Ensoniq

// Bank Select (CC 0 = MSB always 0; CC 32 = LSB = bank 0..3) on the system
// MIDI channel, followed by Program Change 0..127 within that bank.
inline constexpr uint8_t kBankSelectMsbCc = 0;
inline constexpr uint8_t kBankSelectLsbCc = 32;
inline constexpr uint8_t kBankSelectMsbValue = 0;

inline constexpr uint8_t kNumEffectBanks = 4;
inline constexpr uint8_t kProgramsPerBank = 128;
inline constexpr uint8_t kRamBankCount = 2;   // banks 0, 1
inline constexpr uint8_t kRomBankFirst = 2;   // banks 2, 3

// Default MIDI channels (System/MIDI parameters 7/8/9).
inline constexpr int kDefaultSystemChannel = 1;
inline constexpr int kDefaultEspAChannel = 2;
inline constexpr int kDefaultEspBChannel = 3;

// Module convention for the assignable bypass controllers (parameters 62/63).
// One-time device setup: Byp A Cntlr=#80, Byp B Cntlr=#81, Bypass Btn=Bypass.
// A controller value >= 64 engages bypass (manual p.16).
inline constexpr int kBypassEspAController = 80;
inline constexpr int kBypassEspBController = 81;
inline constexpr int kBypassOnValue = 127;
inline constexpr int kBypassOffValue = 0;

// Default tweak-knob controllers (parameters 60/61).
inline constexpr int kDefaultTweak1Controller = 12; // FXCtrl1
inline constexpr int kDefaultTweak2Controller = 13; // FXCtrl2

// Effect display numbers are bank*1000 + program (e.g. 2000 = bank 2, prog 0).
inline constexpr int effectDisplayNumber (int bank, int program)
{
    return bank * 1000 + program;
}

inline constexpr int effectBank (int displayNumber) { return displayNumber / 1000; }
inline constexpr int effectProgram (int displayNumber) { return displayNumber % 1000; }

// ---------------------------------------------------------------------------
// Parameter table
// ---------------------------------------------------------------------------

struct Param {
    const char* id;
    const char* name;          // as the unit's display spells it
    const char* unit;          // "" when the value is a bare number or a choice
    uint8_t     number;        // System/MIDI parameter number (1..77)
    int16_t     min;
    int16_t     max;
    int16_t     defaultValue;
    // Set when the display shows words rather than a number; choices[0] is
    // the wording at min, not at 0 (same convention as qv::Param).
    const char* const* choices;
    uint8_t     choiceCount;
};

// Controllers that can be Off (-1) or a MIDI CC number (0..119).
inline constexpr int16_t kControllerOff = -1;

// ---------------------------------------------------------------------------
// Choice lists (display wording)
// ---------------------------------------------------------------------------

inline constexpr const char* kYesNo[] = { "No", "Yes" };
inline constexpr const char* kOffOn[] = { "Off", "On" };

inline constexpr const char* kInputSource[] = {
    "Analog", "Digital, AES", "Digital, S/PDIF", "Ana+Dig, AES", "Ana+Dig, S/PDIF"
};

inline constexpr const char* kClockRate[] = { "44.100 kHz", "48.000 kHz" };

inline constexpr const char* kPgmChangeMode[] = { "Direct", "Use Map" };

inline constexpr const char* kDumpType[] = {
    "Selected Effect",
    "Bank 0 RAM Effects",
    "Bank 1 RAM Effects",
    "All RAM Effects",
    "All System Params"
};

enum class DumpType : uint8_t {
    selectedEffect = 0,
    bank0RamEffects = 1,
    bank1RamEffects = 2,
    allRamEffects = 3,
    allSystemParams = 4
};

inline constexpr const char* kTempoSource[] = { "Internal", "MIDI Clock" };

inline constexpr const char* kTappedNote[] = {
    "4 Beats", "3 Beats", "1/2 Note", "1/2 Triplet",
    "Dotted 1/4", "1/4 Note", "1/4 Triplet", "Dotted 1/8",
    "1/8 Note", "1/8 Triplet", "Dotted 1/16", "1/16 Note",
    "1/16 Triplet", "Dotted 1/32", "1/32 Note", "1/32 Triplet"
};

inline constexpr const char* kFootSwitchJob[] = {
    "Off", "DP Cntlr", "Tap Tempo",
    "Inc Effect", "Dec Effect", "Inc Song", "Dec Song",
    "Inc Step", "Dec Step", "Bypass A", "Bypass B"
};

// Named DP Cntlr sources. MIDI CC 0..119 are also selectable on the unit
// (shown as ModWheel#1, FXCtrl1 #12, etc.); they sit above this list in
// value space and are represented as 1000 + cc for editor use.
inline constexpr const char* kDpCntlrSource[] = {
    "Off",
    "TwkKnob1", "TwkKnob2",
    "LFO 1", "LFO 2",
    "L FootSw", "L FtSwToggle", "R FootSw", "R FtSwToggle",
    "NoteNumber", "Velocity", "Aftertouch", "PitchBend"
};

enum class DpCntlrSource : int16_t {
    off = 0,
    tweakKnob1 = 1,
    tweakKnob2 = 2,
    lfo1 = 3,
    lfo2 = 4,
    leftFootSw = 5,
    leftFootSwToggle = 6,
    rightFootSw = 7,
    rightFootSwToggle = 8,
    noteNumber = 9,
    velocity = 10,
    aftertouch = 11,
    pitchBend = 12,
    // midiController = 1000 + cc  (0..119)
};

inline constexpr int16_t midiControllerSource (int cc) { return (int16_t) (1000 + cc); }
inline constexpr bool isMidiControllerSource (int16_t v) { return v >= 1000 && v < 1120; }
inline constexpr int midiControllerNumber (int16_t v) { return (int) (v - 1000); }

inline constexpr const char* kBypassMode[] = {
    "Bypass",   // dry audio continues through converters/DSP (needed for latency cal)
    "Inp Mute", // input muted; trails ring out
    "Out Mute", // output muted; input still feeds the ESP
    "All Mute"  // input and output muted
};

enum class BypassMode : uint8_t {
    bypass = 0,
    inputMute = 1,
    outputMute = 2,
    allMute = 3
};

inline constexpr const char* kWakeUp[] = { "Restart", "Restore" };

inline constexpr const char* kAutoLoad[] = {
    "Off", "Algos", "Effects", "Algos & Effects"
};

inline constexpr const char* kMonoInputSrc[] = { "Off", "L-Input", "R-Input" };

// ---------------------------------------------------------------------------
// System/MIDI parameters 1..77
//
// Parameters 3..5 require the DI-Pro digital I/O board and are documented
// only in the DI-Pro User's Guide — omitted here.
// Parameters 29..39 are the ten characters of a Song name (same range each).
// Parameters 42..57 are the sixteen DP Cntlr slots (same choice list each).
// ---------------------------------------------------------------------------

inline constexpr Param kSystemMidiParams[] = {
    // Audio I/O
    { "input",            "Input",            "",  1, 0, 4, 0, kInputSource, 5 },
    { "clock",            "Clock",            "",  2, 0, 1, 1, kClockRate, 2 },

    // MIDI identity / channels
    { "midi_device_id",   "MIDI Device ID",   "",  6, 0, 127, 0, nullptr, 0 },
    { "system_midi_ch",   "System MIDI Channel", "", 7, 1, 16, kDefaultSystemChannel, nullptr, 0 },
    { "espa_midi_ch",     "ESP-A MIDI Channel", "", 8, 1, 16, kDefaultEspAChannel, nullptr, 0 },
    { "espb_midi_ch",     "ESP-B MIDI Channel", "", 9, 1, 16, kDefaultEspBChannel, nullptr, 0 },

    // MIDI enable / transmit
    { "accept_midi",      "Accept MIDI Msgs", "", 10, 0, 1, 1, kYesNo, 2 },
    { "accept_sysex",     "Accept SysEx Msgs", "", 11, 0, 1, 1, kYesNo, 2 },
    { "accept_cntlr",     "Accept Cntlr Msgs", "", 12, 0, 1, 1, kYesNo, 2 },
    { "accept_pgmch",     "Accept PgmCh Msgs", "", 13, 0, 1, 1, kYesNo, 2 },
    { "transmit_cntlr",   "Transmit Cntlr Msgs", "", 14, 0, 1, 0, kYesNo, 2 },
    { "transmit_pgmch",   "Transmit PrgCh Msgs", "", 15, 0, 1, 0, kYesNo, 2 },

    // Program-change map
    { "midi_pgm_chngs",   "MIDI Pgm Chngs",   "", 16, 0, 1, 0, kPgmChangeMode, 2 },
    { "map",              "Map",              "", 17, 0, 127, 0, nullptr, 0 },
    // Effect locations: None (-1) or bank*1000+program (0000..3xxx).
    { "map_effect",       "(Map) Effect",     "", 18, -1, 3127, -1, nullptr, 0 },

    // SysEx dump (front-panel; see DumpType)
    { "sysex_dump_type",  "ENTER to Send SysEx Dump Type", "", 19,
      0, 4, (int16_t) DumpType::bank0RamEffects, kDumpType, 5 },

    // Tempo
    { "tempo_source",     "Tempo Source",     "", 20, 0, 1, 0, kTempoSource, 2 },
    { "system_tempo",     "System Tempo",     "BPM", 21, 40, 400, 120, nullptr, 0 },
    { "tapped_note",      "Tapped Note",      "", 22, 0, 15, 5, kTappedNote, 16 }, // default 1/4 Note
    { "tap_average",      "Tap Average",      "", 23, 2, 8, 2, nullptr, 0 },
    { "tap_led",          "TapLED",           "", 24, 0, 1, 1, kOffOn, 2 },

    // Effect Finder application groups
    { "finder_app_group", "Finder App Group", "", 25, 1, 10, 1, nullptr, 0 },
    { "finder_item",      "Item",             "", 26, 1, 10, 1, nullptr, 0 },
    { "app_group_effect", "(App Group) Effect", "", 27, -1, 3127, -1, nullptr, 0 },

    // Songs (10 songs × 10 steps). Name chars are parameters 29..39.
    { "song",             "Song",             "", 28, 1, 10, 1, nullptr, 0 },
    { "song_name_0",      "Name[0]",          "", 29, 32, 126, (int16_t) 'U', nullptr, 0 },
    { "song_name_1",      "Name[1]",          "", 30, 32, 126, (int16_t) 'n', nullptr, 0 },
    { "song_name_2",      "Name[2]",          "", 31, 32, 126, (int16_t) 'd', nullptr, 0 },
    { "song_name_3",      "Name[3]",          "", 32, 32, 126, (int16_t) 'e', nullptr, 0 },
    { "song_name_4",      "Name[4]",          "", 33, 32, 126, (int16_t) 'f', nullptr, 0 },
    { "song_name_5",      "Name[5]",          "", 34, 32, 126, (int16_t) 'i', nullptr, 0 },
    { "song_name_6",      "Name[6]",          "", 35, 32, 126, (int16_t) 'n', nullptr, 0 },
    { "song_name_7",      "Name[7]",          "", 36, 32, 126, (int16_t) 'e', nullptr, 0 },
    { "song_name_8",      "Name[8]",          "", 37, 32, 126, (int16_t) 'd', nullptr, 0 },
    { "song_name_9",      "Name[9]",          "", 38, 32, 126, (int16_t) ' ', nullptr, 0 },
    // Parameter 39 is the 11th name field slot in the manual's "29-39" range;
    // reserved here so the number space stays contiguous.
    { "song_name_10",     "Name[10]",         "", 39, 32, 126, (int16_t) ' ', nullptr, 0 },
    { "song_step",        "Step",             "", 40, 1, 10, 1, nullptr, 0 },
    { "song_effect",      "(Song) Effect",    "", 41, -1, 3127, -1, nullptr, 0 },

    // Real-time controller suite (DP Cntlr 1..16). Defaults from the manual.
    // Named sources occupy 0..12 (kDpCntlrSource); MIDI CC n is encoded as
    // midiControllerSource(n) = 1000+n so editors can round-trip ModWheel#1 etc.
    { "dp_cntlr_1",  "DP Cntlr 1",  "", 42, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::tweakKnob1, kDpCntlrSource, 13 },
    { "dp_cntlr_2",  "DP Cntlr 2",  "", 43, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::tweakKnob2, kDpCntlrSource, 13 },
    { "dp_cntlr_3",  "DP Cntlr 3",  "", 44, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::lfo1, kDpCntlrSource, 13 },
    { "dp_cntlr_4",  "DP Cntlr 4",  "", 45, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::lfo2, kDpCntlrSource, 13 },
    { "dp_cntlr_5",  "DP Cntlr 5",  "", 46, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::leftFootSw, kDpCntlrSource, 13 },
    { "dp_cntlr_6",  "DP Cntlr 6",  "", 47, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::leftFootSwToggle, kDpCntlrSource, 13 },
    { "dp_cntlr_7",  "DP Cntlr 7",  "", 48, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::rightFootSw, kDpCntlrSource, 13 },
    { "dp_cntlr_8",  "DP Cntlr 8",  "", 49, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::rightFootSwToggle, kDpCntlrSource, 13 },
    { "dp_cntlr_9",  "DP Cntlr 9",  "", 50, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::noteNumber, kDpCntlrSource, 13 },
    // Default ModWheel#1 → MIDI CC 1
    { "dp_cntlr_10", "DP Cntlr 10", "", 51, 0, midiControllerSource (119),
      midiControllerSource (1), kDpCntlrSource, 13 },
    { "dp_cntlr_11", "DP Cntlr 11", "", 52, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::pitchBend, kDpCntlrSource, 13 },
    { "dp_cntlr_12", "DP Cntlr 12", "", 53, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::velocity, kDpCntlrSource, 13 },
    { "dp_cntlr_13", "DP Cntlr 13", "", 54, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::off, kDpCntlrSource, 13 },
    { "dp_cntlr_14", "DP Cntlr 14", "", 55, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::off, kDpCntlrSource, 13 },
    { "dp_cntlr_15", "DP Cntlr 15", "", 56, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::off, kDpCntlrSource, 13 },
    { "dp_cntlr_16", "DP Cntlr 16", "", 57, 0, midiControllerSource (119),
      (int16_t) DpCntlrSource::off, kDpCntlrSource, 13 },

    // Foot switches
    { "l_ftsw_cntlr", "L Ftsw Cntlr", "", 58, 0, 10, 1, kFootSwitchJob, 11 }, // default DP Cntlr
    { "r_ftsw_cntlr", "R Ftsw Cntlr", "", 59, 0, 10, 1, kFootSwitchJob, 11 },

    // Tweak / bypass MIDI controllers (Off = -1, else CC number 0..119)
    { "twk_1_cntlr", "Twk 1 Cntlr", "", 60, kControllerOff, 119, kDefaultTweak1Controller, nullptr, 0 },
    { "twk_2_cntlr", "Twk 2 Cntlr", "", 61, kControllerOff, 119, kDefaultTweak2Controller, nullptr, 0 },
    { "byp_a_cntlr", "Byp A Cntlr", "", 62, kControllerOff, 119, kControllerOff, nullptr, 0 },
    { "byp_b_cntlr", "Byp B Cntlr", "", 63, kControllerOff, 119, kControllerOff, nullptr, 0 },

    // Bypass behaviour
    { "algo_a_bypass_btn", "AlgoA Bypass Btn", "", 64, 0, 3, (int16_t) BypassMode::bypass, kBypassMode, 4 },
    { "algo_b_bypass_btn", "AlgoB Bypass Btn", "", 65, 0, 3, (int16_t) BypassMode::bypass, kBypassMode, 4 },

    // Status / protection
    { "display_bypass_state", "Display Bypass State", "", 66, 0, 1, 0, kOffOn, 2 },
    { "show_compare_message", "Show Compare Message", "", 67, 0, 1, 0, kOffOn, 2 },
    { "memory_protect",       "Memory Protect",       "", 68, 0, 1, 0, kOffOn, 2 },
    { "system_wake_up",       "System Wake Up",       "", 69, 0, 1, 0, kWakeUp, 2 },
    { "autoload",             "AutoLoad",             "", 70, 0, 3, 1, kAutoLoad, 4 }, // default Algos

    // Metering / input / mix
    // Meter Range: 3..48 dB in 3 dB steps (store the dB value).
    { "meter_range",     "Meter Range",     "dB", 71, 3, 48, 18, nullptr, 0 },
    { "mono_input_src",  "Mono Input Src",  "",   72, 0, 2, 0, kMonoInputSrc, 3 },
    // Global Wet Mix: Full Dry (0) .. 100% Wet (100).
    { "global_wet_mix",  "Global Wet Mix",  "%",  73, 0, 100, 100, nullptr, 0 },

    // EQ editor limits
    { "set_min_freq", "Set Min Freq", "Hz", 74, 1, 20, 1, nullptr, 0 },
    // Stored in 0.1 dB units so +48.0 dB = 480.
    { "set_max_gain", "Set Max Gain", "0.1dB", 75, 0, 480, 480, nullptr, 0 },

    { "show_undef_effects", "Show Undef Effects", "", 76, 0, 1, 1, kYesNo, 2 },
    // Parameter 77 is read-only (O.S. Version); kept so the number space is complete.
    { "os_version", "O.S. Version", "", 77, 0, 0, 0, nullptr, 0 },
};

inline constexpr int kSystemMidiParamCount = (int) (sizeof (kSystemMidiParams) / sizeof (kSystemMidiParams[0]));

inline const Param* findParamById (const char* id)
{
    if (id == nullptr)
        return nullptr;
    for (int i = 0; i < kSystemMidiParamCount; ++i)
        if (kSystemMidiParams[i].id != nullptr
            && std::strcmp (kSystemMidiParams[i].id, id) == 0)
            return &kSystemMidiParams[i];
    return nullptr;
}

inline const Param* findParamByNumber (uint8_t number)
{
    for (int i = 0; i < kSystemMidiParamCount; ++i)
        if (kSystemMidiParams[i].number == number)
            return &kSystemMidiParams[i];
    return nullptr;
}

} // namespace dppro
