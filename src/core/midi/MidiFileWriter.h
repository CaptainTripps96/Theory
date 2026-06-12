#pragma once

#include <cstdint>
#include <vector>

namespace tsq::core::midi
{
struct MidiFileEvent
{
    std::int64_t tick = 0;
    int priority = 0;
    std::vector<std::uint8_t> data;
};

class MidiFileWriter
{
public:
    static std::vector<std::uint8_t> writeFormat0 (int pulsesPerQuarterNote, std::vector<MidiFileEvent> events);
};
}
