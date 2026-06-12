#include "core/music_theory/ChordQuality.h"

namespace tsq::core::music_theory
{
std::string chordQualityName (ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::major: return "Major";
        case ChordQuality::minor: return "Minor";
        case ChordQuality::diminished: return "Diminished";
        case ChordQuality::augmented: return "Augmented";
        case ChordQuality::suspendedSecond: return "Suspended Second";
        case ChordQuality::suspendedFourth: return "Suspended Fourth";
        case ChordQuality::power: return "Power";
        case ChordQuality::majorSeventh: return "Major Seventh";
        case ChordQuality::dominantSeventh: return "Dominant Seventh";
        case ChordQuality::minorSeventh: return "Minor Seventh";
        case ChordQuality::halfDiminishedSeventh: return "Half-Diminished Seventh";
        case ChordQuality::diminishedSeventh: return "Diminished Seventh";
        case ChordQuality::minorMajorSeventh: return "Minor-Major Seventh";
        case ChordQuality::addNine: return "Add Nine";
    }

    return "Unknown";
}

std::string chordQualitySuffix (ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::major: return "";
        case ChordQuality::minor: return "m";
        case ChordQuality::diminished: return "dim";
        case ChordQuality::augmented: return "aug";
        case ChordQuality::suspendedSecond: return "sus2";
        case ChordQuality::suspendedFourth: return "sus4";
        case ChordQuality::power: return "5";
        case ChordQuality::majorSeventh: return "maj7";
        case ChordQuality::dominantSeventh: return "7";
        case ChordQuality::minorSeventh: return "m7";
        case ChordQuality::halfDiminishedSeventh: return "m7b5";
        case ChordQuality::diminishedSeventh: return "dim7";
        case ChordQuality::minorMajorSeventh: return "m(maj7)";
        case ChordQuality::addNine: return "add9";
    }

    return "";
}

std::vector<int> chordQualityIntervals (ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::major: return { 0, 4, 7 };
        case ChordQuality::minor: return { 0, 3, 7 };
        case ChordQuality::diminished: return { 0, 3, 6 };
        case ChordQuality::augmented: return { 0, 4, 8 };
        case ChordQuality::suspendedSecond: return { 0, 2, 7 };
        case ChordQuality::suspendedFourth: return { 0, 5, 7 };
        case ChordQuality::power: return { 0, 7 };
        case ChordQuality::majorSeventh: return { 0, 4, 7, 11 };
        case ChordQuality::dominantSeventh: return { 0, 4, 7, 10 };
        case ChordQuality::minorSeventh: return { 0, 3, 7, 10 };
        case ChordQuality::halfDiminishedSeventh: return { 0, 3, 6, 10 };
        case ChordQuality::diminishedSeventh: return { 0, 3, 6, 9 };
        case ChordQuality::minorMajorSeventh: return { 0, 3, 7, 11 };
        case ChordQuality::addNine: return { 0, 2, 4, 7 };
    }

    return {};
}
}
