#pragma once

#include "core/time/Tick.h"
#include "core/time/Tempo.h"
#include "core/time/TimeSignature.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tsq::core::midi
{
struct MidiExportOptions
{
    int channel = 0;
    time::Tempo tempo {};
    time::TimeSignature timeSignature {};
    bool includeTempoEvent = true;
    bool includeTimeSignatureEvent = true;
    bool renderExpressionMidiCcRoutes = false;
    time::TickDuration expressionRenderStep = time::sixteenthNoteDuration();
};

struct MidiExportReport
{
    std::vector<std::string> warnings;

    bool hasWarnings() const noexcept { return ! warnings.empty(); }
};
}
