#include "core/sequencing/RecordingInputTransform.h"

#include "core/music_theory/EnharmonicSpelling.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tsq::core::sequencing
{
namespace
{
std::int64_t roundToGrid (std::int64_t ticks, std::int64_t gridTicks) noexcept
{
    const auto safeGrid = std::max<std::int64_t> (1, gridTicks);
    if (ticks < 0)
        return -roundToGrid (-ticks, safeGrid);

    return ((ticks + (safeGrid / 2)) / safeGrid) * safeGrid;
}

bool pitchIsInContext (music_theory::MidiPitch pitch,
                       const HarmonicContext& context,
                       const music_theory::ScaleLibrary& scaleLibrary)
{
    return context.contains (pitch.pitchClass(), scaleLibrary);
}

std::optional<music_theory::MidiPitch> lockedPitchCandidate (music_theory::MidiPitch pitch,
                                                            const HarmonicContext& context,
                                                            const music_theory::ScaleLibrary& scaleLibrary,
                                                            ScaleLockMode mode)
{
    if (mode == ScaleLockMode::off || pitchIsInContext (pitch, context, scaleLibrary))
        return pitch;

    auto bestPitch = std::optional<music_theory::MidiPitch> {};
    auto bestDistance = std::numeric_limits<int>::max();

    for (auto midi = 0; midi <= 127; ++midi)
    {
        auto candidate = music_theory::MidiPitch::fromValue (midi);
        if (! pitchIsInContext (candidate, context, scaleLibrary))
            continue;

        const auto distance = midi - pitch.value();
        if (mode == ScaleLockMode::roundUp && distance < 0)
            continue;

        if (mode == ScaleLockMode::roundDown && distance > 0)
            continue;

        const auto absoluteDistance = std::abs (distance);
        const auto betterNearest = mode == ScaleLockMode::nearest
            && (absoluteDistance < bestDistance
                || (absoluteDistance == bestDistance && bestPitch.has_value() && midi > bestPitch->value()));
        const auto betterDirectional = mode != ScaleLockMode::nearest && absoluteDistance < bestDistance;

        if (! bestPitch.has_value() || betterNearest || betterDirectional)
        {
            bestPitch = candidate;
            bestDistance = absoluteDistance;
        }
    }

    return bestPitch;
}
}

std::string scaleLockModeName (ScaleLockMode mode)
{
    switch (mode)
    {
        case ScaleLockMode::off: return "Off";
        case ScaleLockMode::nearest: return "Nearest";
        case ScaleLockMode::roundUp: return "Round Up";
        case ScaleLockMode::roundDown: return "Round Down";
    }

    return "Off";
}

std::vector<time::GridDivisionDefinition> availableInputQuantizationDivisions (const time::ProjectRhythmSettings& settings)
{
    auto result = time::availableGridDivisions (settings);
    std::stable_sort (result.begin(), result.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.tickDuration.ticks() > rhs.tickDuration.ticks();
    });
    return result;
}

std::optional<time::GridDivisionDefinition> inputQuantizationDivisionById (std::string_view id,
                                                                           const time::ProjectRhythmSettings& settings)
{
    const auto divisions = availableInputQuantizationDivisions (settings);
    const auto match = std::find_if (divisions.begin(), divisions.end(), [id] (const auto& division) {
        return division.id == id;
    });

    if (match == divisions.end())
        return std::nullopt;

    return *match;
}

time::TickPosition quantizeInputStart (time::TickPosition position,
                                       std::string_view gridDivisionId,
                                       const time::ProjectRhythmSettings& settings)
{
    const auto division = inputQuantizationDivisionById (gridDivisionId, settings);
    if (! division.has_value())
        return position;

    return time::TickPosition::fromTicks (roundToGrid (position.ticks(), division->tickDuration.ticks()));
}

music_theory::MidiPitch applyScaleLock (music_theory::MidiPitch pitch,
                                        const HarmonicContext& context,
                                        const music_theory::ScaleLibrary& scaleLibrary,
                                        ScaleLockMode mode)
{
    if (const auto locked = lockedPitchCandidate (pitch, context, scaleLibrary, mode))
        return *locked;

    return pitch;
}

music_theory::NoteName spellingForRecordedPitch (music_theory::MidiPitch pitch,
                                                 const HarmonicContext& context,
                                                 const music_theory::ScaleLibrary& scaleLibrary)
{
    const auto scale = context.scaleInstance (scaleLibrary);
    const auto pitchClasses = scale.pitchClasses();
    const auto spellings = scale.visibleNoteSpellings();

    for (auto index = std::size_t {}; index < pitchClasses.size() && index < spellings.size(); ++index)
    {
        if (pitchClasses[index] == pitch.pitchClass())
            return spellings[index];
    }

    return music_theory::spellPitchClass (pitch.pitchClass());
}
}
