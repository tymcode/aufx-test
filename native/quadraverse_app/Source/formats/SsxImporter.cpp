#include "SsxImporter.h"
#include "Utf8.h"
#include "SyxIO.h"
#include "../domain/AlesisCodec.h"

namespace qverse
{

bool SsxImporter::importToSyx (const juce::File& ssxFile,
                               const juce::File& destSyx,
                               bool emitIndividualPrograms,
                               juce::String& error,
                               int* outProgramCount)
{
    juce::MemoryBlock mb;
    if (! ssxFile.loadFileAsData (mb))
    {
        error = utf8 ("Could not read .ssx file");
        return false;
    }

    const auto* data = (const uint8_t*) mb.getData();
    const int size = (int) mb.getSize();
    if (size < 8 || data[0] != 0xf0 || data[size - 1] != 0xf7)
    {
        error = utf8 (".ssx is not raw SysEx (expected F0…F7)");
        return false;
    }
    if (data[1] != 0x00 || data[2] != 0x00 || data[3] != 0x0e || data[4] != 0x02)
    {
        error = utf8 (".ssx is not an Alesis Quadraverb dump");
        return false;
    }

    std::vector<QuadraverbProgram> programs;
    if (! SyxIO::loadFile (ssxFile, programs, error))
        return false;

    if (outProgramCount != nullptr)
        *outProgramCount = (int) programs.size();

    if (programs.empty())
    {
        error = utf8 ("No programs decoded from .ssx");
        return false;
    }

    destSyx.getParentDirectory().createDirectory();

    if (! emitIndividualPrograms && programs.size() == 100)
        return SyxIO::saveBank (destSyx, programs, error);

    if (! emitIndividualPrograms && programs.size() == 1)
        return SyxIO::saveSingle (destSyx, programs.front(), AlesisCodec::kEditBuffer, error);

    // Individual program messages concatenated (friendlier for some hardware).
    juce::MemoryOutputStream mos;
    for (size_t i = 0; i < programs.size(); ++i)
    {
        auto prog = programs[i];
        prog.flushValuesToBytes();
        const uint8_t slot = programs.size() == 1
            ? AlesisCodec::kEditBuffer
            : (uint8_t) juce::jlimit (0, 99, (int) i);
        const auto msg = AlesisCodec::buildLoadProgram (0x02, slot, prog.bytes.data());
        mos.write (msg.getRawData(), (size_t) msg.getRawDataSize());
    }
    if (! destSyx.replaceWithData (mos.getData(), mos.getDataSize()))
    {
        error = utf8 ("Could not write converted .syx");
        return false;
    }
    return true;
}

} // namespace qverse
