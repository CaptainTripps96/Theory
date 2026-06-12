#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tsq::engine
{
struct AudioOutputDevice
{
    std::string deviceType;
    std::string deviceName;
    std::string displayName;
};

struct AudioDeviceSettings
{
    bool hasOpenDevice = false;
    std::string outputDeviceType;
    std::string outputDeviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    std::string message;
};

struct PlaybackEngineStatus
{
    bool initialized = false;
    bool playing = false;
    std::string backendName;
    std::string backendVersion;
    std::string audioDeviceType;
    std::string audioDeviceName;
    double sampleRate = 0.0;
    int blockSize = 0;
    std::string message;
};

struct TestInstrumentStatus
{
    bool pluginLoaded = false;
    bool phraseReady = false;
    bool pluginEditorSupported = false;
    std::string pluginName;
    std::string pluginIdentifier;
    std::string message;
};

struct TrackPluginState
{
    std::string trackId;
    std::vector<std::uint8_t> data;
};

struct MeterChannelSnapshot
{
    float peakDb = -100.0f;
    float peakLinear = 0.0f;
    bool peakOverload = false;
    bool rmsAvailable = false;
    float rmsDb = -100.0f;
    float rmsLinear = 0.0f;
};

struct MeterSourceSnapshot
{
    std::string sourceId;
    std::string trackId;
    std::string displayName;
    bool master = false;
    bool returnTrack = false;
    bool active = true;
    std::vector<MeterChannelSnapshot> channels;
};

struct MeterSnapshot
{
    bool playing = false;
    std::uint64_t sequence = 0;
    std::vector<MeterSourceSnapshot> sources;
};
}
