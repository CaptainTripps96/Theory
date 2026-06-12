#pragma once

#include <string>

namespace tsq::app
{
struct AppSettings
{
    static constexpr int currentSchemaVersion = 1;

    int schemaVersion = currentSchemaVersion;
    std::string audioDeviceStateXml;
    std::string outputDeviceType;
    std::string outputDeviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    std::string selectedTestInstrumentIdentifier;
    std::string selectedTestInstrumentName;

    bool hasAudioDeviceState() const noexcept;
    bool hasOutputDeviceChoice() const noexcept;
    bool hasSelectedTestInstrument() const noexcept;
};
}
