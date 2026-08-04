#include <JuceHeader.h>
#include "domain/AlesisCodec.h"
#include "formats/SyxIO.h"
#include "formats/SsxImporter.h"
#include <iostream>
#include <cstdlib>

int main()
{
    int failures = 0;

    // Encode/decode round-trip
    std::array<uint8_t, 128> raw {};
    for (int i = 0; i < 128; ++i)
        raw[(size_t) i] = (uint8_t) (i * 3 + 7);

    auto enc = qverse::AlesisCodec::encode (raw.data(), 128);
    auto dec = qverse::AlesisCodec::decode (enc.data(), (int) enc.size());
    if (dec.size() < 128)
    {
        std::cerr << "decode length " << dec.size() << "\n";
        ++failures;
    }
    else
    {
        for (int i = 0; i < 128; ++i)
            if (dec[(size_t) i] != raw[(size_t) i])
            {
                std::cerr << "mismatch at " << i << "\n";
                ++failures;
                break;
            }
    }

    // Change Parameter packing: value 1 → encoded [0x00, 0x40, 0x00]
    {
        const auto msg = qverse::AlesisCodec::buildChangeParameter (0x02, 2, 1, 1);
        const auto* d = msg.getSysExData();
        const int n = msg.getSysExDataSize();
        // body: 00 00 0e 02 01 gg pp v1 v2 v3
        if (n < 10 || (uint8_t) d[7] != 0x00 || (uint8_t) d[8] != 0x40 || (uint8_t) d[9] != 0x00)
        {
            std::cerr << "Change Parameter packing for value 1 incorrect\n";
            ++failures;
        }
    }

    // Change Parameter packing: value 99 → [0x31, 0x40, 0x00]
    {
        const auto msg = qverse::AlesisCodec::buildChangeParameter (0x02, 8, 1, 99);
        const auto* d = msg.getSysExData();
        if ((uint8_t) d[7] != 0x31 || (uint8_t) d[8] != 0x40 || (uint8_t) d[9] != 0x00)
        {
            std::cerr << "Change Parameter packing for value 99 incorrect: "
                      << (int) (uint8_t) d[7] << " " << (int) (uint8_t) d[8] << " "
                      << (int) (uint8_t) d[9] << "\n";
            ++failures;
        }
    }

    // SSX fixture
    const char* fixtures = std::getenv ("QUADRAVERSE_FIXTURES_DIR");
#ifdef QUADRAVERSE_FIXTURES_DIR
    if (fixtures == nullptr)
        fixtures = QUADRAVERSE_FIXTURES_DIR;
#endif
    if (fixtures != nullptr)
    {
        juce::File ssx (juce::String (fixtures) + "/QV1.SSX");
        if (ssx.existsAsFile())
        {
            const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("quadraverse_test_out.syx");
            juce::String error;
            int count = 0;
            if (! qverse::SsxImporter::importToSyx (ssx, tmp, true, error, &count))
            {
                std::cerr << "ssx import failed: " << error << "\n";
                ++failures;
            }
            else if (count != 100)
            {
                std::cerr << "expected 100 programs, got " << count << "\n";
                ++failures;
            }
            else
            {
                std::vector<qverse::QuadraverbProgram> programs;
                if (! qverse::SyxIO::loadFile (tmp, programs, error))
                {
                    std::cerr << "syx reload failed: " << error << "\n";
                    ++failures;
                }
                else
                {
                    std::cout << "Loaded " << programs.size() << " programs; first name='"
                              << programs.front().name << "'\n";
                }
            }
            tmp.deleteFile();
        }
        else
            std::cout << "SSX fixture not found, skipping\n";
    }

    if (failures == 0)
        std::cout << "All quadraverse tests passed\n";
    return failures == 0 ? 0 : 1;
}
