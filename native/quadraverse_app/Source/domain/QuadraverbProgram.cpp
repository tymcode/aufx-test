#include "QuadraverbProgram.h"

namespace qverse
{

void QuadraverbProgram::clearValues()
{
    for (int f = 0; f < kMaxFunctions; ++f)
        for (int p = 0; p < kMaxPages; ++p)
        {
            value[f][p] = 0;
            known[f][p] = false;
        }
}

void QuadraverbProgram::setParam (int function, int page, int v)
{
    if (function < 0 || function >= kMaxFunctions || page < 0 || page >= kMaxPages)
        return;
    value[function][page] = v;
    known[function][page] = true;
    if (function == 7 && page == 0)
        config = v;
}

int QuadraverbProgram::getParam (int function, int page, int fallback) const
{
    if (function < 0 || function >= kMaxFunctions || page < 0 || page >= kMaxPages)
        return fallback;
    return known[function][page] ? value[function][page] : fallback;
}

bool QuadraverbProgram::isKnown (int function, int page) const
{
    if (function < 0 || function >= kMaxFunctions || page < 0 || page >= kMaxPages)
        return false;
    return known[function][page];
}

void QuadraverbProgram::setName (const juce::String& n)
{
    name = n.substring (0, kNameLength);
    while (name.length() < kNameLength)
        name << ' ';
    syncNameToBytes();
}

void QuadraverbProgram::syncNameFromBytes()
{
    if (! hasValidBytes)
        return;
    juce::String n;
    for (int i = 0; i < kNameLength; ++i)
    {
        const auto c = bytes[(size_t) (106 + i)];
        n << (juce::juce_wchar) ((c >= 32 && c < 127) ? c : ' ');
    }
    name = n.trimEnd();
}

void QuadraverbProgram::syncNameToBytes()
{
    juce::String padded = name;
    while (padded.length() < kNameLength)
        padded << ' ';
    padded = padded.substring (0, kNameLength);
    for (int i = 0; i < kNameLength; ++i)
        bytes[(size_t) (106 + i)] = (uint8_t) padded[i];
    hasValidBytes = true;
}

void QuadraverbProgram::syncConfigFromBytes()
{
    if (! hasValidBytes)
        return;
    config = (int) bytes[68];
    setParam (7, 0, config);
}

void QuadraverbProgram::syncConfigToBytes()
{
    bytes[68] = (uint8_t) juce::jlimit (0, 7, config);
    hasValidBytes = true;
}

namespace
{
    void putU16 (std::array<uint8_t, 128>& b, int offset, int v)
    {
        b[(size_t) offset] = (uint8_t) ((v >> 8) & 0xff);
        b[(size_t) (offset + 1)] = (uint8_t) (v & 0xff);
    }

