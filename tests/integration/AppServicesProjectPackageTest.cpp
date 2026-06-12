#include "app/AppServices.h"
#include "core/midi/MidiFileWriter.h"
#include "core/serialization/ProjectPackage.h"
#include "core/sequencing/DeviceChain.h"
#include "engine/plugins/PluginDescription.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
using namespace tsq;

std::filesystem::path uniquePackagePath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerAppServicesPackageTest-" + std::to_string (stamp) + ".tseq");
}

core::sequencing::TrackInstrumentReference fakeInstrumentReference()
{
    return core::sequencing::TrackInstrumentReference {
        "test-instrument",
        "TheorySequencer",
        "VST3",
        "/tmp/TheorySequencer/Test Instrument.vst3",
        "vst3:test-instrument",
        3001,
        0,
        true,
        0,
        2,
        {}
    };
}

core::sequencing::PluginReference fakePluginReference (std::string name,
                                                       std::string fileOrIdentifier,
                                                       std::string uniqueIdentifier,
                                                       int uniqueId,
                                                       int numInputChannels,
                                                       int numOutputChannels)
{
    return core::sequencing::PluginReference {
        std::move (name),
        "TheorySequencer",
        "VST3",
        std::move (fileOrIdentifier),
        std::move (uniqueIdentifier),
        uniqueId,
        0,
        numInputChannels,
        numOutputChannels
    };
}

engine::plugins::PluginDescription fakePluginDescription (std::string name, bool instrument, bool audioEffect)
{
    engine::plugins::PluginDescription plugin;
    plugin.name = std::move (name);
    plugin.manufacturer = "TheorySequencer";
    plugin.format = "VST3";
    plugin.fileOrIdentifier = "/tmp/TheorySequencer/" + plugin.name + ".vst3";
    plugin.uniqueIdentifier = "vst3:" + plugin.name;
    plugin.uniqueId = instrument ? 4001 : 4002;
    plugin.isInstrument = instrument;
    plugin.isAudioEffect = audioEffect;
    plugin.numInputChannels = audioEffect ? 2 : 0;
    plugin.numOutputChannels = (instrument || audioEffect) ? 2 : 0;
    engine::plugins::normalizePluginMetadata (plugin);
    return plugin;
}

std::filesystem::path uniqueImportPath (std::string extension)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerImportTest-" + std::to_string (stamp) + extension);
}
}

TEST_CASE ("AppServices saves separate plugin state placeholders for device-chain slots", "[integration][package][plugin-state]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    auto* editableTrack = services.project().findTrackById ("track-1");
    REQUIRE (editableTrack != nullptr);
    editableTrack->setInstrument (fakeInstrumentReference());

    core::sequencing::DeviceChain chain;
    chain.appendSlot (core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { "instrument" },
        core::sequencing::PluginReference::fromTrackInstrumentReference (*editableTrack->instrument()),
        core::sequencing::PluginKind::instrument
    });
    chain.appendSlot (core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { "delay" },
        fakePluginReference ("delay",
                             "/tmp/TheorySequencer/Delay.vst3",
                             "vst3:test-delay",
                             3002,
                             2,
                             2),
        core::sequencing::PluginKind::audioEffect
    });
    editableTrack->setDeviceChain (std::move (chain));

    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);

    REQUIRE (services.saveProjectAs (packagePath));

    const auto loaded = core::serialization::ProjectPackage::load (packagePath);
    const auto* track = loaded.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    REQUIRE (track->instrument().has_value());
    CHECK (track->instrument()->pluginStateFile == "plugin-states/track-1.vststate");

    REQUIRE (track->deviceChain().slots().size() == 2);
    CHECK (track->deviceChain().slots()[0].id() == core::sequencing::DeviceSlotId { "instrument" });
    CHECK (track->deviceChain().slots()[0].pluginStateFile() == "plugin-states/track-1--instrument.vststate");
    CHECK (track->deviceChain().slots()[1].id() == core::sequencing::DeviceSlotId { "delay" });
    CHECK (track->deviceChain().slots()[1].pluginStateFile() == "plugin-states/track-1--delay.vststate");

    CHECK (std::filesystem::is_regular_file (packagePath / "plugin-states" / "track-1.vststate"));
    CHECK (std::filesystem::is_regular_file (packagePath / "plugin-states" / "track-1--instrument.vststate"));
    CHECK (std::filesystem::is_regular_file (packagePath / "plugin-states" / "track-1--delay.vststate"));

    std::filesystem::remove_all (packagePath);
}

