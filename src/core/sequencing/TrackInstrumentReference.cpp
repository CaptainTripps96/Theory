#include "core/sequencing/TrackInstrumentReference.h"

namespace tsq::core::sequencing
{
bool operator== (const TrackInstrumentReference& lhs, const TrackInstrumentReference& rhs) noexcept
{
    return lhs.pluginName == rhs.pluginName
        && lhs.manufacturer == rhs.manufacturer
        && lhs.format == rhs.format
        && lhs.fileOrIdentifier == rhs.fileOrIdentifier
        && lhs.uniqueIdentifier == rhs.uniqueIdentifier
        && lhs.uniqueId == rhs.uniqueId
        && lhs.deprecatedUid == rhs.deprecatedUid
        && lhs.isInstrument == rhs.isInstrument
        && lhs.numInputChannels == rhs.numInputChannels
        && lhs.numOutputChannels == rhs.numOutputChannels
        && lhs.pluginStateFile == rhs.pluginStateFile;
}

bool operator!= (const TrackInstrumentReference& lhs, const TrackInstrumentReference& rhs) noexcept
{
    return ! (lhs == rhs);
}
}
