#include "AlesisCodec.h"

namespace qverse
{

std::vector<uint8_t> AlesisCodec::decode (const uint8_t* encoded, int encodedLen)
{
    std::vector<uint8_t> out;
    out.reserve ((size_t) (encodedLen * 7 / 8));
    uint32_t acc = 0;
    int bits = 0;

    for (int i = 0; i < encodedLen; ++i)
    {
        acc = (acc << 7) | (uint32_t) (encoded[i] & 0x7f);
        bits += 7;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back ((uint8_t) ((acc >> bits) & 0xff));
        }
    }
    return out;
}

std::vector<uint8_t> AlesisCodec::encode (const uint8_t* raw, int rawLen)
{
    std::vector<uint8_t> out;
    out.reserve ((size_t) ((rawLen * 8 + 6) / 7));
    uint32_t acc = 0;
    int bits = 0;

    for (int i = 0; i < rawLen; ++i)
    {
        acc = (acc << 8) | (uint32_t) raw[i];
        bits += 8;
        while (bits >= 7)
        {
            bits -= 7;
            out.push_back ((uint8_t) ((acc >> bits) & 0x7f));
        }
    }
    if (bits > 0)
        out.push_back ((uint8_t) ((acc << (7 - bits)) & 0x7f));
    return out;
}

juce::MidiMessage AlesisCodec::buildChangeParameter (uint8_t productId,
                                                     uint8_t function,
                                                     uint8_t page,
                                                     uint16_t value)
{
    // Service manual / Bob Page: value is packed with the same 8↔7 encoding
    // as program dumps. Always three MIDI data bytes after gg/pp.
    // Two raw bytes LSB then MSB → three encoded bytes (matches the manual's
    // Sent: bit diagram). Values that fit in one byte are just MSB=0.
    const uint8_t raw[2] = { (uint8_t) (value & 0xff), (uint8_t) ((value >> 8) & 0xff) };
    auto encoded = encode (raw, 2);
    // encode() of 2 bytes yields 3 MIDI bytes; pad defensively if short.
    uint8_t v1 = encoded.size() > 0 ? encoded[0] : 0;
    uint8_t v2 = encoded.size() > 1 ? encoded[1] : 0;
    uint8_t v3 = encoded.size() > 2 ? encoded[2] : 0;

    const uint8_t data[] = {
        0x00, 0x00, 0x0e, productId, 0x01, function, page, v1, v2, v3
    };
    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

juce::MidiMessage AlesisCodec::buildLoadProgram (uint8_t productId,
                                                 uint8_t programOrEdit,
                                                 const uint8_t* raw128)
{
    auto encoded = encode (raw128, 128);
    std::vector<uint8_t> body;
    body.reserve (4 + 1 + 1 + encoded.size());
    body.push_back (0x00);
    body.push_back (0x00);
    body.push_back (0x0e);
    body.push_back (productId);
    body.push_back (0x02); // Load Program
    body.push_back (programOrEdit);
    body.insert (body.end(), encoded.begin(), encoded.end());
    return juce::MidiMessage::createSysExMessage (body.data(), (int) body.size());
}

} // namespace qverse
