#pragma once

#include <string>

namespace tsq::core::sequencing
{
struct TrackInstrumentReference
{
    std::string pluginName;
    std::string manufacturer;
    std::string format;
    std::string fileOrIdentifier;
    std::string uniqueIdentifier;
    int uniqueId = 0;
    int deprecatedUid = 0;
    bool isInstrument = true;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    std::string pluginStateFile;

    bool isValid() const noexcept
    {
        return ! fileOrIdentifier.empty() || ! uniqueIdentifier.empty();
    }
};

bool operator== (const TrackInstrumentReference& lhs, const TrackInstrumentReference& rhs) noexcept;
bool operator!= (const TrackInstrumentReference& lhs, const TrackInstrumentReference& rhs) noexcept;
}
