#include "Utf8.h"
#include "SyxIO.h"
#include "../domain/AlesisCodec.h"
#include "../domain/DeviceProfile.h"

namespace qverse
{

namespace
{
    bool parseMessage (const uint8_t* data, int size,
                       std::vector<QuadraverbProgram>& out,
                       juce::String& error)
    {
        if (size < 8 || data[0] != 0xf0 || data[size - 1] != 0xf7)
        {
            error = utf8 ("Not a SysEx message");
            return false;
        }
        if (data[1] != 0x00 || data[2] != 0x00 || data[3] != 0x0e || data[4] != 0x02)
        {
            error = utf8 ("Not an Alesis Quadraverb SysEx dump");
            return false;
        }

        const uint8_t opcode = data[5];
        const uint8_t pp = data[6];
        const uint8_t* payload = data + 7;
        const int payloadLen = size - 8;

        if (opcode != 0x01 && opcode != 0x02)
        {
            error = utf8 ("Unsupported Quadraverb opcode (expected program dump/load)");
            return false;
        }

        auto pushDecoded = [&] (const uint8_t* enc, int encLen, int programNumber)
        {
            auto raw = AlesisCodec::decode (enc, encLen);
            if ((int) raw.size() < 128)
                return false;
            QuadraverbProgram prog;
            prog.model = DeviceModel::quadraverbPlus;
            prog.programNumber = programNumber;
            std::copy (raw.begin(), raw.begin() + 128, prog.bytes.begin());
            prog.hasValidBytes = true;
            prog.hydrateValuesFromBytes();
            out.push_back (std::move (prog));
            return true;
        };

        if (pp == AlesisCodec::kAllPrograms || payloadLen >= AlesisCodec::kEncodedProgramSize * 100)
        {
            if (payloadLen < AlesisCodec::kEncodedProgramSize * 100)
            {
                error = utf8 ("Bank dump too short");
                return false;
            }
            for (int i = 0; i < 100; ++i)
                if (! pushDecoded (payload + i * AlesisCodec::kEncodedProgramSize,
                                   AlesisCodec::kEncodedProgramSize, i))
                {
                    error = utf8 ("Failed to decode program ") + juce::String (i);
                    return false;
                }
            return true;
        }

        if (payloadLen < AlesisCodec::kEncodedProgramSize)
        {
            error = utf8 ("Single program dump too short");
            return false;
        }

        const int progNum = (pp == AlesisCodec::kEditBuffer) ? -1 : (int) pp;
        if (! pushDecoded (payload, AlesisCodec::kEncodedProgramSize, progNum))
        {
            error = utf8 ("Failed to decode program");
            return false;
        }
        return true;
    }
}

bool SyxIO::loadFile (const juce::File& file,
                      std::vector<QuadraverbProgram>& outPrograms,
                      juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb))
    {
        error = utf8 ("Could not read file");
        return false;
    }
    return loadFromMemory (mb.getData(), mb.getSize(), outPrograms, error);
}

bool SyxIO::loadFromMemory (const void* dataIn,
                            size_t sizeIn,
                            std::vector<QuadraverbProgram>& outPrograms,
                            juce::String& error)
{
    outPrograms.clear();
    const auto* data = (const uint8_t*) dataIn;
    const int size = (int) sizeIn;

    // One or more concatenated SysEx messages.
    int i = 0;
    bool any = false;
    while (i < size)
    {
        while (i < size && data[i] != 0xf0)
            ++i;
        if (i >= size)
            break;
        int end = i + 1;
        while (end < size && data[end] != 0xf7)
            ++end;
        if (end >= size)
        {
            error = utf8 ("Truncated SysEx");
            return false;
        }
        std::vector<QuadraverbProgram> batch;
        juce::String err;
        if (! parseMessage (data + i, end - i + 1, batch, err))
        {
            error = err;
            return false;
        }
        outPrograms.insert (outPrograms.end(), batch.begin(), batch.end());
        any = true;
        i = end + 1;
    }

    if (! any)
    {
        error = utf8 ("No SysEx data found");
        return false;
    }
    return true;
}

bool SyxIO::saveSingle (const juce::File& file,
                        const QuadraverbProgram& program,
                        uint8_t programSlot,
                        juce::String& error)
{
    auto prog = program;
    prog.flushValuesToBytes();
    if (! prog.hasValidBytes)
    {
        error = utf8 ("Program has no dump bytes to encode");
        return false;
    }

    const auto msg = AlesisCodec::buildLoadProgram (
        profileFor (prog.model).getSysexProductId(),
        programSlot,
        prog.bytes.data());

    juce::MemoryBlock mb;
    mb.append (msg.getRawData(), (size_t) msg.getRawDataSize());
    if (! file.replaceWithData (mb.getData(), mb.getSize()))
    {
        error = utf8 ("Could not write .syx file");
        return false;
    }
    return true;
}

bool SyxIO::savePrograms (const juce::File& file,
                          const std::vector<QuadraverbProgram>& programs,
                          juce::String& error)
{
    if (programs.empty())
    {
        error = utf8 ("No programs to save");
        return false;
    }

    juce::MemoryBlock mb;
    for (size_t i = 0; i < programs.size(); ++i)
    {
        auto prog = programs[i];
        prog.flushValuesToBytes();
        if (! prog.hasValidBytes)
        {
            error = utf8 ("Program has no dump bytes to encode");
            return false;
        }

        const uint8_t slot = programs.size() == 1
                                 ? AlesisCodec::kEditBuffer
                                 : (uint8_t) juce::jmin (99, (int) i);
        const auto msg = AlesisCodec::buildLoadProgram (
            profileFor (prog.model).getSysexProductId(),
            slot,
            prog.bytes.data());
        mb.append (msg.getRawData(), (size_t) msg.getRawDataSize());
    }

    if (! file.replaceWithData (mb.getData(), mb.getSize()))
    {
        error = utf8 ("Could not write .syx file");
        return false;
    }
    return true;
}

bool SyxIO::saveBank (const juce::File& file,
                      const std::vector<QuadraverbProgram>& programs,
                      juce::String& error)
{
    if (programs.size() != 100)
    {
        error = utf8 ("Bank save requires exactly 100 programs");
        return false;
    }

    std::vector<uint8_t> out;
    out.push_back (0xf0);
    out.push_back (0x00);
    out.push_back (0x00);
    out.push_back (0x0e);
    out.push_back (0x02);
    out.push_back (0x02);
    out.push_back (AlesisCodec::kAllPrograms);

    for (const auto& p : programs)
    {
        auto prog = p;
        prog.flushValuesToBytes();
        auto enc = AlesisCodec::encode (prog.bytes.data(), 128);
        if ((int) enc.size() < AlesisCodec::kEncodedProgramSize)
            enc.resize ((size_t) AlesisCodec::kEncodedProgramSize, 0);
        out.insert (out.end(), enc.begin(), enc.begin() + AlesisCodec::kEncodedProgramSize);
    }
    out.push_back (0xf7);

    if (! file.replaceWithData (out.data(), out.size()))
    {
        error = utf8 ("Could not write bank .syx");
        return false;
    }
    return true;
}

} // namespace qverse
