#pragma once

#include "core/sequencing/MidiClip.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/time/TempoMap.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
struct ReleaseGhostNote
{
    std::string noteId;
    Region noteRegion { time::TickPosition {}, time::TickPosition {} };
    Region ghostRegion { time::TickPosition {}, time::TickPosition {} };
    Region phraseRegion { time::TickPosition {}, time::TickPosition {} };
    time::TickDuration releaseDuration {};
};

time::TickDuration releaseSecondsToTicksAt (const time::TempoMap& tempoMap,
                                            time::TickPosition projectNoteEnd,
                                            double releaseSeconds);

std::optional<double> firstPartyReleaseSecondsForTrack (const Track& track);

std::optional<time::TickDuration> firstPartyReleaseDurationForTrackAt (const Project& project,
                                                                       const Track& track,
                                                                       time::TickPosition projectNoteEnd);

std::vector<ReleaseGhostNote> computeReleaseGhostNotes (const Project& project,
                                                        const Track& track,
                                                        const MidiClip& clip);
}
