// The QuadraVerb's parameter space.
//
// A parameter here is a (function, page) pair -- the same address the front
// panel and the SysEx parameter edit use.  What this table is for is knowing
// which pages exist right now: the meaning of a page depends on the
// configuration and on the block's mode, and an edit to a page the current
// configuration does not have is silently dropped.
//
// Ranges are the unit's own clamps, measured on the hardware rather than
// taken from the published table -- the two disagree in eight places.
#pragma once

#include <cstdint>

namespace qv {

enum class Block : uint8_t { Reverb = 1, Delay = 2, Pitch = 3, Eq = 4, Mix = 8, Mod = 9 };

struct Param {
    const char* id;
    const char* name;      // as the unit's own display spells it
    const char* unit;      // "" when the value is a bare number or a choice
    uint8_t     function;
    uint8_t     page;
    int16_t     min;
    int16_t     max;
    int16_t     defaultValue;
    // Set when the display shows words rather than a number; choices[0] is
    // the wording at min, not at 0.
    const char* const* choices;
    uint8_t     choiceCount;
};

// A block whose page 0 selects a mode has one Mode per value of page 0; the
// pages behind it change with it.  A block whose page 0 is an ordinary
// parameter has a single Mode holding every page including page 0.
struct Mode {
    const char*  name;     // the display's own wording, "" when unnamed
    uint8_t      value;    // what page 0 must be set to
    const Param* params;
    uint8_t      count;
};

struct BlockDef {
    Block       block;
    const char* name;
    uint8_t     function;
    bool        page0SelectsMode;
    int16_t     modeMin;   // -1 when page 0 is not a mode selector
    int16_t     modeMax;
    int16_t     modeDefault;
    const Mode* modes;
    uint8_t     modeCount;
};

struct ConfigDef {
    uint8_t         value;      // what (7, 0) must be set to
    const char*     name;       // the routing, in words
    const BlockDef* blocks;
    uint8_t         blockCount;
};

// Function 7 page 0.  Measured range 0..7; the published table says 0-4 and
// then lists all eight.
inline constexpr uint8_t kConfigFunction = 7;
inline constexpr uint8_t kConfigPage     = 0;
inline constexpr uint8_t kNumConfigs     = 8;

// Identical parameter lists are shared rather than repeated -- plate, room and
// chamber have the same pages as each other, and so do most configurations.
namespace detail {

inline constexpr const char* kReverbPlate1C0P1[] = { "PRE-EQ", "POST-EQ", "PITCH OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbPlate1C0P2[] = { "PITCH", "DELAY" };
inline constexpr const char* kReverbPlate1C0P11[] = { "OFF", "ON" };
// Shared by: config 0 PLATE 1; config 0 ROOM 1; config 0 CHAMBER 1.
inline constexpr Param kReverbPlate1C0[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 0, 3, 3, kReverbPlate1C0P1, 4 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbPlate1C0P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 39, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 74, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 179, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 56, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 7, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 50, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 11, 0, 1, 0, kReverbPlate1C0P11, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 12, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 13, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 14, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbHall1C0P1[] = { "PRE-EQ", "POST-EQ", "PITCH OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbHall1C0P2[] = { "PITCH", "DELAY" };
inline constexpr const char* kReverbHall1C0P10[] = { "OFF", "ON" };
inline constexpr Param kReverbHall1C0[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 0, 3, 3, kReverbHall1C0P1, 4 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbHall1C0P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 39, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 74, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 179, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 56, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 8, 0, 60, 50, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 9, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 10, 0, 1, 0, kReverbHall1C0P10, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 11, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 12, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 13, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbReverse1C0P1[] = { "PRE-EQ", "POST-EQ", "PITCH OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbReverse1C0P2[] = { "PITCH", "DELAY" };
inline constexpr Param kReverbReverse1C0[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 0, 3, 3, kReverbReverse1C0P1, 4 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbReverse1C0P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 39, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 74, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 179, nullptr, 0 },
    { "reverb_reverse_time", "Reverb Reverse Time", "", 1, 6, 0, 99, 56, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 7, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 50, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 40, nullptr, 0 },
};

inline constexpr const char* kDelayMonoDelayC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayMonoDelayC0[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayMonoDelayC0P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 3, 1, 800, 229, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 4, 0, 99, 26, nullptr, 0 },
};

inline constexpr const char* kDelayStereoDelayC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayStereoDelayC0[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayStereoDelayC0P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_left_delay_time", "Left Delay Time", "ms", 2, 3, 1, 400, 229, nullptr, 0 },
    { "delay_feedback_left", "Delay Feedback Left", "%", 2, 4, 0, 99, 26, nullptr, 0 },
    { "delay_right_delay_time", "Right Delay Time", "ms", 2, 5, 1, 400, 396, nullptr, 0 },
    { "delay_feedback_right", "Delay Feedback Right", "%", 2, 6, 0, 99, 36, nullptr, 0 },
};

inline constexpr const char* kDelayPingPongDelayC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayPingPongDelayC0[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayPingPongDelayC0P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 3, 1, 400, 229, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 4, 0, 99, 26, nullptr, 0 },
};

inline constexpr const char* kPitchMonoChorusC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr const char* kPitchMonoChorusC0P2[] = { "TRIANGLE", "SQUARE" };
// Shared by: config 0 MONO CHORUS; config 0 STEREO CHORUS.
inline constexpr Param kPitchMonoChorusC0[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchMonoChorusC0P1, 2 },
    { "pitch_lfo_waveshape", "LFO Waveshape", "", 3, 2, 0, 1, 1, kPitchMonoChorusC0P2, 2 },
    { "pitch_lfo_speed", "LFO Speed", "", 3, 3, 0, 98, 0, nullptr, 0 },
    { "pitch_lfo_depth", "LFO Depth", "", 3, 4, 0, 98, 62, nullptr, 0 },
    { "pitch_feedback", "Pitch Feedback", "%", 3, 5, 0, 99, 37, nullptr, 0 },
};

inline constexpr const char* kPitchMonoFlangeC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr const char* kPitchMonoFlangeC0P5[] = { "OFF", "ON" };
// Shared by: config 0 MONO FLANGE; config 0 STEREO FLANGE.
inline constexpr Param kPitchMonoFlangeC0[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchMonoFlangeC0P1, 2 },
    { "pitch_lfo_speed", "LFO Speed", "", 3, 2, 0, 98, 0, nullptr, 0 },
    { "pitch_lfo_depth", "LFO Depth", "", 3, 3, 0, 98, 62, nullptr, 0 },
    { "pitch_feedback", "Pitch Feedback", "%", 3, 4, 0, 99, 37, nullptr, 0 },
    { "pitch_trigger_flange", "Trigger Flange", "", 3, 5, 0, 1, 0, kPitchMonoFlangeC0P5, 2 },
};

inline constexpr const char* kPitchPitchDetuneC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kPitchPitchDetuneC0[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchPitchDetuneC0P1, 2 },
    { "pitch_detune_amount", "Detune Amount", "", 3, 2, 0, 198, 104, nullptr, 0 },
};

inline constexpr const char* kPitchPhaseShifterC0P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kPitchPhaseShifterC0[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchPhaseShifterC0P1, 2 },
    { "pitch_phaser_speed", "Phaser Speed", "", 3, 2, 0, 98, 0, nullptr, 0 },
    { "pitch_phaser_depth", "Phaser Depth", "", 3, 3, 0, 98, 62, nullptr, 0 },
};

inline constexpr Param kEq020HzC0[] = {
    { "eq_low_eq_frequency", "Low EQ Frequency", "Hz", 4, 0, 20, 999, 100, nullptr, 0 },
    { "eq_low_eq_amplitude", "Low EQ Amplitude", "dB", 4, 1, 0, 560, 340, nullptr, 0 },
    { "eq_mid_eq_frequency", "Mid EQ Frequency", "Hz", 4, 2, 200, 9999, 2000, nullptr, 0 },
    { "eq_mid_eq_bandwidth", "Mid EQ Bandwidth", "octaves", 4, 3, 20, 255, 100, nullptr, 0 },
    { "eq_mid_eq_amplitude", "Mid EQ Amplitude", "dB", 4, 4, 0, 560, 280, nullptr, 0 },
    { "eq_high_eq_frequency", "High EQ Frequency", "Hz", 4, 5, 2000, 18000, 10000, nullptr, 0 },
    { "eq_high_eq_amplitude", "High EQ Amplitude", "dB", 4, 6, 0, 560, 361, nullptr, 0 },
};

inline constexpr Param kMixPreEqC0[] = {
    { "mix_direct_signal_level", "Direct Signal Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_master_effects_level", "Master Effects Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 68, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 21, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 5, 0, 99, 23, nullptr, 0 },
};

inline constexpr Param kMixPostEqC0[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 68, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 21, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 5, 0, 99, 23, nullptr, 0 },
};

