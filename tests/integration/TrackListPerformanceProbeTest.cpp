#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Routing.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "ui/TrackListComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <string>

namespace
{
using namespace tsq;

core::sequencing::TrackType trackTypeForIndex (int index, int trackCount)
{
    if (trackCount > 1 && index == trackCount - 1)
        return core::sequencing::TrackType::master;

    if (trackCount >= 8 && index > 0 && (index % 8) == 7)
        return core::sequencing::TrackType::returnTrack;

    return (index % 2) == 0 ? core::sequencing::TrackType::midi
                            : core::sequencing::TrackType::audio;
}

core::sequencing::Track makeTrack (int index, int trackCount)
{
    const auto type = trackTypeForIndex (index, trackCount);
    const auto id = type == core::sequencing::TrackType::master
        ? std::string { "master-1" }
        : "track-" + std::to_string (index + 1);
    auto track = core::sequencing::Track {
        id,
        std::string { type == core::sequencing::TrackType::master ? "Master" : "Track " + std::to_string (index + 1) },
        type
    };

    auto mixer = track.mixerStrip();
    mixer.setVolumeDb (std::clamp (-18.0 + static_cast<double> (index % 12), -60.0, 6.0));
    mixer.setPan (((index % 9) - 4) / 4.0);
    mixer.setSoloed ((index % 31) == 0 && type != core::sequencing::TrackType::master);
    track.setMixerStrip (mixer);

    return track;
}

void resetProjectTracks (app::AppServices& services, int trackCount)
{
    auto& project = services.project();
    while (! project.tracks().empty())
        project.removeTrackById (project.tracks().back().id());

    for (int index = 0; index < trackCount; ++index)
        project.addTrack (makeTrack (index, trackCount));

    if (! project.tracks().empty())
        services.setSelectedTrack (project.tracks().front().id());
}

int componentHeightForTrackCount (int trackCount)
{
    return 240 + (trackCount * 96);
}

void probeTrackList (app::AppServices& services, int trackCount)
{
    resetProjectTracks (services, trackCount);

    ui::TrackListComponent trackList { services };
    trackList.setBounds (0, 0, 960, componentHeightForTrackCount (trackCount));

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "TrackListPerfProbe::refresh cold tracks=" + std::to_string (trackCount)
        };
        trackList.refresh();
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "TrackListPerfProbe::refresh warm tracks=" + std::to_string (trackCount)
        };
        trackList.refresh();
    }

    juce::Image image {
        juce::Image::ARGB,
        960,
        componentHeightForTrackCount (trackCount),
        true
    };
    juce::Graphics graphics { image };

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "TrackListPerfProbe::paint tracks=" + std::to_string (trackCount)
        };
        trackList.paintEntireComponent (graphics, true);
    }

    CHECK (trackList.getNumChildComponents() == trackCount);
}
}

TEST_CASE ("Track List refreshes and paints large mixer row counts for performance probing", "[integration][track-list][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    for (const auto trackCount : std::array<int, 4> { 1, 16, 64, 128 })
        DYNAMIC_SECTION ("track count " << trackCount)
        {
            probeTrackList (services, trackCount);
        }
}
