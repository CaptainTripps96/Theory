#pragma once

#include <string>

namespace tsq::app
{
struct MidiInputDeviceInfo
{
    std::string identifier;
    std::string name;
    std::string displayName;
};

struct QueuedMidiInputEvent
{
    enum class Type
    {
        noteOn,
        noteOff
    };

    Type type = Type::noteOn;
    int channel = 1;
    int noteNumber = 60;
    int velocity = 0;
    double timestampSeconds = 0.0;
};
}
