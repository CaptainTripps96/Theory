#include "core/sequencing/AccidentalVisibility.h"

#include "core/music_theory/EnharmonicSpelling.h"

#include <algorithm>
#include <optional>

namespace tsq::core::sequencing
{
namespace
{
bool regionsIntersect (const Region& lhs, const Region& rhs) noexcept
{
    return lhs.intersects (rhs);
}

Region noteRegion (const MidiNote& note)
{
    return Region { note.startInClip(), note.endInClip() };
}

bool samePitchClass (const PitchLaneVisibility& lane, music_theory::PitchClass pitchClass) noexcept
{
    return lane.pitchClass == pitchClass;
}

void sortLanes (std::vector<PitchLaneVisibility>& lanes)
{
    std::stable_sort (lanes.begin(), lanes.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.pitchClass.semitonesFromC() < rhs.pitchClass.semitonesFromC();
    });
}

bool containsLane (const std::vector<PitchLaneVisibility>& lanes, music_theory::PitchClass pitchClass)
{
    return std::any_of (lanes.begin(), lanes.end(), [pitchClass] (const auto& lane) {
        return samePitchClass (lane, pitchClass);
    });
}

void addOrPromoteLane (std::vector<PitchLaneVisibility>& lanes, PitchLaneVisibility lane)
{
    const auto match = std::find_if (lanes.begin(), lanes.end(), [&lane] (const auto& existingLane) {
        return existingLane.pitchClass == lane.pitchClass;
    });

    if (match == lanes.end())
    {
        lanes.push_back (lane);
        return;
    }

    if (match->status == PitchLaneStatus::chromaticReveal && lane.status != PitchLaneStatus::chromaticReveal)
        *match = lane;
}

std::optional<music_theory::NoteName> noteSpellingForLane (const MidiNote& note)
{
    if (note.spelling().has_value() && note.spelling()->pitchClass() == note.pitch().pitchClass())
        return note.spelling();

    return std::nullopt;
}
}

std::vector<PitchLaneVisibility> nativeScaleLanes (const ClipHarmonicMap& harmonicMap,
                                                   Region clipLocalRange,
                                                   const music_theory::ScaleLibrary& scaleLibrary)
{
    std::vector<PitchLaneVisibility> lanes;

    for (const auto& harmonicSegment : harmonicMap.segmentsForRange (clipLocalRange))
    {
        const auto scaleInstance = harmonicSegment.originalContext.scaleInstance (scaleLibrary);
        const auto pitchClasses = scaleInstance.pitchClasses();
        const auto spellings = scaleInstance.visibleNoteSpellings();

        for (std::size_t index = 0; index < pitchClasses.size(); ++index)
        {
            addOrPromoteLane (lanes, PitchLaneVisibility {
                pitchClasses[index],
                spellings[index],
                PitchLaneStatus::nativeScale,
                true,
                false
            });
        }
    }

    sortLanes (lanes);
    return lanes;
}

std::vector<PitchLaneVisibility> usedAccidentalLanes (const MidiClip& clip,
                                                      Region clipLocalRange,
                                                      const music_theory::ScaleLibrary& scaleLibrary)
{
    std::vector<PitchLaneVisibility> lanes;

    for (const auto& harmonicSegment : clip.harmonicMetadata().segmentsForRange (clipLocalRange))
    {
        const auto scaleInstance = harmonicSegment.originalContext.scaleInstance (scaleLibrary);

        for (const auto& note : clip.notes())
        {
            if (! regionsIntersect (noteRegion (note), harmonicSegment.region))
                continue;

            const auto pitchClass = note.pitch().pitchClass();
            if (scaleInstance.contains (pitchClass))
                continue;

            addOrPromoteLane (lanes, PitchLaneVisibility {
                pitchClass,
                noteSpellingForLane (note).value_or (music_theory::spellPitchClass (pitchClass)),
                PitchLaneStatus::usedAccidental,
                true,
                false
            });
        }
    }

    sortLanes (lanes);
    return lanes;
}

std::vector<PitchLaneVisibility> chromaticRevealLanes (const MidiClip& clip,
                                                       Region clipLocalRange,
                                                       const music_theory::ScaleLibrary& scaleLibrary)
{
    auto lanes = nativeScaleLanes (clip.harmonicMetadata(), clipLocalRange, scaleLibrary);

    for (const auto& accidentalLane : usedAccidentalLanes (clip, clipLocalRange, scaleLibrary))
        addOrPromoteLane (lanes, accidentalLane);

    for (auto semitone = 0; semitone < 12; ++semitone)
    {
        const auto pitchClass = music_theory::PitchClass::fromSemitonesFromC (semitone);
        if (containsLane (lanes, pitchClass))
            continue;

        lanes.push_back (PitchLaneVisibility {
            pitchClass,
            music_theory::spellPitchClass (pitchClass),
            PitchLaneStatus::chromaticReveal,
            true,
            true
        });
    }

    sortLanes (lanes);
    return lanes;
}

std::vector<PitchLaneVisibility> visibleLanesForClip (const MidiClip& clip,
                                                      Region clipLocalRange,
                                                      const music_theory::ScaleLibrary& scaleLibrary,
                                                      bool chromaticReveal)
{
    if (chromaticReveal)
        return chromaticRevealLanes (clip, clipLocalRange, scaleLibrary);

    auto lanes = nativeScaleLanes (clip.harmonicMetadata(), clipLocalRange, scaleLibrary);
    for (const auto& accidentalLane : usedAccidentalLanes (clip, clipLocalRange, scaleLibrary))
        addOrPromoteLane (lanes, accidentalLane);

    sortLanes (lanes);
    return lanes;
}
}
