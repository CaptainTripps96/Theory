#pragma once

#include "core/time/Tempo.h"
#include "core/time/TimeSignature.h"

#include <cstdint>

namespace tsq::core::midi
{
struct MidiExportOptions
{
    int channel = 0;
    time::Tempo tempo {};
    time::TimeSignature timeSignature {};
    bool includeTempoEvent = true;
    bool includeTimeSignatureEvent = true;
};
}