    int getU16 (const std::array<uint8_t, 128>& b, int offset)
    {
        return ((int) b[(size_t) offset] << 8) | (int) b[(size_t) (offset + 1)];
    }
}

void QuadraverbProgram::hydrateValuesFromBytes()
{
    if (! hasValidBytes)
        return;

    clearValues();
    syncConfigFromBytes();
    syncNameFromBytes();

    // Mix (function 8) — Bob Page bytes 69-74 map to pages 0-5 in config 0.
    setParam (8, 0, bytes[69]);
    setParam (8, 1, bytes[70]);
    setParam (8, 2, bytes[71]);
    setParam (8, 3, bytes[72]);
    setParam (8, 4, bytes[73]);
    setParam (8, 5, bytes[74]);

    // Pitch mode + common (function 3) — approximate page 0 = mode
    setParam (3, 0, bytes[26]);
    setParam (3, 1, bytes[27]);
    setParam (3, 2, bytes[28]);
    setParam (3, 3, bytes[29]);
    setParam (3, 4, bytes[30]);
    setParam (3, 5, bytes[32]);

    // Delay (function 2)
    setParam (2, 0, bytes[39]);
    setParam (2, 1, bytes[40]);
    setParam (2, 2, bytes[41]);
    setParam (2, 3, getU16 (bytes, 42));
    setParam (2, 4, bytes[44]);
    setParam (2, 5, getU16 (bytes, 45));
    setParam (2, 6, bytes[47]);

    // Reverb (function 1) — page 0 = type/mode
    setParam (1, 0, bytes[50]);
    setParam (1, 1, bytes[52]);
    setParam (1, 2, bytes[53]);
    setParam (1, 3, bytes[54]);
    setParam (1, 4, bytes[55]);
    setParam (1, 5, bytes[56]);
    setParam (1, 6, bytes[57]);
    setParam (1, 7, bytes[58]);
    setParam (1, 8, bytes[61]);
    setParam (1, 9, bytes[59]);
    setParam (1, 10, bytes[60]);
    setParam (1, 11, bytes[62]);
    setParam (1, 12, bytes[63]);
    setParam (1, 13, bytes[64]);
    setParam (1, 14, bytes[65]);

    // Mod slots (function 9) — 8 sources/targets/amplitudes
    for (int m = 0; m < 8; ++m)
    {
        setParam (9, m * 3 + 0, bytes[(size_t) (80 + m * 3)]);
        setParam (9, m * 3 + 1, bytes[(size_t) (81 + m * 3)]);
        setParam (9, m * 3 + 2, bytes[(size_t) (82 + m * 3)]);
    }
}

void QuadraverbProgram::flushValuesToBytes()
{
    if (! hasValidBytes)
        bytes.fill (0);

    syncConfigToBytes();
    syncNameToBytes();

    if (isKnown (8, 0)) bytes[69] = (uint8_t) getParam (8, 0);
    if (isKnown (8, 1)) bytes[70] = (uint8_t) getParam (8, 1);
    if (isKnown (8, 2)) bytes[71] = (uint8_t) getParam (8, 2);
    if (isKnown (8, 3)) bytes[72] = (uint8_t) getParam (8, 3);
    if (isKnown (8, 4)) bytes[73] = (uint8_t) getParam (8, 4);
    if (isKnown (8, 5)) bytes[74] = (uint8_t) getParam (8, 5);

    if (isKnown (3, 0)) bytes[26] = (uint8_t) getParam (3, 0);
    if (isKnown (3, 1)) bytes[27] = (uint8_t) getParam (3, 1);
    if (isKnown (3, 2)) bytes[28] = (uint8_t) getParam (3, 2);
    if (isKnown (3, 3)) bytes[29] = (uint8_t) getParam (3, 3);
    if (isKnown (3, 4)) bytes[30] = (uint8_t) getParam (3, 4);
    if (isKnown (3, 5)) bytes[32] = (uint8_t) getParam (3, 5);

    if (isKnown (2, 0)) bytes[39] = (uint8_t) getParam (2, 0);
    if (isKnown (2, 1)) bytes[40] = (uint8_t) getParam (2, 1);
    if (isKnown (2, 2)) bytes[41] = (uint8_t) getParam (2, 2);
    if (isKnown (2, 3)) putU16 (bytes, 42, getParam (2, 3));
    if (isKnown (2, 4)) bytes[44] = (uint8_t) getParam (2, 4);
    if (isKnown (2, 5)) putU16 (bytes, 45, getParam (2, 5));
    if (isKnown (2, 6)) bytes[47] = (uint8_t) getParam (2, 6);

    if (isKnown (1, 0)) bytes[50] = (uint8_t) getParam (1, 0);
    if (isKnown (1, 1)) bytes[52] = (uint8_t) getParam (1, 1);
    if (isKnown (1, 2)) bytes[53] = (uint8_t) getParam (1, 2);
    if (isKnown (1, 3)) bytes[54] = (uint8_t) getParam (1, 3);
    if (isKnown (1, 4)) bytes[55] = (uint8_t) getParam (1, 4);
    if (isKnown (1, 5)) bytes[56] = (uint8_t) getParam (1, 5);
    if (isKnown (1, 6)) bytes[57] = (uint8_t) getParam (1, 6);
    if (isKnown (1, 7)) bytes[58] = (uint8_t) getParam (1, 7);
    if (isKnown (1, 8)) bytes[61] = (uint8_t) getParam (1, 8);
    if (isKnown (1, 9)) bytes[59] = (uint8_t) getParam (1, 9);
    if (isKnown (1, 10)) bytes[60] = (uint8_t) getParam (1, 10);
    if (isKnown (1, 11)) bytes[62] = (uint8_t) getParam (1, 11);
    if (isKnown (1, 12)) bytes[63] = (uint8_t) getParam (1, 12);
    if (isKnown (1, 13)) bytes[64] = (uint8_t) getParam (1, 13);
    if (isKnown (1, 14)) bytes[65] = (uint8_t) getParam (1, 14);

    for (int m = 0; m < 8; ++m)
    {
        if (isKnown (9, m * 3 + 0)) bytes[(size_t) (80 + m * 3)] = (uint8_t) getParam (9, m * 3 + 0);
        if (isKnown (9, m * 3 + 1)) bytes[(size_t) (81 + m * 3)] = (uint8_t) getParam (9, m * 3 + 1);
        if (isKnown (9, m * 3 + 2)) bytes[(size_t) (82 + m * 3)] = (uint8_t) getParam (9, m * 3 + 2);
    }

    hasValidBytes = true;
}

ParamRuntimeMeta& QuadraverbProgram::metaFor (const ParamAddress& addr)
{
    const auto k = addr.key().toStdString();
    auto it = paramMeta.find (k);
    if (it == paramMeta.end())
    {
        ParamRuntimeMeta m;
        m.randomMin = 0;
        m.randomMax = 99;
        it = paramMeta.emplace (k, m).first;
    }
    return it->second;
}

const ParamRuntimeMeta* QuadraverbProgram::findMeta (const ParamAddress& addr) const
{
    const auto it = paramMeta.find (addr.key().toStdString());
    return it != paramMeta.end() ? &it->second : nullptr;
}

juce::var QuadraverbProgram::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("model", toString (model));
    obj->setProperty ("programNumber", programNumber);
    obj->setProperty ("config", config);
    obj->setProperty ("inLevel", inLevel);
    obj->setProperty ("outLevel", outLevel);
    obj->setProperty ("name", name);
    obj->setProperty ("hasValidBytes", hasValidBytes);

