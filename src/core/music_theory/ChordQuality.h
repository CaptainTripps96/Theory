#pragma once

#include <string>
#include <vector>

namespace tsq::core::music_theory
{
enum class ChordQuality
{
    major,
    minor,
    diminished,
    augmented,
    suspendedSecond,
    suspendedFourth,
    power,
    majorSeventh,
    dominantSeventh,
    minorSeventh,
    halfDiminishedSeventh,
    diminishedSeventh,
    minorMajorSeventh,
    addNine
};

std::string chordQualityName (ChordQuality quality);
std::string chordQualitySuffix (ChordQuality quality);
std::vector<int> chordQualityIntervals (ChordQuality quality);
}