inline constexpr Param kMixPostEqPanningC0[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 68, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 21, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 5, 0, 99, 23, nullptr, 0 },
    { "mix_eq_panning_depth", "EQ Panning Depth", "", 8, 6, 0, 99, 99, nullptr, 0 },
    { "mix_eq_panning_speed", "EQ Panning Speed", "", 8, 7, 0, 98, 29, nullptr, 0 },
};

inline constexpr Param kMixPostEqTremoloC0[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 68, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 21, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 5, 0, 99, 23, nullptr, 0 },
    { "mix_eq_tremolo_depth", "EQ Tremolo Depth", "", 8, 6, 0, 99, 99, nullptr, 0 },
    { "mix_eq_tremolo_speed", "EQ Tremolo Speed", "", 8, 7, 0, 98, 29, nullptr, 0 },
};

inline constexpr const char* kReverbPlate1C1P1[] = { "PRE-LEZLIE", "LEZLIE OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbPlate1C1P2[] = { "LEZLIE OUTPUT", "DELAY OUTPUT" };
inline constexpr const char* kReverbPlate1C1P11[] = { "OFF", "ON" };
// Shared by: config 1 PLATE 1; config 1 ROOM 1; config 1 CHAMBER 1.
inline constexpr Param kReverbPlate1C1[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 2, kReverbPlate1C1P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 1, kReverbPlate1C1P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 57, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 87, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 0, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 41, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 8, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 8, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 43, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 31, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 11, 0, 1, 0, kReverbPlate1C1P11, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 12, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 13, 0, 99, 0, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 14, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbHall1C1P1[] = { "PRE-LEZLIE", "LEZLIE OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbHall1C1P2[] = { "LEZLIE OUTPUT", "DELAY OUTPUT" };
inline constexpr const char* kReverbHall1C1P10[] = { "OFF", "ON" };
inline constexpr Param kReverbHall1C1[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 2, kReverbHall1C1P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 1, kReverbHall1C1P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 57, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 87, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 0, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 41, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 8, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 8, 0, 60, 43, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 9, 0, 60, 31, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 10, 0, 1, 0, kReverbHall1C1P10, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 11, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 12, 0, 99, 0, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 13, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbReverse1C1P1[] = { "PRE-LEZLIE", "LEZLIE OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbReverse1C1P2[] = { "LEZLIE OUTPUT", "DELAY OUTPUT" };
inline constexpr Param kReverbReverse1C1[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 2, kReverbReverse1C1P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 1, kReverbReverse1C1P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 57, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 87, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 0, nullptr, 0 },
    { "reverb_reverse_time", "Reverb Reverse Time", "", 1, 6, 0, 99, 41, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 8, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 8, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 43, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 31, nullptr, 0 },
};

inline constexpr Param kDelayMonoDelayC1[] = {
    { "delay_input_mix_input_to_lezlie", "Delay Input Mix (input to Lezlie)", "", 2, 1, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 800, 183, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 0, nullptr, 0 },
};

inline constexpr Param kDelayStereoDelayC1[] = {
    { "delay_input_mix_input_to_lezlie", "Delay Input Mix (input to Lezlie)", "", 2, 1, 0, 198, 99, nullptr, 0 },
    { "delay_left_delay_time", "Left Delay Time", "ms", 2, 2, 1, 400, 183, nullptr, 0 },
    { "delay_feedback_left", "Delay Feedback Left", "%", 2, 3, 0, 99, 0, nullptr, 0 },
    { "delay_right_delay_time", "Right Delay Time", "ms", 2, 4, 1, 400, 210, nullptr, 0 },
    { "delay_feedback_right", "Delay Feedback Right", "%", 2, 5, 0, 99, 0, nullptr, 0 },
};

inline constexpr Param kDelayPingPongDelayC1[] = {
    { "delay_input_mix_input_to_lezlie", "Delay Input Mix (input to Lezlie)", "", 2, 1, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 400, 183, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kPitchSeparation00C1P1[] = { "OFF", "ON" };
inline constexpr const char* kPitchSeparation00C1P2[] = { "SLOW", "FAST" };
inline constexpr Param kPitchSeparation00C1[] = {
    { "pitch_lezlie_stereo_separation", "Lezlie Stereo Separation", "", 3, 0, 0, 99, 50, nullptr, 0 },
    { "pitch_lezlie_motor_control", "Lezlie Motor Control", "", 3, 1, 0, 1, 1, kPitchSeparation00C1P1, 2 },
    { "pitch_lezlie_speed", "Lezlie Speed", "", 3, 2, 0, 1, 0, kPitchSeparation00C1P2, 2 },
};

inline constexpr Param kEq20DbC1[] = {
    { "eq_high_rotor_level", "High Rotor Level", "dB", 4, 0, 0, 26, 23, nullptr, 0 },
};

inline constexpr Param kMixLevel00C1[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 0, 0, 99, 50, nullptr, 0 },
    { "mix_lezlie_output_level", "Lezlie Output Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 2, 0, 99, 0, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 3, 0, 99, 7, nullptr, 0 },
};

inline constexpr const char* kDelayMonoDelayC2P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayMonoDelayC2[] = {
    { "delay_input", "Delay Input", "", 2, 1, 0, 1, 0, kDelayMonoDelayC2P1, 2 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 1500, 750, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 61, nullptr, 0 },
};

inline constexpr const char* kDelayStereoDelayC2P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayStereoDelayC2[] = {
    { "delay_input", "Delay Input", "", 2, 1, 0, 1, 0, kDelayStereoDelayC2P1, 2 },
    { "delay_left_delay_time", "Left Delay Time", "ms", 2, 2, 1, 750, 750, nullptr, 0 },
    { "delay_feedback_left", "Delay Feedback Left", "%", 2, 3, 0, 99, 61, nullptr, 0 },
    { "delay_right_delay_time", "Right Delay Time", "ms", 2, 4, 1, 750, 750, nullptr, 0 },
    { "delay_feedback_right", "Delay Feedback Right", "%", 2, 5, 0, 99, 61, nullptr, 0 },
};

inline constexpr const char* kDelayPingPongDelayC2P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayPingPongDelayC2[] = {
    { "delay_input", "Delay Input", "", 2, 1, 0, 1, 0, kDelayPingPongDelayC2P1, 2 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 750, 750, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 61, nullptr, 0 },
};

inline constexpr Param kEq16HzC2[] = {
    { "eq_graphic_eq_16hz", "Graphic EQ 16Hz", "", 4, 0, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_32hz", "Graphic EQ 32Hz", "", 4, 1, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_62hz", "Graphic EQ 62Hz", "", 4, 2, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_126hz", "Graphic EQ 126Hz", "", 4, 3, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_250hz", "Graphic EQ 250Hz", "", 4, 4, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_500hz", "Graphic EQ 500Hz", "", 4, 5, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_1khz", "Graphic EQ 1KHz", "", 4, 6, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_2khz", "Graphic EQ 2KHz", "", 4, 7, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_4khz", "Graphic EQ 4KHz", "", 4, 8, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_8khz", "Graphic EQ 8KHz", "", 4, 9, 0, 28, 14, nullptr, 0 },
    { "eq_graphic_eq_16khz", "Graphic EQ 16KHz", "", 4, 10, 0, 28, 14, nullptr, 0 },
};

inline constexpr Param kMixLevel00C2[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 0, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 1, 0, 99, 53, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 2, 0, 99, 65, nullptr, 0 },
};

inline constexpr const char* kDelayMonoDelayC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayMonoDelayC3[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayMonoDelayC3P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 3, 1, 1500, 300, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 4, 0, 99, 40, nullptr, 0 },
};

inline constexpr const char* kDelayStereoDelayC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayStereoDelayC3[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayStereoDelayC3P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_left_delay_time", "Left Delay Time", "ms", 2, 3, 1, 750, 300, nullptr, 0 },
    { "delay_feedback_left", "Delay Feedback Left", "%", 2, 4, 0, 99, 40, nullptr, 0 },
    { "delay_right_delay_time", "Right Delay Time", "ms", 2, 5, 1, 750, 300, nullptr, 0 },
    { "delay_feedback_right", "Delay Feedback Right", "%", 2, 6, 0, 99, 40, nullptr, 0 },
};

inline constexpr const char* kDelayPingPongDelayC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayPingPongDelayC3[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayPingPongDelayC3P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 3, 1, 750, 300, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 4, 0, 99, 40, nullptr, 0 },
};

inline constexpr const char* kDelayMultiTapDelayC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kDelayMultiTapDelayC3[] = {
    { "delay_input_1", "Delay Input 1", "", 2, 1, 0, 1, 1, kDelayMultiTapDelayC3P1, 2 },
    { "delay_input_mix_input_1_to_pitch", "Delay Input Mix (input 1 to pitch)", "", 2, 2, 0, 198, 99, nullptr, 0 },
    { "delay_tap_number", "Tap Number", "", 2, 3, 0, 7, 0, nullptr, 0 },
    { "delay_tap_delay_time", "Tap Delay Time", "ms", 2, 4, 1, 1493, 1, nullptr, 0 },
    { "delay_tap_volume", "Tap Volume", "", 2, 5, 0, 99, 0, nullptr, 0 },
    { "delay_tap_panning", "Tap Panning", "", 2, 6, 0, 198, 0, nullptr, 0 },
    { "delay_tap_feedback", "Tap Feedback", "%", 2, 7, 0, 99, 0, nullptr, 0 },
    { "delay_master_feedback", "Master Feedback", "%", 2, 8, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kPitchMonoChorusC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr const char* kPitchMonoChorusC3P2[] = { "TRIANGLE", "SQUARE" };
// Shared by: config 3 MONO CHORUS; config 3 STEREO CHORUS.
inline constexpr Param kPitchMonoChorusC3[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchMonoChorusC3P1, 2 },
    { "pitch_lfo_waveshape", "LFO Waveshape", "", 3, 2, 0, 1, 0, kPitchMonoChorusC3P2, 2 },
    { "pitch_lfo_speed", "LFO Speed", "", 3, 3, 0, 98, 2, nullptr, 0 },
    { "pitch_lfo_depth", "LFO Depth", "", 3, 4, 0, 98, 90, nullptr, 0 },
    { "pitch_feedback", "Pitch Feedback", "%", 3, 5, 0, 99, 82, nullptr, 0 },
};

inline constexpr const char* kPitchMonoFlangeC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr const char* kPitchMonoFlangeC3P5[] = { "OFF", "ON" };
// Shared by: config 3 MONO FLANGE; config 3 STEREO FLANGE.
inline constexpr Param kPitchMonoFlangeC3[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchMonoFlangeC3P1, 2 },
    { "pitch_lfo_speed", "LFO Speed", "", 3, 2, 0, 98, 2, nullptr, 0 },
    { "pitch_lfo_depth", "LFO Depth", "", 3, 3, 0, 98, 90, nullptr, 0 },
    { "pitch_feedback", "Pitch Feedback", "%", 3, 4, 0, 99, 82, nullptr, 0 },
    { "pitch_trigger_flange", "Trigger Flange", "", 3, 5, 0, 1, 0, kPitchMonoFlangeC3P5, 2 },
};

inline constexpr const char* kPitchPitchDetuneC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kPitchPitchDetuneC3[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchPitchDetuneC3P1, 2 },
    { "pitch_detune_amount", "Detune Amount", "", 3, 2, 0, 198, 198, nullptr, 0 },
};

inline constexpr const char* kPitchPhaseShifterC3P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kPitchPhaseShifterC3[] = {
    { "pitch_input", "Pitch Input", "", 3, 1, 0, 1, 1, kPitchPhaseShifterC3P1, 2 },
    { "pitch_phaser_speed", "Phaser Speed", "", 3, 2, 0, 98, 2, nullptr, 0 },
    { "pitch_phaser_depth", "Phaser Depth", "", 3, 3, 0, 98, 90, nullptr, 0 },
};

inline constexpr Param kEq020HzC3[] = {
    { "eq_low_eq_frequency", "Low EQ Frequency", "Hz", 4, 0, 20, 999, 200, nullptr, 0 },
    { "eq_low_eq_amplitude", "Low EQ Amplitude", "dB", 4, 1, 0, 560, 280, nullptr, 0 },
    { "eq_low_mid_eq_freq", "Low Mid EQ Freq", "Hz", 4, 2, 20, 500, 100, nullptr, 0 },
    { "eq_low_mid_eq_bandwidth", "Low Mid EQ Bandwidth", "octaves", 4, 3, 20, 255, 100, nullptr, 0 },
    { "eq_low_mid_eq_amp", "Low Mid EQ Amp", "dB", 4, 4, 0, 560, 280, nullptr, 0 },
    { "eq_mid_eq_frequency", "Mid EQ Frequency", "Hz", 4, 5, 200, 9999, 2000, nullptr, 0 },
    { "eq_mid_eq_bandwidth", "Mid EQ Bandwidth", "octaves", 4, 6, 20, 255, 100, nullptr, 0 },
    { "eq_mid_eq_amplitude", "Mid EQ Amplitude", "dB", 4, 7, 0, 560, 280, nullptr, 0 },
    { "eq_high_mid_eq_freq", "High Mid EQ Freq", "Hz", 4, 8, 2000, 18000, 5000, nullptr, 0 },
    { "eq_high_mid_eq_bandwidth", "High Mid EQ Bandwidth", "octaves", 4, 9, 20, 255, 100, nullptr, 0 },
    { "eq_high_mid_eq_amp", "High Mid EQ Amp", "dB", 4, 10, 0, 560, 280, nullptr, 0 },
    { "eq_high_eq_frequency", "High EQ Frequency", "Hz", 4, 11, 2000, 18000, 8000, nullptr, 0 },
    { "eq_high_eq_amplitude", "High EQ Amplitude", "dB", 4, 12, 0, 560, 280, nullptr, 0 },
};

inline constexpr Param kMixPreEqC3[] = {
    { "mix_direct_signal_level", "Direct Signal Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_master_effects_level", "Master Effects Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 95, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 0, nullptr, 0 },
};

inline constexpr Param kMixPostEqC3[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 95, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 0, nullptr, 0 },
};

inline constexpr Param kMixPostEqPanningC3[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 95, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 0, nullptr, 0 },
    { "mix_eq_panning_depth", "EQ Panning Depth", "", 8, 5, 0, 99, 99, nullptr, 0 },
    { "mix_eq_panning_speed", "EQ Panning Speed", "", 8, 6, 0, 98, 29, nullptr, 0 },
};

inline constexpr Param kMixPostEqTremoloC3[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_pitch_output_level", "Pitch Output Level", "", 8, 3, 0, 99, 95, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 4, 0, 99, 0, nullptr, 0 },
    { "mix_eq_tremolo_depth", "EQ Tremolo Depth", "", 8, 5, 0, 99, 99, nullptr, 0 },
    { "mix_eq_tremolo_speed", "EQ Tremolo Speed", "", 8, 6, 0, 98, 29, nullptr, 0 },
};

inline constexpr const char* kReverbPlate2C4P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr const char* kReverbPlate2C4P9[] = { "OFF", "ON" };
// Shared by: config 4 PLATE 2; config 4 ROOM 2; config 4 CHAMBER 2.
inline constexpr Param kReverbPlate2C4[] = {
    { "reverb_input", "Reverb Input", "", 1, 1, 0, 1, 0, kReverbPlate2C4P1, 2 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 2, 1, 140, 20, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 3, 0, 198, 198, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 4, 0, 99, 50, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 5, 0, 8, 7, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 6, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 7, 0, 60, 54, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 8, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 9, 0, 1, 0, kReverbPlate2C4P9, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 10, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 11, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 12, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbHall2C4P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr const char* kReverbHall2C4P8[] = { "OFF", "ON" };
inline constexpr Param kReverbHall2C4[] = {
    { "reverb_input", "Reverb Input", "", 1, 1, 0, 1, 0, kReverbHall2C4P1, 2 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 2, 1, 140, 20, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 3, 0, 198, 198, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 4, 0, 99, 50, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 5, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 6, 0, 60, 54, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 7, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 8, 0, 1, 0, kReverbHall2C4P8, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 9, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 10, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 11, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbReverse2C4P1[] = { "PRE-EQ", "POST-EQ" };
inline constexpr Param kReverbReverse2C4[] = {
    { "reverb_input", "Reverb Input", "", 1, 1, 0, 1, 0, kReverbReverse2C4P1, 2 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 2, 1, 140, 20, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 3, 0, 198, 198, nullptr, 0 },
    { "reverb_reverse_time", "Reverb Reverse Time", "", 1, 4, 0, 99, 50, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 5, 0, 8, 7, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 6, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 7, 0, 60, 54, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 8, 0, 60, 40, nullptr, 0 },
};

inline constexpr const char* kPitchOnC4P1[] = { "TRIANGLE", "SQUARE" };
inline constexpr Param kPitchOnC4[] = {
    { "pitch_lfo_waveshape", "LFO Waveshape", "", 3, 1, 0, 1, 0, kPitchOnC4P1, 2 },
    { "pitch_lfo_speed", "LFO Speed", "", 3, 2, 0, 98, 9, nullptr, 0 },
    { "pitch_lfo_depth", "LFO Depth", "", 3, 3, 0, 98, 9, nullptr, 0 },
};

inline constexpr Param kEq020HzC4[] = {
    { "eq_low_eq_frequency", "Low EQ Frequency", "Hz", 4, 0, 20, 999, 200, nullptr, 0 },
    { "eq_low_eq_amplitude", "Low EQ Amplitude", "dB", 4, 1, 0, 560, 280, nullptr, 0 },
    { "eq_mid_eq_frequency", "Mid EQ Frequency", "Hz", 4, 2, 200, 9999, 2000, nullptr, 0 },
    { "eq_mid_eq_bandwidth", "Mid EQ Bandwidth", "octaves", 4, 3, 20, 255, 100, nullptr, 0 },
    { "eq_mid_eq_amplitude", "Mid EQ Amplitude", "dB", 4, 4, 0, 560, 280, nullptr, 0 },
    { "eq_high_eq_frequency", "High EQ Frequency", "Hz", 4, 5, 2000, 18000, 8000, nullptr, 0 },
    { "eq_high_eq_amplitude", "High EQ Amplitude", "dB", 4, 6, 0, 560, 280, nullptr, 0 },
};

inline constexpr Param kMixSelectPreEqC4[] = {
    { "mix_direct_signal_level", "Direct Signal Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_master_effects_level", "Master Effects Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 3, 0, 99, 99, nullptr, 0 },
};

inline constexpr Param kMixSelectPostEqC4[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_eq_output_level", "EQ Output Level", "", 8, 2, 0, 99, 50, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 3, 0, 99, 99, nullptr, 0 },
};

inline constexpr const char* kReverbPlate1C5P1[] = { "PRE-RING", "RING OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbPlate1C5P2[] = { "RING OUTPUT", "DELAY OUTPUT" };
inline constexpr const char* kReverbPlate1C5P11[] = { "OFF", "ON" };
// Shared by: config 5 PLATE 1; config 5 ROOM 1; config 5 CHAMBER 1.
inline constexpr Param kReverbPlate1C5[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 1, kReverbPlate1C5P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbPlate1C5P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 39, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 74, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 179, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 56, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 7, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 50, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 11, 0, 1, 0, kReverbPlate1C5P11, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 12, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 13, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 14, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbHall1C5P1[] = { "PRE-RING", "RING OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbHall1C5P2[] = { "RING OUTPUT", "DELAY OUTPUT" };
inline constexpr const char* kReverbHall1C5P10[] = { "OFF", "ON" };
inline constexpr Param kReverbHall1C5[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 1, kReverbHall1C5P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbHall1C5P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 39, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 74, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 179, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 56, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 8, 0, 60, 50, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 9, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 10, 0, 1, 0, kReverbHall1C5P10, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 11, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 12, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 13, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbReverse1C5P1[] = { "PRE-RING", "RING OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbReverse1C5P2[] = { "RING OUTPUT", "DELAY OUTPUT" };
inline constexpr Param kReverbReverse1C5[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 1, kReverbReverse1C5P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbReverse1C5P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 39, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 74, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 179, nullptr, 0 },
    { "reverb_reverse_time", "Reverb Reverse Time", "", 1, 6, 0, 99, 56, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 7, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 50, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 40, nullptr, 0 },
};

inline constexpr Param kDelayMonoDelayC5[] = {
    { "delay_input_mix_input_to_ring_mod", "Delay Input Mix (input to ring mod)", "", 2, 1, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 800, 229, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 26, nullptr, 0 },
};

inline constexpr Param kDelayStereoDelayC5[] = {
    { "delay_input_mix_input_to_ring_mod", "Delay Input Mix (input to ring mod)", "", 2, 1, 0, 198, 99, nullptr, 0 },
    { "delay_left_delay_time", "Left Delay Time", "ms", 2, 2, 1, 400, 229, nullptr, 0 },
    { "delay_feedback_left", "Delay Feedback Left", "%", 2, 3, 0, 99, 26, nullptr, 0 },
    { "delay_right_delay_time", "Right Delay Time", "ms", 2, 4, 1, 400, 320, nullptr, 0 },
    { "delay_feedback_right", "Delay Feedback Right", "%", 2, 5, 0, 99, 36, nullptr, 0 },
};

inline constexpr Param kDelayPingPongDelayC5[] = {
    { "delay_input_mix_input_to_ring_mod", "Delay Input Mix (input to ring mod)", "", 2, 1, 0, 198, 99, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 400, 229, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 26, nullptr, 0 },
};

inline constexpr Param kPitch001HzC5[] = {
    { "pitch_spectrum_shift", "Spectrum Shift", "Hz", 3, 0, 1, 300, 2, nullptr, 0 },
    { "pitch_ring_mod_output_mix_up", "Ring Mod Output Mix Up", "", 3, 1, 0, 198, 89, nullptr, 0 },
    { "pitch_del_reverb_input_mix_up", "Del/reverb Input Mix Up", "", 3, 2, 0, 198, 109, nullptr, 0 },
};

inline constexpr Param kMixLevel00C5[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 0, 0, 99, 50, nullptr, 0 },
    { "mix_direct_signal_level", "Direct Signal Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_ring_mod_output_level", "Ring Mod Output Level", "", 8, 2, 0, 99, 99, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 3, 0, 99, 4, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 4, 0, 99, 6, nullptr, 0 },
};

inline constexpr const char* kReverbPlate1C6P1[] = { "PRE-RESONATOR", "RESONATOR OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbPlate1C6P2[] = { "RESONATOR OUTPUT", "DELAY OUTPUT" };
inline constexpr const char* kReverbPlate1C6P11[] = { "OFF", "ON" };
// Shared by: config 6 PLATE 1; config 6 ROOM 1; config 6 CHAMBER 1.
inline constexpr Param kReverbPlate1C6[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 2, kReverbPlate1C6P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbPlate1C6P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 99, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 100, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 109, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 60, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 3, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 60, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 11, 0, 1, 0, kReverbPlate1C6P11, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 12, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 13, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 14, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbHall1C6P1[] = { "PRE-RESONATOR", "RESONATOR OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbHall1C6P2[] = { "RESONATOR OUTPUT", "DELAY OUTPUT" };
inline constexpr const char* kReverbHall1C6P10[] = { "OFF", "ON" };
inline constexpr Param kReverbHall1C6[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 2, kReverbHall1C6P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbHall1C6P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 99, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 100, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 109, nullptr, 0 },
    { "reverb_decay", "Reverb Decay", "", 1, 6, 0, 99, 60, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 3, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 8, 0, 60, 60, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 9, 0, 60, 40, nullptr, 0 },
    { "reverb_gate", "Reverb Gate", "", 1, 10, 0, 1, 0, kReverbHall1C6P10, 2 },
    { "reverb_gate_hold_time", "Reverb Gate Hold Time", "", 1, 11, 0, 99, 0, nullptr, 0 },
    { "reverb_gate_release_time", "Reverb Gate Release Time", "", 1, 12, 0, 99, 80, nullptr, 0 },
    { "reverb_gated_level", "Reverb Gated Level", "%", 1, 13, 0, 99, 0, nullptr, 0 },
};

inline constexpr const char* kReverbReverse1C6P1[] = { "PRE-RESONATOR", "RESONATOR OUTPUT", "DELAY MIX INPUT" };
inline constexpr const char* kReverbReverse1C6P2[] = { "RESONATOR OUTPUT", "DELAY OUTPUT" };
inline constexpr Param kReverbReverse1C6[] = {
    { "reverb_input_1", "Reverb Input 1", "", 1, 1, 1, 3, 2, kReverbReverse1C6P1, 3 },
    { "reverb_input_2", "Reverb Input 2", "", 1, 2, 0, 1, 0, kReverbReverse1C6P2, 2 },
    { "reverb_input_mix_1_to_2", "Reverb Input Mix (1 to 2)", "", 1, 3, 0, 198, 99, nullptr, 0 },
    { "reverb_predelay", "Reverb Predelay", "ms", 1, 4, 1, 140, 100, nullptr, 0 },
    { "reverb_predelay_mix_pre_to_post", "Predelay Mix (pre to post)", "", 1, 5, 0, 198, 109, nullptr, 0 },
    { "reverb_reverse_time", "Reverb Reverse Time", "", 1, 6, 0, 99, 60, nullptr, 0 },
    { "reverb_diffusion_amount", "Reverb Diffusion Amount", "", 1, 7, 0, 8, 3, nullptr, 0 },
    { "reverb_density", "Reverb Density", "", 1, 8, 0, 8, 7, nullptr, 0 },
    { "reverb_low_frequency_decay", "Low Frequency Decay", "", 1, 9, 0, 60, 60, nullptr, 0 },
    { "reverb_high_frequency_decay", "High Frequency Decay", "", 1, 10, 0, 60, 40, nullptr, 0 },
};

inline constexpr Param kDelayMonoDelayC6[] = {
    { "delay_input_mix_resonator", "Delay Input Mix Resonator", "", 2, 1, 0, 198, 198, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 720, 229, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 26, nullptr, 0 },
};

inline constexpr Param kDelayStereoDelayC6[] = {
    { "delay_input_mix_resonator", "Delay Input Mix Resonator", "", 2, 1, 0, 198, 198, nullptr, 0 },
    { "delay_left_delay_time", "Left Delay Time", "ms", 2, 2, 1, 320, 229, nullptr, 0 },
    { "delay_feedback_left", "Delay Feedback Left", "%", 2, 3, 0, 99, 26, nullptr, 0 },
    { "delay_right_delay_time", "Right Delay Time", "ms", 2, 4, 1, 320, 229, nullptr, 0 },
    { "delay_feedback_right", "Delay Feedback Right", "%", 2, 5, 0, 99, 26, nullptr, 0 },
};

inline constexpr Param kDelayPingPongDelayC6[] = {
    { "delay_input_mix_resonator", "Delay Input Mix Resonator", "", 2, 1, 0, 198, 198, nullptr, 0 },
    { "delay_time", "Delay Time", "ms", 2, 2, 1, 320, 229, nullptr, 0 },
    { "delay_feedback", "Delay Feedback", "%", 2, 3, 0, 99, 26, nullptr, 0 },
};

inline constexpr const char* kPitchModeContinuousC6P0[] = { "CONTINUOUS", "MIDI GATED" };
inline constexpr Param kPitchModeContinuousC6[] = {
    { "pitch_resonator_gate_mode", "Resonator Gate Mode", "", 3, 0, 0, 1, 0, kPitchModeContinuousC6P0, 2 },
    { "pitch_resonator_decay", "Resonator Decay", "", 3, 1, 0, 99, 80, nullptr, 0 },
    { "pitch_resonator_1_tune", "Resonator 1 Tune", "semitones", 3, 2, 0, 60, 24, nullptr, 0 },
    { "pitch_resonator_2_tune", "Resonator 2 Tune", "semitones", 3, 3, 0, 60, 24, nullptr, 0 },
    { "pitch_resonator_3_tune", "Resonator 3 Tune", "semitones", 3, 4, 0, 60, 24, nullptr, 0 },
    { "pitch_resonator_4_tune", "Resonator 4 Tune", "semitones", 3, 5, 0, 60, 24, nullptr, 0 },
    { "pitch_resonator_5_tune", "Resonator 5 Tune", "semitones", 3, 6, 0, 60, 24, nullptr, 0 },
};

inline constexpr Param kMixLevel00C6[] = {
    { "mix_master_effects_level", "Master Effects Level", "", 8, 0, 0, 99, 50, nullptr, 0 },
    { "mix_direct_signal_level", "Direct Signal Level", "", 8, 1, 0, 99, 50, nullptr, 0 },
    { "mix_resonator_output_level", "Resonator Output Level", "", 8, 2, 0, 99, 68, nullptr, 0 },
    { "mix_delay_output_level", "Delay Output Level", "", 8, 3, 0, 99, 0, nullptr, 0 },
    { "mix_reverb_output_level", "Reverb Output Level", "", 8, 4, 0, 99, 20, nullptr, 0 },
};

inline constexpr const char* kDelayLoopingC7P0[] = { "LOOPING", "ONE SHOT", "AUDIO TRIGGER" };
inline constexpr const char* kDelayLoopingC7P3[] = { "OFF", "ON" };
inline constexpr const char* kDelayLoopingC7P4[] = { "OFF", "GATED", "ONE SHOT" };
inline constexpr Param kDelayLoopingC7[] = {
    { "delay_sample_playback", "Sample Playback", "", 2, 0, 0, 2, 1, kDelayLoopingC7P0, 3 },
    { "delay_sample_start", "Sample Start", "s", 2, 1, 0, 150, 0, nullptr, 0 },
    { "delay_sample_length", "Sample Length", "s", 2, 2, 5, 155, 155, nullptr, 0 },
    { "delay_audio_trigger_sampling", "Audio Trigger Sampling", "", 2, 3, 0, 1, 1, kDelayLoopingC7P3, 2 },
    { "delay_midi_trigger", "MIDI Trigger", "", 2, 4, 0, 2, 0, kDelayLoopingC7P4, 3 },
    { "delay_midi_trigger_low_limit", "MIDI Trigger Low Limit", "", 2, 5, 0, 127, 0, nullptr, 0 },
    { "delay_midi_trigger_base", "MIDI Trigger Base", "", 2, 6, 0, 127, 60, nullptr, 0 },
    { "delay_midi_trigger_high_limit", "MIDI Trigger High Limit", "", 2, 7, 0, 127, 127, nullptr, 0 },
};

inline constexpr Param kMixLevel00C7[] = {
    { "mix_direct_signal_level", "Direct Signal Level", "", 8, 0, 0, 99, 50, nullptr, 0 },
    { "mix_sample_playback_level", "Sample Playback Level", "", 8, 1, 0, 99, 99, nullptr, 0 },
};

// The MOD section (function 9): the MIDI modulation matrix -- eight
// modulators, each with SOURCE / TARGET / AMPLITUDE.
// Pages 0-23 exist in configurations 0-6; configuration 7
// (SAMPLING) has none.  The SOURCE list is the same in every
// configuration.  The TARGET list is per-configuration, and the wording of
// some entries follows the current block modes (a stereo delay shows LEFT
// DELAY TIME where a mono one shows DELAY TIME, multi-tap shows TAP n
// DELAY TIME, a phaser shows PHASER SPEED where a chorus shows LFO
// SPEED...), which a static table cannot express: the lists below are the
// display's wording under the factory-default modes of each
// configuration's reference program (0, 8, 6, 5, 1, 88, 89).
// AMPLITUDE is stored 0..198 and displayed as -99..+99.

inline constexpr const char* kModSources[] = {
    "PITCH BEND", "AFTER TOUCH", "NOTE NUMBER", "NOTE VELOCITY",
    "CONTROLLER 000", "CONTROLLER 001", "CONTROLLER 002", "CONTROLLER 003",
    "CONTROLLER 004", "CONTROLLER 005", "CONTROLLER 006", "CONTROLLER 007",
    "CONTROLLER 008", "CONTROLLER 009", "CONTROLLER 010", "CONTROLLER 011",
    "CONTROLLER 012", "CONTROLLER 013", "CONTROLLER 014", "CONTROLLER 015",
    "CONTROLLER 016", "CONTROLLER 017", "CONTROLLER 018", "CONTROLLER 019",
    "CONTROLLER 020", "CONTROLLER 021", "CONTROLLER 022", "CONTROLLER 023",
    "CONTROLLER 024", "CONTROLLER 025", "CONTROLLER 026", "CONTROLLER 027",
    "CONTROLLER 028", "CONTROLLER 029", "CONTROLLER 030", "CONTROLLER 031",
    "CONTROLLER 032", "CONTROLLER 033", "CONTROLLER 034", "CONTROLLER 035",
    "CONTROLLER 036", "CONTROLLER 037", "CONTROLLER 038", "CONTROLLER 039",
    "CONTROLLER 040", "CONTROLLER 041", "CONTROLLER 042", "CONTROLLER 043",
    "CONTROLLER 044", "CONTROLLER 045", "CONTROLLER 046", "CONTROLLER 047",
    "CONTROLLER 048", "CONTROLLER 049", "CONTROLLER 050", "CONTROLLER 051",
    "CONTROLLER 052", "CONTROLLER 053", "CONTROLLER 054", "CONTROLLER 055",
    "CONTROLLER 056", "CONTROLLER 057", "CONTROLLER 058", "CONTROLLER 059",
    "CONTROLLER 060", "CONTROLLER 061", "CONTROLLER 062", "CONTROLLER 063",
    "CONTROLLER 064", "CONTROLLER 065", "CONTROLLER 066", "CONTROLLER 067",
    "CONTROLLER 068", "CONTROLLER 069", "CONTROLLER 070", "CONTROLLER 071",
    "CONTROLLER 072", "CONTROLLER 073", "CONTROLLER 074", "CONTROLLER 075",
    "CONTROLLER 076", "CONTROLLER 077", "CONTROLLER 078", "CONTROLLER 079",
    "CONTROLLER 080", "CONTROLLER 081", "CONTROLLER 082", "CONTROLLER 083",
    "CONTROLLER 084", "CONTROLLER 085", "CONTROLLER 086", "CONTROLLER 087",
    "CONTROLLER 088", "CONTROLLER 089", "CONTROLLER 090", "CONTROLLER 091",
    "CONTROLLER 092", "CONTROLLER 093", "CONTROLLER 094", "CONTROLLER 095",
    "CONTROLLER 096", "CONTROLLER 097", "CONTROLLER 098", "CONTROLLER 099",
    "CONTROLLER 100", "CONTROLLER 101", "CONTROLLER 102", "CONTROLLER 103",
    "CONTROLLER 104", "CONTROLLER 105", "CONTROLLER 106", "CONTROLLER 107",
    "CONTROLLER 108", "CONTROLLER 109", "CONTROLLER 110", "CONTROLLER 111",
    "CONTROLLER 112", "CONTROLLER 113", "CONTROLLER 114", "CONTROLLER 115",
    "CONTROLLER 116", "CONTROLLER 117", "CONTROLLER 118", "CONTROLLER 119",
    "CONTROLLER 120", "CONTROLLER 121",
};

// Target values 0..68; choices[0] is the wording at 0.
inline constexpr const char* kModTargetsC0[] = {
    "REVERB INPUT MIX", "REVERB PREDELAY", "REV PREDELAY MIX", "REVERB DECAY",
    "REVERB DIFFUSION", "REVERB LOW DECAY", "REVERB HI DECAY", "DELAY INPUT MIX",
    "DELAY INPUT MIX", "DELAY INPUT MIX", "DELAY INPUT MIX", "DELAY INPUT MIX",
    "DELAY INPUT MIX", "DELAY INPUT MIX", "DELAY INPUT MIX", "DELAY INPUT MIX",
    "DELAY INPUT MIX", "LEFT DELAY TIME", "L DELAY FEEDBACK", "RIGHT DELAY TIME",
    "R DELAY FEEDBACK", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO DEPTH", "PITCH FEEDBACK", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ AMPLITUDE", "MID EQ FREQUENCY", "MID EQ BANDWIDTH",
    "MID EQ AMPLITUDE", "HI EQ FREQUENCY", "HI EQ AMPLITUDE", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EQ MIX LEVEL", "PITCH MIX LEVEL", "DELAY MIX LEVEL",
    "REVERB MIX LEVEL",
};

// Target values 0..67; choices[0] is the wording at 0.
inline constexpr const char* kModTargetsC1[] = {
    "REVERB INPUT MIX", "REVERB PREDELAY", "REV PREDELAY MIX", "REVERB DECAY",
    "REVERB DIFFUSION", "REVERB LOW DECAY", "REVERB HI DECAY", "LEFT DELAY TIME",
    "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME",
    "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME",
    "LEFT DELAY TIME", "L DELAY FEEDBACK", "RIGHT DELAY TIME", "R DELAY FEEDBACK",
    "LEZLIE STEREO", "LEZLIE STEREO", "LEZLIE STEREO", "LEZLIE STEREO",
    "LEZLIE STEREO", "LEZLIE STEREO", "LEZLIE STEREO", "LEZLIE STEREO",
    "LEZLIE STEREO", "LEZLIE STEREO", "LEZLIE STEREO", "LEZLIE STEREO",
    "LEZLIE STEREO", "LEZLIE MOTOR", "LEZLIE SPEED", "LEZLIE HI LEVEL",
    "LEZLIE HI LEVEL", "LEZLIE HI LEVEL", "LEZLIE HI LEVEL", "LEZLIE HI LEVEL",
    "LEZLIE HI LEVEL", "LEZLIE HI LEVEL", "LEZLIE HI LEVEL", "LEZLIE HI LEVEL",
    "LEZLIE HI LEVEL", "LEZLIE HI LEVEL", "LEZLIE HI LEVEL", "LEZLIE HI LEVEL",
    "LEZLIE HI LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "LEZLIE MIX LEVEL", "DELAY MIX LEVEL", "REVERB MIX LEVEL",
};

// Target values 16..66; choices[0] is the wording at 16.
inline constexpr const char* kModTargetsC2[] = {
    "DELAY TIME", "DELAY FEEDBACK", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT", "16Hz BOOST/CUT",
    "16Hz BOOST/CUT", "32Hz BOOST/CUT", "62Hz BOOST/CUT", "126Hz BOOST/CUT",
    "250Hz BOOST/CUT", "500Hz BOOST/CUT", "1KHz BOOST/CUT", "2KHz BOOST/CUT",
    "4KHz BOOST/CUT", "8KHz BOOST/CUT", "16KHz BOOST/CUT", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EQ MIX LEVEL", "DELAY MIX LEVEL",
};

// Target values 16..67; choices[0] is the wording at 16.
inline constexpr const char* kModTargetsC3[] = {
    "DELAY INPUT MIX", "DELAY TIME", "DELAY FEEDBACK", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO DEPTH", "PITCH FEEDBACK", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ AMPLITUDE", "LOW MID EQ FREQ", "LOW MID EQ WIDTH",
    "LOW MID EQ AMP", "MID EQ FREQUENCY", "MID EQ BANDWIDTH", "MID EQ AMPLITUDE",
    "HIGH MID EQ FREQ", "HIGH MID EQ WDTH", "HIGH MID EQ AMP", "HI EQ FREQUENCY",
    "HI EQ AMPLITUDE", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL", "EFFECT MIX LEVEL",
    "EFFECT MIX LEVEL", "EQ MIX LEVEL", "PITCH MIX LEVEL", "DELAY MIX LEVEL",
};

// Target values 0..66; choices[0] is the wording at 0.
inline constexpr const char* kModTargetsC4[] = {
    "REVERB INPUT MIX", "REVERB PREDELAY", "REV PREDELAY MIX", "REVERB DECAY",
    "REVERB DIFFUSION", "REVERB LOW DECAY", "REVERB HI DECAY", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO SPEED", "LFO SPEED", "LFO SPEED",
    "LFO SPEED", "LFO DEPTH", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY", "LOW EQ FREQUENCY",
    "LOW EQ FREQUENCY", "LOW EQ AMPLITUDE", "MID EQ FREQUENCY", "MID EQ BANDWIDTH",
    "MID EQ AMPLITUDE", "HI EQ FREQUENCY", "HI EQ AMPLITUDE", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "EFFECT MIX LEVEL", "REVERB MIX LEVEL",
};

// Target values 0..68; choices[0] is the wording at 0.
inline constexpr const char* kModTargetsC5[] = {
    "REVERB INPUT MIX", "REVERB PREDELAY", "REV PREDELAY MIX", "REVERB DECAY",
    "REVERB DIFFUSION", "REVERB LOW DECAY", "REVERB HI DECAY", "LEFT DELAY TIME",
    "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME",
    "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME", "LEFT DELAY TIME",
    "LEFT DELAY TIME", "L DELAY FEEDBACK", "RIGHT DELAY TIME", "R DELAY FEEDBACK",
    "SPECTRUM SHIFT", "SPECTRUM SHIFT", "SPECTRUM SHIFT", "SPECTRUM SHIFT",
    "SPECTRUM SHIFT", "SPECTRUM SHIFT", "SPECTRUM SHIFT", "SPECTRUM SHIFT",
    "SPECTRUM SHIFT", "SPECTRUM SHIFT", "SPECTRUM SHIFT", "SPECTRUM SHIFT",
    "SPECTRUM SHIFT", "RING OUTPUT MIX", "DEL/REV IN MIX", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "EFFECT MIX LEVEL", "RING MIX LEVEL", "DELAY MIX LEVEL",
    "REVERB MIX LEVEL",
};

// Target values 0..68; choices[0] is the wording at 0.
inline constexpr const char* kModTargetsC6[] = {
    "REVERB INPUT MIX", "REVERB PREDELAY", "REV PREDELAY MIX", "REVERB DECAY",
    "REVERB DIFFUSION", "REVERB LOW DECAY", "REVERB HI DECAY", "DELAY TIME",
    "DELAY TIME", "DELAY TIME", "DELAY TIME", "DELAY TIME",
    "DELAY TIME", "DELAY TIME", "DELAY TIME", "DELAY TIME",
    "DELAY TIME", "DELAY FEEDBACK", "RESONATOR DECAY", "RESONATOR DECAY",
    "RESONATOR DECAY", "RESONATOR DECAY", "RESONATOR DECAY", "RESONATOR DECAY",
    "RESONATOR DECAY", "RESONATOR DECAY", "RESONATOR DECAY", "RESONATOR DECAY",
    "RESONATOR DECAY", "RESONATOR DECAY", "RESONATOR DECAY", "RESONATOR DECAY",
    "RESONATOR DECAY", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL", "DIRECT MIX LEVEL",
    "DIRECT MIX LEVEL", "EFFECT MIX LEVEL", "RESONATOR LEVEL", "DELAY MIX LEVEL",
    "REVERB MIX LEVEL",
};

inline constexpr Param kModC0[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 0, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 99, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 0, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 0, 68, 0, kModTargetsC0, 69 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Param kModC1[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 1, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 0, 67, 34, kModTargetsC1, 68 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 129, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 46, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 0, 67, 33, kModTargetsC1, 68 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 0, 67, 17, kModTargetsC1, 68 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 0, 67, 17, kModTargetsC1, 68 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 0, 67, 17, kModTargetsC1, 68 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 0, 67, 17, kModTargetsC1, 68 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 0, 67, 17, kModTargetsC1, 68 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 0, 67, 17, kModTargetsC1, 68 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Param kModC2[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 0, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 101, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 0, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 16, 66, 17, kModTargetsC2, 51 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Param kModC3[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 0, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 99, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 0, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 16, 67, 32, kModTargetsC3, 52 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Param kModC4[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 0, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 99, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 0, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 0, 66, 32, kModTargetsC4, 67 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Param kModC5[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 0, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 99, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 0, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 0, 68, 32, kModTargetsC5, 69 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Param kModC6[] = {
    { "mod_1_source", "Mod 1 Source", "", 9, 0, 0, 125, 0, kModSources, 126 },
    { "mod_1_target", "Mod 1 Target", "", 9, 1, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_1_amplitude", "Mod 1 Amplitude", "", 9, 2, 0, 198, 99, nullptr, 0 },
    { "mod_2_source", "Mod 2 Source", "", 9, 3, 0, 125, 0, kModSources, 126 },
    { "mod_2_target", "Mod 2 Target", "", 9, 4, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_2_amplitude", "Mod 2 Amplitude", "", 9, 5, 0, 198, 99, nullptr, 0 },
    { "mod_3_source", "Mod 3 Source", "", 9, 6, 0, 125, 0, kModSources, 126 },
    { "mod_3_target", "Mod 3 Target", "", 9, 7, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_3_amplitude", "Mod 3 Amplitude", "", 9, 8, 0, 198, 99, nullptr, 0 },
    { "mod_4_source", "Mod 4 Source", "", 9, 9, 0, 125, 0, kModSources, 126 },
    { "mod_4_target", "Mod 4 Target", "", 9, 10, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_4_amplitude", "Mod 4 Amplitude", "", 9, 11, 0, 198, 99, nullptr, 0 },
    { "mod_5_source", "Mod 5 Source", "", 9, 12, 0, 125, 0, kModSources, 126 },
    { "mod_5_target", "Mod 5 Target", "", 9, 13, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_5_amplitude", "Mod 5 Amplitude", "", 9, 14, 0, 198, 99, nullptr, 0 },
    { "mod_6_source", "Mod 6 Source", "", 9, 15, 0, 125, 0, kModSources, 126 },
    { "mod_6_target", "Mod 6 Target", "", 9, 16, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_6_amplitude", "Mod 6 Amplitude", "", 9, 17, 0, 198, 99, nullptr, 0 },
    { "mod_7_source", "Mod 7 Source", "", 9, 18, 0, 125, 0, kModSources, 126 },
    { "mod_7_target", "Mod 7 Target", "", 9, 19, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_7_amplitude", "Mod 7 Amplitude", "", 9, 20, 0, 198, 99, nullptr, 0 },
    { "mod_8_source", "Mod 8 Source", "", 9, 21, 0, 125, 0, kModSources, 126 },
    { "mod_8_target", "Mod 8 Target", "", 9, 22, 0, 68, 32, kModTargetsC6, 69 },
    { "mod_8_amplitude", "Mod 8 Amplitude", "", 9, 23, 0, 198, 99, nullptr, 0 },
};

inline constexpr Mode kModesReverbC0[] = {
    { "PLATE 1", 0, kReverbPlate1C0, 14 },
    { "ROOM 1", 1, kReverbPlate1C0, 14 },
    { "CHAMBER 1", 2, kReverbPlate1C0, 14 },
    { "HALL 1", 3, kReverbHall1C0, 13 },
    { "REVERSE 1", 4, kReverbReverse1C0, 10 },
};

inline constexpr Mode kModesDelayC0[] = {
    { "MONO DELAY", 0, kDelayMonoDelayC0, 4 },
    { "STEREO DELAY", 1, kDelayStereoDelayC0, 6 },
    { "PING PONG DELAY", 2, kDelayPingPongDelayC0, 4 },
};

inline constexpr Mode kModesPitchC0[] = {
    { "MONO CHORUS", 0, kPitchMonoChorusC0, 5 },
    { "STEREO CHORUS", 1, kPitchMonoChorusC0, 5 },
    { "MONO FLANGE", 2, kPitchMonoFlangeC0, 5 },
    { "STEREO FLANGE", 3, kPitchMonoFlangeC0, 5 },
    { "PITCH DETUNE", 4, kPitchPitchDetuneC0, 2 },
    { "PHASE SHIFTER", 5, kPitchPhaseShifterC0, 3 },
};

inline constexpr Mode kModesEqC0[] = {
    { "", 0, kEq020HzC0, 7 },
};

inline constexpr Mode kModesMixC0[] = {
    { "PRE-EQ", 0, kMixPreEqC0, 5 },
    { "POST-EQ", 1, kMixPostEqC0, 5 },
    { "POST-EQ PANNING", 2, kMixPostEqPanningC0, 7 },
    { "POST-EQ TREMOLO", 3, kMixPostEqTremoloC0, 7 },
};

inline constexpr Mode kModesReverbC1[] = {
    { "PLATE 1", 0, kReverbPlate1C1, 14 },
    { "ROOM 1", 1, kReverbPlate1C1, 14 },
    { "CHAMBER 1", 2, kReverbPlate1C1, 14 },
    { "HALL 1", 3, kReverbHall1C1, 13 },
    { "REVERSE 1", 4, kReverbReverse1C1, 10 },
};

inline constexpr Mode kModesDelayC1[] = {
    { "MONO DELAY", 0, kDelayMonoDelayC1, 3 },
    { "STEREO DELAY", 1, kDelayStereoDelayC1, 5 },
    { "PING PONG DELAY", 2, kDelayPingPongDelayC1, 3 },
};

inline constexpr Mode kModesPitchC1[] = {
    { "", 0, kPitchSeparation00C1, 3 },
};

inline constexpr Mode kModesEqC1[] = {
    { "", 0, kEq20DbC1, 1 },
};

inline constexpr Mode kModesMixC1[] = {
    { "", 0, kMixLevel00C1, 4 },
};

inline constexpr Mode kModesDelayC2[] = {
    { "MONO DELAY", 0, kDelayMonoDelayC2, 3 },
    { "STEREO DELAY", 1, kDelayStereoDelayC2, 5 },
    { "PING PONG DELAY", 2, kDelayPingPongDelayC2, 3 },
};

inline constexpr Mode kModesEqC2[] = {
    { "", 0, kEq16HzC2, 11 },
};

inline constexpr Mode kModesMixC2[] = {
    { "", 0, kMixLevel00C2, 3 },
};

inline constexpr Mode kModesDelayC3[] = {
    { "MONO DELAY", 0, kDelayMonoDelayC3, 4 },
    { "STEREO DELAY", 1, kDelayStereoDelayC3, 6 },
    { "PING PONG DELAY", 2, kDelayPingPongDelayC3, 4 },
    { "MULTI TAP DELAY", 3, kDelayMultiTapDelayC3, 8 },
};

inline constexpr Mode kModesPitchC3[] = {
    { "MONO CHORUS", 0, kPitchMonoChorusC3, 5 },
    { "STEREO CHORUS", 1, kPitchMonoChorusC3, 5 },
    { "MONO FLANGE", 2, kPitchMonoFlangeC3, 5 },
    { "STEREO FLANGE", 3, kPitchMonoFlangeC3, 5 },
    { "PITCH DETUNE", 4, kPitchPitchDetuneC3, 2 },
    { "PHASE SHIFTER", 5, kPitchPhaseShifterC3, 3 },
};

inline constexpr Mode kModesEqC3[] = {
    { "", 0, kEq020HzC3, 13 },
};

inline constexpr Mode kModesMixC3[] = {
    { "PRE-EQ", 0, kMixPreEqC3, 4 },
    { "POST-EQ", 1, kMixPostEqC3, 4 },
    { "POST-EQ PANNING", 2, kMixPostEqPanningC3, 6 },
    { "POST-EQ TREMOLO", 3, kMixPostEqTremoloC3, 6 },
};

inline constexpr Mode kModesReverbC4[] = {
    { "PLATE 2", 0, kReverbPlate2C4, 12 },
    { "ROOM 2", 1, kReverbPlate2C4, 12 },
    { "CHAMBER 2", 2, kReverbPlate2C4, 12 },
    { "HALL 2", 3, kReverbHall2C4, 11 },
    { "REVERSE 2", 4, kReverbReverse2C4, 8 },
};

inline constexpr Mode kModesPitchC4[] = {
    { "OFF", 0, nullptr, 0 },
    { "ON", 1, kPitchOnC4, 3 },
};

inline constexpr Mode kModesEqC4[] = {
    { "", 0, kEq020HzC4, 7 },
};

inline constexpr Mode kModesMixC4[] = {
    { "SELECT: PRE-EQ", 0, kMixSelectPreEqC4, 3 },
    { "SELECT: POST-EQ", 1, kMixSelectPostEqC4, 3 },
};

inline constexpr Mode kModesReverbC5[] = {
    { "PLATE 1", 0, kReverbPlate1C5, 14 },
    { "ROOM 1", 1, kReverbPlate1C5, 14 },
    { "CHAMBER 1", 2, kReverbPlate1C5, 14 },
    { "HALL 1", 3, kReverbHall1C5, 13 },
    { "REVERSE 1", 4, kReverbReverse1C5, 10 },
};

inline constexpr Mode kModesDelayC5[] = {
    { "MONO DELAY", 0, kDelayMonoDelayC5, 3 },
    { "STEREO DELAY", 1, kDelayStereoDelayC5, 5 },
    { "PING PONG DELAY", 2, kDelayPingPongDelayC5, 3 },
};

inline constexpr Mode kModesPitchC5[] = {
    { "", 0, kPitch001HzC5, 3 },
};

inline constexpr Mode kModesMixC5[] = {
    { "", 0, kMixLevel00C5, 5 },
};

inline constexpr Mode kModesReverbC6[] = {
    { "PLATE 1", 0, kReverbPlate1C6, 14 },
    { "ROOM 1", 1, kReverbPlate1C6, 14 },
    { "CHAMBER 1", 2, kReverbPlate1C6, 14 },
    { "HALL 1", 3, kReverbHall1C6, 13 },
    { "REVERSE 1", 4, kReverbReverse1C6, 10 },
};

inline constexpr Mode kModesDelayC6[] = {
    { "MONO DELAY", 0, kDelayMonoDelayC6, 3 },
    { "STEREO DELAY", 1, kDelayStereoDelayC6, 5 },
    { "PING PONG DELAY", 2, kDelayPingPongDelayC6, 3 },
};

inline constexpr Mode kModesPitchC6[] = {
    { "", 0, kPitchModeContinuousC6, 7 },
};

inline constexpr Mode kModesMixC6[] = {
    { "", 0, kMixLevel00C6, 5 },
};

inline constexpr Mode kModesDelayC7[] = {
    { "", 0, kDelayLoopingC7, 8 },
};

inline constexpr Mode kModesMixC7[] = {
    { "", 0, kMixLevel00C7, 2 },
};

inline constexpr Mode kModesModC0[] = {
    { "", 0, kModC0, 24 },
};

inline constexpr Mode kModesModC1[] = {
    { "", 0, kModC1, 24 },
};

inline constexpr Mode kModesModC2[] = {
    { "", 0, kModC2, 24 },
};

inline constexpr Mode kModesModC3[] = {
    { "", 0, kModC3, 24 },
};

inline constexpr Mode kModesModC4[] = {
    { "", 0, kModC4, 24 },
};

inline constexpr Mode kModesModC5[] = {
    { "", 0, kModC5, 24 },
};

inline constexpr Mode kModesModC6[] = {
    { "", 0, kModC6, 24 },
};

inline constexpr BlockDef kBlocksC0[] = {
    { Block::Reverb, "Reverb", 1, true, 0, 4, 3, kModesReverbC0, 5 },
    { Block::Delay, "Delay", 2, true, 0, 2, 1, kModesDelayC0, 3 },
    { Block::Pitch, "Pitch", 3, true, 0, 5, 1, kModesPitchC0, 6 },
    { Block::Eq, "3-Band EQ", 4, false, -1, -1, -1, kModesEqC0, 1 },
    { Block::Mix, "Mix", 8, true, 0, 3, 1, kModesMixC0, 4 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC0, 1 },
};

inline constexpr BlockDef kBlocksC1[] = {
    { Block::Reverb, "Reverb", 1, true, 0, 4, 3, kModesReverbC1, 5 },
    { Block::Delay, "Delay", 2, true, 0, 2, 1, kModesDelayC1, 3 },
    { Block::Pitch, "Lezlie", 3, false, -1, -1, -1, kModesPitchC1, 1 },
    { Block::Eq, "Rotor EQ", 4, false, -1, -1, -1, kModesEqC1, 1 },
    { Block::Mix, "Mix", 8, false, -1, -1, -1, kModesMixC1, 1 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC1, 1 },
};

inline constexpr BlockDef kBlocksC2[] = {
    { Block::Delay, "Delay", 2, true, 0, 2, 2, kModesDelayC2, 3 },
    { Block::Eq, "Graphic EQ", 4, false, -1, -1, -1, kModesEqC2, 1 },
    { Block::Mix, "Mix", 8, false, -1, -1, -1, kModesMixC2, 1 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC2, 1 },
};

inline constexpr BlockDef kBlocksC3[] = {
    { Block::Delay, "Delay", 2, true, 0, 3, 0, kModesDelayC3, 4 },
    { Block::Pitch, "Pitch", 3, true, 0, 5, 3, kModesPitchC3, 6 },
    { Block::Eq, "5-Band EQ", 4, false, -1, -1, -1, kModesEqC3, 1 },
    { Block::Mix, "Mix", 8, true, 0, 3, 1, kModesMixC3, 4 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC3, 1 },
};

inline constexpr BlockDef kBlocksC4[] = {
    { Block::Reverb, "Reverb", 1, true, 0, 4, 3, kModesReverbC4, 5 },
    { Block::Pitch, "Reverb Chorus", 3, true, 0, 1, 1, kModesPitchC4, 2 },
    { Block::Eq, "3-Band EQ", 4, false, -1, -1, -1, kModesEqC4, 1 },
    { Block::Mix, "Mix", 8, true, 0, 1, 0, kModesMixC4, 2 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC4, 1 },
};

inline constexpr BlockDef kBlocksC5[] = {
    { Block::Reverb, "Reverb", 1, true, 0, 4, 3, kModesReverbC5, 5 },
    { Block::Delay, "Delay", 2, true, 0, 2, 1, kModesDelayC5, 3 },
    { Block::Pitch, "Ring Modulator", 3, false, -1, -1, -1, kModesPitchC5, 1 },
    { Block::Mix, "Mix", 8, false, -1, -1, -1, kModesMixC5, 1 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC5, 1 },
};

inline constexpr BlockDef kBlocksC6[] = {
    { Block::Reverb, "Reverb", 1, true, 0, 4, 3, kModesReverbC6, 5 },
    { Block::Delay, "Delay", 2, true, 0, 2, 0, kModesDelayC6, 3 },
    { Block::Pitch, "Resonator", 3, false, -1, -1, -1, kModesPitchC6, 1 },
    { Block::Mix, "Mix", 8, false, -1, -1, -1, kModesMixC6, 1 },
    { Block::Mod, "Mod", 9, false, -1, -1, -1, kModesModC6, 1 },
};

inline constexpr BlockDef kBlocksC7[] = {
    { Block::Delay, "Sampler", 2, false, -1, -1, -1, kModesDelayC7, 1 },
    { Block::Mix, "Mix", 8, false, -1, -1, -1, kModesMixC7, 1 },
};

}  // namespace detail

inline constexpr ConfigDef kConfigs[kNumConfigs] = {
    { 0, "EQ > pitch > delay > reverb", detail::kBlocksC0, 6 },
    { 1, "Lezlie > delay > reverb", detail::kBlocksC1, 6 },
    { 2, "graphic EQ > delay", detail::kBlocksC2, 4 },
    { 3, "5-band EQ > pitch > delay", detail::kBlocksC3, 5 },
    { 4, "3-band EQ > reverb", detail::kBlocksC4, 5 },
    { 5, "ring modulator > delay > reverb", detail::kBlocksC5, 5 },
    { 6, "resonator > delay > reverb", detail::kBlocksC6, 5 },
    { 7, "sampling", detail::kBlocksC7, 2 },
};

// Mix is the one block every configuration has, so it is the only one that
// can be addressed without checking first.
inline constexpr uint8_t kMixFunction = 8;

inline const BlockDef* findBlock(const ConfigDef& c, Block b) {
    for (uint8_t i = 0; i < c.blockCount; ++i)
        if (c.blocks[i].block == b) return &c.blocks[i];
    return nullptr;
}

inline const Mode* findMode(const BlockDef& b, int page0Value) {
    if (!b.page0SelectsMode) return b.modeCount ? &b.modes[0] : nullptr;
    for (uint8_t i = 0; i < b.modeCount; ++i)
        if (b.modes[i].value == page0Value) return &b.modes[i];
    return nullptr;
}

}  // namespace qv