TEST_CASE ("AppServices inserts typed tracks and drag-created tracks transactionally", "[integration][track-create]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    const auto initialTrackCount = services.project().tracks().size();
    REQUIRE (services.selectedTrackId().has_value());
    CHECK (*services.selectedTrackId() == "track-1");

    REQUIRE (services.insertTrack (core::sequencing::TrackType::audio));
    REQUIRE (services.project().tracks().size() == initialTrackCount + 1);
    CHECK (services.project().tracks().back().type() == core::sequencing::TrackType::audio);
    REQUIRE (services.selectedTrackId().has_value());
    CHECK (*services.selectedTrackId() == services.project().tracks().back().id());

    auto synth = fakePluginDescription ("Prompt08Synth", true, false);
    REQUIRE (services.createTrackFromPlugin (synth));
    REQUIRE (services.project().tracks().size() == initialTrackCount + 2);
    const auto& synthTrack = services.project().tracks().back();
    CHECK (synthTrack.type() == core::sequencing::TrackType::midi);
    REQUIRE (synthTrack.instrument().has_value());
    CHECK (synthTrack.instrument()->pluginName == "Prompt08Synth");
    REQUIRE (synthTrack.deviceChain().slots().size() == 1);
    CHECK (synthTrack.deviceChain().slots().front().kind() == core::sequencing::PluginKind::instrument);
    const auto synthTrackId = synthTrack.id();

    REQUIRE (services.commandStack().undo().succeeded());
    CHECK (services.project().findTrackById (synthTrackId) == nullptr);

    auto effect = fakePluginDescription ("Prompt08Delay", false, true);
    REQUIRE (services.createTrackFromPlugin (effect));
    REQUIRE (services.project().tracks().size() == initialTrackCount + 2);
    const auto& effectTrack = services.project().tracks().back();
    CHECK (effectTrack.type() == core::sequencing::TrackType::audio);
    REQUIRE (effectTrack.deviceChain().slots().size() == 1);
    CHECK (effectTrack.deviceChain().slots().front().kind() == core::sequencing::PluginKind::audioEffect);

    auto unsupported = fakePluginDescription ("Prompt08Unsupported", false, false);
    const auto beforeInvalidCount = services.project().tracks().size();
    CHECK_FALSE (services.createTrackFromPlugin (unsupported));
    CHECK (services.project().tracks().size() == beforeInvalidCount);

    const auto unsupportedAudioPath = uniqueImportPath (".txt");
    {
        std::ofstream stream { unsupportedAudioPath, std::ios::binary };
        stream << "not audio";
    }

    CHECK_FALSE (services.createAudioTrackFromFile (unsupportedAudioPath, "Unsupported Audio"));
    CHECK (services.project().tracks().size() == beforeInvalidCount);

    const auto audioPath = uniqueImportPath (".wav");
    {
        std::ofstream stream { audioPath, std::ios::binary };
        stream << "placeholder";
    }

    REQUIRE (services.createAudioTrackFromFile (audioPath, "Imported Audio"));
    REQUIRE (services.project().tracks().size() == beforeInvalidCount + 1);
    const auto& audioTrack = services.project().tracks().back();
    CHECK (audioTrack.type() == core::sequencing::TrackType::audio);
    REQUIRE (audioTrack.audioClips().size() == 1);
    CHECK (audioTrack.audioClips().front().source().filePath == audioPath.string());

    const auto midiPath = uniqueImportPath (".mid");
    {
        std::ofstream stream { midiPath, std::ios::binary };
        stream << "MThd";
    }

    const auto beforeMidiImportCount = services.project().tracks().size();
    CHECK_FALSE (services.createMidiTrackFromFile (midiPath, "Imported MIDI"));
    CHECK (services.project().tracks().size() == beforeMidiImportCount);

    const auto validMidiPath = uniqueImportPath (".mid");
    const auto midiBytes = core::midi::MidiFileWriter::writeFormat0 (
        960,
        {
            core::midi::MidiFileEvent { 0, 0, { 0x90, 60, 100 } },
            core::midi::MidiFileEvent { 960, 0, { 0x80, 60, 0 } },
        });
    {
        std::ofstream stream { validMidiPath, std::ios::binary };
        stream.write (reinterpret_cast<const char*> (midiBytes.data()), static_cast<std::streamsize> (midiBytes.size()));
    }

    REQUIRE (services.createMidiTrackFromFile (validMidiPath, "Imported MIDI"));
    REQUIRE (services.project().tracks().size() == beforeMidiImportCount + 1);
    const auto& midiTrack = services.project().tracks().back();
    CHECK (midiTrack.type() == core::sequencing::TrackType::midi);
    REQUIRE (midiTrack.clips().size() == 1);
    CHECK (midiTrack.clips().front().name() == "Imported MIDI");
    REQUIRE (midiTrack.clips().front().notes().size() == 1);
    CHECK (midiTrack.clips().front().notes().front().pitch().value() == 60);
    REQUIRE (services.selectedTrackId().has_value());
    CHECK (*services.selectedTrackId() == midiTrack.id());

    std::filesystem::remove (audioPath);
    std::filesystem::remove (unsupportedAudioPath);
    std::filesystem::remove (midiPath);
    std::filesystem::remove (validMidiPath);
}

