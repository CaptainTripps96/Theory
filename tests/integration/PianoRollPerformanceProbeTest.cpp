#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/ChordQuality.h"
#include "core/sequencing/ChordRegion.h"
#include "core/sequencing/KeyCenterRegion.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/ScaleModeRegion.h"
#include "ui/PianoRollComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace
{
using namespace tsq;

core::time::TickPosition beat (int beatIndex)
{
    return core::time::TickPosition::fromTicks (core::time::ticksPerQuarterNote * beatIndex);
}

core::time::TickDuration beats (int beatCount)
{
    return core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote * beatCount);
}

core::sequencing::Region beatRegion (int startBeat, int endBeat)
{
    return core::sequencing::Region { beat (startBeat), beat (endBeat) };
}

void addHarmonicRegions (core::sequencing::Project& project)
{
    auto& structure = project.musicalStructure();
    structure.addScaleModeRegion (core::sequencing::ScaleModeRegion { beatRegion (16, 32), "Lydian" });
    structure.addScaleModeRegion (core::sequencing::ScaleModeRegion { beatRegion (32, 48), "Major" });
    structure.addScaleModeRegion (core::sequencing::ScaleModeRegion { beatRegion (48, 64), "Whole Tone" });
    structure.addKeyCenterRegion (core::sequencing::KeyCenterRegion { beatRegion (32, 48), core::music_theory::PitchClass::g() });
    structure.addKeyCenterRegion (core::sequencing::KeyCenterRegion { beatRegion (48, 64), core::music_theory::PitchClass::fromSemitonesFromC (11) });

    structure.addChordRegion (core::sequencing::ChordRegion {
        beatRegion (0, 16),
        core::music_theory::PitchClass::c(),
        core::music_theory::ChordQuality::majorSeventh,
        {
            core::music_theory::PitchClass::c(),
            core::music_theory::PitchClass::e(),
            core::music_theory::PitchClass::g(),
            core::music_theory::PitchClass::fromSemitonesFromC (11),
        },
        "Cmaj7"
    });
    structure.addChordRegion (core::sequencing::ChordRegion {
        beatRegion (16, 32),
        core::music_theory::PitchClass::c(),
        core::music_theory::ChordQuality::majorSeventh,
        {
            core::music_theory::PitchClass::c(),
            core::music_theory::PitchClass::e(),
            core::music_theory::PitchClass::g(),
            core::music_theory::PitchClass::fromSemitonesFromC (11),
        },
        "Cmaj7"
    });
    structure.addChordRegion (core::sequencing::ChordRegion {
        beatRegion (32, 48),
        core::music_theory::PitchClass::g(),
        core::music_theory::ChordQuality::dominantSeventh,
        {
            core::music_theory::PitchClass::g(),
            core::music_theory::PitchClass::fromSemitonesFromC (11),
            core::music_theory::PitchClass::d(),
            core::music_theory::PitchClass::f(),
        },
        "G7"
    });
}

core::sequencing::MidiClip makeDenseClip (std::size_t noteCount)
{
    auto clip = core::sequencing::MidiClip {
        "piano-roll-perf-clip-" + std::to_string (noteCount),
        "Piano Roll Perf Clip",
        core::time::TickPosition {},
        beats (64)
    };

    const auto sixteenth = core::time::ticksPerQuarterNote / 4;
    const auto duration = core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2);
    const auto usableSixteenthSlots = (64 * 4) - 2;

    for (std::size_t index = 0; index < noteCount; ++index)
    {
        const auto startSlot = static_cast<std::int64_t> (index % usableSixteenthSlots);
        const auto octaveBand = static_cast<int> ((index / (64 * 4)) % 4);
        const auto pitch = 48 + octaveBand + static_cast<int> ((index * 5) % 36);

        clip.addNote (core::sequencing::MidiNote {
            "note-" + std::to_string (index + 1),
            core::music_theory::MidiPitch::fromValue (std::clamp (pitch, 36, 96)),
            core::time::TickPosition::fromTicks (startSlot * sixteenth),
            duration,
            100
        });
    }

    return clip;
}

void renderPianoRollWithNotes (app::AppServices& services, std::size_t noteCount)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    const auto clipId = "piano-roll-perf-clip-" + std::to_string (noteCount);
    track->addClip (makeDenseClip (noteCount));

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", clipId);

    juce::Image image { juce::Image::ARGB, 1280, 720, true };
    juce::Graphics graphics { image };

    {
        core::diagnostics::ScopedPerformanceTimer renderTimer {
            "PianoRollPerfProbe::render cold notes=" + std::to_string (noteCount)
        };
        pianoRoll.paintEntireComponent (graphics, true);
    }

    {
        core::diagnostics::ScopedPerformanceTimer renderTimer {
            "PianoRollPerfProbe::render warm notes=" + std::to_string (noteCount)
        };
        pianoRoll.paintEntireComponent (graphics, true);
    }

    CHECK (pianoRoll.hasOpenClip());
}
}

TEST_CASE ("Piano Roll paints dense clips for performance probing", "[integration][piano-roll][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addHarmonicRegions (services.project());

    for (const auto noteCount : std::array<std::size_t, 4> { 10, 100, 500, 2000 })
        DYNAMIC_SECTION ("note count " << noteCount)
        {
            renderPianoRollWithNotes (services, noteCount);
        }
}
