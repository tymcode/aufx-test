#pragma once

#include "QvParameters.h"

// Stable identity for every MIDI-mappable QDV1 control, modelled on DEEP/Z's
// MidiLearn.h.
//
// A block parameter is addressed exactly the way the hardware addresses it --
// (function, page) -- scoped by the CONFIGURATION and by the block's MODE,
// because a page means nothing outside those two.  A mapping made while
// config 0's reverb is on PLATE therefore stays dormant until config 0 /
// PLATE is selected again, which is DEEP/Z's per-algorithm rule transplanted
// onto the QuadraVerb's address space.
//
// Page 0 is either the block's own mode selector or, on blocks that have no
// selector, an ordinary parameter.  Either way it does not live inside a
// mode, so every page-0 target is normalised to mode 0 (see param() below).
enum class QvMidiTargetKind : int
{
    parameter   = 0,   // a (config, function, mode, page) block parameter
    inputLevel  = 1,   // the front panel's analog INPUT trim
    outputLevel = 2,   // the front panel's analog OUTPUT trim
    bypass      = 3    // the BYPASS key, the hardware's remote-bypass equivalent
};

constexpr int kQvMidiConfigs   = 8;
constexpr int kQvMidiFunctions = 10;
constexpr int kQvMidiModes     = 8;
constexpr int kQvMidiPages     = 32;

struct QvMidiTarget
{
    QvMidiTargetKind kind = QvMidiTargetKind::parameter;
    int config   = -1;
    int function = -1;
    int mode     = 0;
    int page     = -1;

    static QvMidiTarget param (int configIn, int functionIn, int modeIn, int pageIn)
    {
        return { QvMidiTargetKind::parameter, configIn, functionIn,
                 pageIn == 0 ? 0 : modeIn, pageIn };
    }

    static QvMidiTarget inputLevel()  { return { QvMidiTargetKind::inputLevel,  0, 0, 0, 0 }; }
    static QvMidiTarget outputLevel() { return { QvMidiTargetKind::outputLevel, 0, 0, 0, 0 }; }
    static QvMidiTarget bypass()      { return { QvMidiTargetKind::bypass,      0, 0, 0, 0 }; }

    bool isValid() const
    {
        if (kind != QvMidiTargetKind::parameter)
            return true;
        return config >= 0 && config < kQvMidiConfigs
            && function >= 1 && function < kQvMidiFunctions
            && mode >= 0 && mode < kQvMidiModes
            && page >= 0 && page < kQvMidiPages;
    }

    bool operator== (const QvMidiTarget& o) const
    {
        return kind == o.kind && config == o.config && function == o.function
            && mode == o.mode && page == o.page;
    }
    bool operator!= (const QvMidiTarget& o) const { return ! (*this == o); }
};

struct QvMidiBinding
{
    bool active = false;
    int  channel = -1;   // learned bindings use an exact 0-based MIDI channel
    int  cc = 0;
};

constexpr int kQvMidiParamSlots =
    kQvMidiConfigs * kQvMidiFunctions * kQvMidiModes * kQvMidiPages;
constexpr int kQvMidiPanelBase   = kQvMidiParamSlots;
constexpr int kQvMidiTargetSlots = kQvMidiPanelBase + 3;

inline int qvMidiTargetIndex (const QvMidiTarget& t)
{
    if (! t.isValid())
        return -1;
    if (t.kind == QvMidiTargetKind::parameter)
        return (((t.config * kQvMidiFunctions + t.function) * kQvMidiModes + t.mode)
                * kQvMidiPages) + t.page;
    return kQvMidiPanelBase + ((int) t.kind - 1);
}

// The block a parameter target belongs to in its own configuration, or null
// when that configuration has no such block.
inline const qv::BlockDef* qvMidiBlockFor (const QvMidiTarget& t)
{
    if (t.kind != QvMidiTargetKind::parameter || ! t.isValid())
        return nullptr;
    const auto& cfg = qv::kConfigs[t.config];
    for (int i = 0; i < cfg.blockCount; ++i)
        if (cfg.blocks[i].function == t.function)
            return &cfg.blocks[i];
    return nullptr;
}

// The unit's own clamps for a target, which is what a 0..127 controller
// is scaled into.  False when the target does not exist any more (a page the
// current configuration/mode does not have).
inline bool qvMidiTargetRange (const QvMidiTarget& t, int& lo, int& hi)
{
    switch (t.kind)
    {
        case QvMidiTargetKind::inputLevel:
        case QvMidiTargetKind::outputLevel: lo = 0; hi = 99; return true;
        case QvMidiTargetKind::bypass:      lo = 0; hi = 1;  return true;
        case QvMidiTargetKind::parameter:   break;   // range comes from the block
    }

    const auto* blk = qvMidiBlockFor (t);
    if (blk == nullptr)
        return false;

    if (t.page == 0 && blk->page0SelectsMode)
    {
        lo = blk->modeMin;
        hi = blk->modeMax;
        return true;
    }

    const auto* mode = qv::findMode (*blk, t.mode);
    if (mode == nullptr)
        return false;
    for (int i = 0; i < mode->count; ++i)
        if (mode->params[i].page == t.page)
        {
            lo = mode->params[i].min;
            hi = mode->params[i].max;
            return true;
        }
    return false;
}