TEST_CASE ("AppServices imported media survives project package round trip", "[integration][package][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;

    const auto audioPath = uniqueImportPath (".wav");
    {
        std::ofstream stream { audioPath, std::ios::binary };
        stream << "placeholder audio";
    }

    REQUIRE (services.createAudioTrackFromFile (audioPath, "Round Trip Audio"));
    const auto audioTrackId = services.project().tracks().back().id();

    const auto midiPath = uniqueImportPath (".mid");
    const auto midiBytes = core::midi::MidiFileWriter::writeFormat0 (
        960,
        {
            core::midi::MidiFileEvent { 0, 0, { 0x90, 60, 100 } },
            core::midi::MidiFileEvent { 480, 0, { 0x80, 60, 0 } },
            core::midi::MidiFileEvent { 960, 0, { 0x90, 64, 96 } },
            core::midi::MidiFileEvent { 1440, 0, { 0x80, 64, 0 } },
        });
    {
        std::ofstream stream { midiPath, std::ios::binary };
        stream.write (reinterpret_cast<const char*> (midiBytes.data()), static_cast<std::streamsize> (midiBytes.size()));
    }

    REQUIRE (services.createMidiTrackFromFile (midiPath, "Round Trip MIDI"));
    const auto midiTrackId = services.project().tracks().back().id();

    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);

    REQUIRE (services.saveProjectAs (packagePath));

    const auto loadResult = core::serialization::ProjectPackage::loadWithWarnings (packagePath);
    const auto* loadedAudioTrack = loadResult.project.findTrackById (audioTrackId);
    REQUIRE (loadedAudioTrack != nullptr);
    CHECK (loadedAudioTrack->type() == core::sequencing::TrackType::audio);
    REQUIRE (loadedAudioTrack->audioClips().size() == 1);
    const auto& loadedAudioClip = loadedAudioTrack->audioClips().front();
    CHECK (loadedAudioClip.name() == "Round Trip Audio");
    CHECK (loadedAudioClip.source().filePath == audioPath.string());
    CHECK (loadedAudioClip.source().displayName == "Round Trip Audio");
    CHECK_FALSE (loadedAudioClip.source().embeddedInProject);

    const auto* loadedMidiTrack = loadResult.project.findTrackById (midiTrackId);
    REQUIRE (loadedMidiTrack != nullptr);
    CHECK (loadedMidiTrack->type() == core::sequencing::TrackType::midi);
    REQUIRE (loadedMidiTrack->clips().size() == 1);
    const auto& loadedMidiClip = loadedMidiTrack->clips().front();
    CHECK (loadedMidiClip.name() == "Round Trip MIDI");
    REQUIRE (loadedMidiClip.notes().size() == 2);
    CHECK (loadedMidiClip.notes()[0].pitch().value() == 60);
    CHECK (loadedMidiClip.notes()[0].startInClip() == core::time::TickPosition {});
    CHECK (loadedMidiClip.notes()[0].duration() == core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2));
    CHECK (loadedMidiClip.notes()[0].velocity() == 100);
    CHECK (loadedMidiClip.notes()[1].pitch().value() == 64);
    CHECK (loadedMidiClip.notes()[1].startInClip() == core::time::TickPosition::fromTicks (core::time::ticksPerQuarterNote));
    CHECK (loadedMidiClip.notes()[1].duration() == core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2));
    CHECK (loadedMidiClip.notes()[1].velocity() == 96);
    CHECK (loadResult.warnings.empty());

    std::filesystem::remove_all (packagePath);
    std::filesystem::remove (audioPath);
    std::filesystem::remove (midiPath);
}
