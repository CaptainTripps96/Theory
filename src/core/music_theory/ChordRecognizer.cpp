#include "core/music_theory/ChordRecognizer.h"

#include "core/music_theory/ChordQuality.h"

#include <algorithm>
#include <array>

namespace tsq::core::music_theory
{
namespace
{
struct CandidateQuality
{
    ChordQuality quality;
    int priority = 0;
};

constexpr std::array<CandidateQuality, 14> candidateQualities {{
    { ChordQuality::addNine, 95 },
    { ChordQuality::majorSeventh, 90 },
    { ChordQuality::dominantSeventh, 90 },
    { ChordQuality::minorSeventh, 90 },
    { ChordQuality::halfDiminishedSeventh, 90 },
    { ChordQuality::diminishedSeventh, 90 },
    { ChordQuality::minorMajorSeventh, 90 },
    { ChordQuality::major, 75 },
    { ChordQuality::minor, 75 },
    { ChordQuality::diminished, 75 },
    { ChordQuality::augmented, 75 },
    { ChordQuality::suspendedSecond, 70 },
    { ChordQuality::suspendedFourth, 70 },
    { ChordQuality::power, 50 }
}};

int positiveModulo (int value, int modulus) noexcept
{
    const auto result = value % modulus;
    return result < 0 ? result + modulus : result;
}

bool containsPitchClass (const std::vector<PitchClass>& pitchClasses, PitchClass pitchClass)
{
    return std::find (pitchClasses.begin(), pitchClasses.end(), pitchClass) != pitchClasses.end();
}

std::vector<PitchClass> uniquePreservingOrder (const std::vector<PitchClass>& pitchClasses)
{
    std::vector<PitchClass> result;
    for (const auto pitchClass : pitchClasses)
    {
        if (! containsPitchClass (result, pitchClass))
            result.push_back (pitchClass);
    }

    return result;
}

std::vector<PitchClass> pitchClassesFromPitches (std::vector<MidiPitch> pitches)
{
    std::sort (pitches.begin(), pitches.end(), [] (const auto lhs, const auto rhs) {
        return lhs.value() < rhs.value();
    });

    std::vector<PitchClass> result;
    result.reserve (pitches.size());
    for (const auto pitch : pitches)
        result.push_back (pitch.pitchClass());

    return uniquePreservingOrder (result);
}

std::vector<int> intervalsFromRoot (const std::vector<PitchClass>& pitchClasses, PitchClass root)
{
    std::vector<int> result;
    result.reserve (pitchClasses.size());

    for (const auto pitchClass : pitchClasses)
        result.push_back (positiveModulo (pitchClass.semitonesFromC() - root.semitonesFromC(), 12));

    std::sort (result.begin(), result.end());
    return result;
}

bool intervalsMatch (const std::vector<int>& intervals, ChordQuality quality)
{
    return intervals == chordQualityIntervals (quality);
}

std::vector<PitchClass> chordTonesFor (PitchClass root, ChordQuality quality)
{
    std::vector<PitchClass> result;
    for (const auto interval : chordQualityIntervals (quality))
        result.push_back (root.transposedBy (interval));

    return result;
}
}

std::optional<Chord> ChordRecognizer::recognize (const std::vector<MidiPitch>& pitches) const
{
    return recognize (pitchClassesFromPitches (pitches));
}

std::optional<Chord> ChordRecognizer::recognize (const std::vector<PitchClass>& pitchClasses) const
{
    const auto uniquePitchClasses = uniquePreservingOrder (pitchClasses);
    if (uniquePitchClasses.size() < 2)
        return std::nullopt;

    std::optional<Chord> bestChord;
    auto bestScore = -1;

    for (std::size_t rootIndex = 0; rootIndex < uniquePitchClasses.size(); ++rootIndex)
    {
        const auto root = uniquePitchClasses[rootIndex];
        const auto intervals = intervalsFromRoot (uniquePitchClasses, root);

        for (const auto candidate : candidateQualities)
        {
            if (! intervalsMatch (intervals, candidate.quality))
                continue;

            const auto bassBonus = rootIndex == 0 ? 20 : 0;
            const auto score = candidate.priority + bassBonus;
            if (score > bestScore)
            {
                bestScore = score;
                bestChord = Chord { root, candidate.quality, chordTonesFor (root, candidate.quality) };
            }
        }
    }

    return bestChord;
}
}