    if (hasValidBytes)
    {
        juce::MemoryBlock mb (bytes.data(), bytes.size());
        obj->setProperty ("bytesBase64", juce::Base64::toBase64 (mb.getData(), mb.getSize()));
    }

    juce::Array<juce::var> params;
    for (int f = 0; f < kMaxFunctions; ++f)
        for (int p = 0; p < kMaxPages; ++p)
            if (known[f][p])
            {
                auto* pe = new juce::DynamicObject();
                pe->setProperty ("f", f);
                pe->setProperty ("p", p);
                pe->setProperty ("v", value[f][p]);
                params.add (pe);
            }
    obj->setProperty ("params", params);

    juce::Array<juce::var> metas;
    for (const auto& kv : paramMeta)
    {
        auto* me = new juce::DynamicObject();
        me->setProperty ("key", juce::String (kv.first));
        me->setProperty ("protectFromAdjustment", kv.second.protectFromAdjustment);
        me->setProperty ("protectFromRandomization", kv.second.protectFromRandomization);
        me->setProperty ("randomMin", kv.second.randomMin);
        me->setProperty ("randomMax", kv.second.randomMax);
        me->setProperty ("hasCustomRandomRange", kv.second.hasCustomRandomRange);
        metas.add (me);
    }
    obj->setProperty ("paramMeta", metas);
    return obj;
}

QuadraverbProgram QuadraverbProgram::fromVar (const juce::var& v)
{
    QuadraverbProgram prog;
    if (auto* obj = v.getDynamicObject())
    {
        prog.model = deviceModelFromString (obj->getProperty ("model").toString());
        prog.programNumber = (int) obj->getProperty ("programNumber");
        prog.config = (int) obj->getProperty ("config");
        prog.inLevel = (int) obj->getProperty ("inLevel");
        prog.outLevel = (int) obj->getProperty ("outLevel");
        prog.name = obj->getProperty ("name").toString();
        prog.hasValidBytes = (bool) obj->getProperty ("hasValidBytes");

        const auto b64 = obj->getProperty ("bytesBase64").toString();
        if (b64.isNotEmpty())
        {
            juce::MemoryOutputStream mo;
            if (juce::Base64::convertFromBase64 (mo, b64))
            {
                const auto* data = (const uint8_t*) mo.getData();
                const int n = (int) juce::jmin ((size_t) kProgramBytes, mo.getDataSize());
                std::copy (data, data + n, prog.bytes.begin());
                prog.hasValidBytes = true;
            }
        }

        if (auto* arr = obj->getProperty ("params").getArray())
        {
            for (const auto& item : *arr)
                if (auto* pe = item.getDynamicObject())
                    prog.setParam ((int) pe->getProperty ("f"),
                                   (int) pe->getProperty ("p"),
                                   (int) pe->getProperty ("v"));
        }

        if (auto* arr = obj->getProperty ("paramMeta").getArray())
        {
            for (const auto& item : *arr)
                if (auto* me = item.getDynamicObject())
                {
                    ParamAddress addr;
                    const auto key = me->getProperty ("key").toString();
                    addr.function = key.upToFirstOccurrenceOf (".", false, false).getIntValue();
                    addr.page = key.fromFirstOccurrenceOf (".", false, false).getIntValue();
                    auto& meta = prog.metaFor (addr);
                    meta.protectFromAdjustment = (bool) me->getProperty ("protectFromAdjustment");
                    meta.protectFromRandomization = (bool) me->getProperty ("protectFromRandomization");
                    meta.randomMin = (int) me->getProperty ("randomMin");
                    meta.randomMax = (int) me->getProperty ("randomMax");
                    meta.hasCustomRandomRange = (bool) me->getProperty ("hasCustomRandomRange");
                }
        }
    }
    return prog;
}

} // namespace qverse
