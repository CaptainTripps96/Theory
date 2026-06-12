#include "core/commands/AddClipCommand.h"
#include "core/commands/AddChordRegionCommand.h"
#include "core/commands/AddCustomScaleCommand.h"
#include "core/commands/AddKeyCenterRegionCommand.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/ArpeggiateSelectionCommand.h"
#include "core/commands/AddScaleModeRegionCommand.h"
#include "core/commands/AddTempoNodeCommand.h"
#include "core/commands/AddTimeSignatureMarkerCommand.h"
#include "core/commands/AddTrackCommand.h"
#include "core/commands/AssignTrackInstrumentCommand.h"
#include "core/commands/ChordStackingCommand.h"
#include "core/commands/DeleteChordRegionCommand.h"
#include "core/commands/DeleteClipCommand.h"
#include "core/commands/DeleteKeyCenterRegionCommand.h"
#include "core/commands/DeleteNoteCommand.h"
#include "core/commands/DeleteScaleModeRegionCommand.h"
#include "core/commands/GlobalizeChordProgressionCommand.h"
#include "core/commands/MoveClipCommand.h"
#include "core/commands/MoveNoteCommand.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/commands/ReplaceChordRegionCommand.h"
#include "core/commands/ReplaceKeyCenterRegionCommand.h"
#include "core/commands/ReplaceScaleModeRegionCommand.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/commands/ResizeNoteCommand.h"
#include "core/commands/SetProjectRhythmSettingsCommand.h"
#include "core/commands/TransposeClipCommand.h"
#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/ChordNameFormatter.h"
#include "core/music_theory/ChordRecognizer.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/HarmonicContextResolver.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace tsq::core::commands
{
namespace
{
CommandResult failureFromException (const std::exception& exception)
{
    return CommandResult::failure (exception.what());
}

sequencing::Track& requireTrack (sequencing::Project& project, const std::string& trackId)
{
    auto* track = project.findTrackById (trackId);
    if (track == nullptr)
        throw std::invalid_argument ("Project does not contain the requested track");

    return *track;
}

sequencing::MidiClip& requireClip (sequencing::Project& project, const std::string& trackId, const std::string& clipId)
{
    auto& track = requireTrack (project, trackId);
    auto* clip = track.findClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain the requested clip");

    return *clip;
}

sequencing::MidiNote& requireNote (sequencing::Project& project,
                                   const std::string& trackId,
                                   const std::string& clipId,
                                   const std::string& noteId)
{
    auto& clip = requireClip (project, trackId, clipId);
    auto* note = clip.findNoteById (noteId);
    if (note == nullptr)
        throw std::invalid_argument ("Clip does not contain the requested note");

    return *note;
}

int positiveModulo (int value, int modulus) noexcept
{
    const auto result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int centeredPitchClassDelta (music_theory::PitchClass from, music_theory::PitchClass to) noexcept
{
    auto delta = to.semitonesFromC() - from.semitonesFromC();
    while (delta > 6)
        delta -= 12;
    while (delta < -6)
        delta += 12;
    return delta;
}

int pitchClassIntervalFromRoot (music_theory::PitchClass root, music_theory::PitchClass pitchClass) noexcept
{
    return positiveModulo (pitchClass.semitonesFromC() - root.semitonesFromC(), 12);
}

music_theory::ScaleLibrary scaleLibraryForProject (const sequencing::Project& project);

sequencing::MidiNote noteWithInferredHarmonicInterpretation (const sequencing::Project& project,
                                                             const sequencing::MidiClip& clip,
                                                             const sequencing::MidiNote& note)
{
    if (note.harmonicInterpretation().has_value())
        return note;

    const auto library = scaleLibraryForProject (project);
    const sequencing::HarmonicContextResolver resolver { project.musicalStructure() };
    const auto context = resolver.resolveAt (clip.localToProject (note.startInClip()));
    return note.withHarmonicInterpretation (sequencing::interpretNoteHarmonically (note.pitch(), context, library));
}

struct SelectedNoteGroup
{
    time::TickPosition start {};
    time::TickDuration duration {};
    std::vector<sequencing::MidiNote> notes;
};

struct ChordStackGroup
{
    time::TickPosition start {};
    time::TickPosition end {};
    std::vector<sequencing::MidiNote> notes;
};

music_theory::ScaleLibrary scaleLibraryForProject (const sequencing::Project& project)
{
    auto library = music_theory::ScaleLibrary::createBuiltInLibrary();
    for (const auto& customScale : project.customScales())
    {
        try
        {
            library.addDefinition (customScale);
        }
        catch (const std::exception&)
        {
        }
    }

    return library;
}

void addToGroup (std::vector<SelectedNoteGroup>& groups, sequencing::MidiNote note)
{
    for (auto& group : groups)
    {
        if (group.start == note.startInClip() && group.duration == note.duration())
        {
            group.notes.push_back (std::move (note));
            return;
        }
    }

    groups.push_back (SelectedNoteGroup {
        note.startInClip(),
        note.duration(),
        { std::move (note) }
    });
}

std::vector<SelectedNoteGroup> selectedNoteGroups (const sequencing::MidiClip& clip, const std::vector<std::string>& selectedNoteIds)
{
    std::vector<SelectedNoteGroup> groups;
    for (const auto& noteId : selectedNoteIds)
    {
        const auto* note = clip.findNoteById (noteId);
        if (note != nullptr)
            addToGroup (groups, *note);
    }

    for (auto& group : groups)
    {
        std::stable_sort (group.notes.begin(), group.notes.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.pitch().value() < rhs.pitch().value();
        });
    }

    return groups;
}

std::vector<sequencing::MidiNote> selectedNotesById (const sequencing::MidiClip& clip,
                                                    const std::vector<std::string>& selectedNoteIds)
{
    std::vector<sequencing::MidiNote> result;
    for (const auto& noteId : selectedNoteIds)
    {
        const auto* note = clip.findNoteById (noteId);
        if (note != nullptr)
            result.push_back (*note);
    }

    std::stable_sort (result.begin(), result.end(), [] (const auto& lhs, const auto& rhs) {
        if (lhs.startInClip() == rhs.startInClip())
            return lhs.pitch().value() < rhs.pitch().value();

        return lhs.startInClip() < rhs.startInClip();
    });
    return result;
}

std::vector<sequencing::MidiNote> arpeggiationSourceNotes (const std::vector<sequencing::MidiNote>& selectedNotes)
{
    if (selectedNotes.empty())
        return {};

    const auto blockStart = selectedNotes.front().startInClip();
    const auto blockEnd = selectedNotes.front().endInClip();
    const auto isBlockChord = std::all_of (selectedNotes.begin(), selectedNotes.end(), [blockStart, blockEnd] (const auto& note) {
        return note.startInClip() == blockStart && note.endInClip() == blockEnd;
    });

    auto sourceNotes = selectedNotes;
    if (isBlockChord)
    {
        std::stable_sort (sourceNotes.begin(), sourceNotes.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.pitch().value() < rhs.pitch().value();
        });
        return sourceNotes;
    }

    const auto start = std::min_element (selectedNotes.begin(), selectedNotes.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInClip() < rhs.startInClip();
    })->startInClip();
    const auto end = std::max_element (selectedNotes.begin(), selectedNotes.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.endInClip() < rhs.endInClip();
    })->endInClip();
    const auto duration = end - start;

    std::stable_sort (sourceNotes.begin(), sourceNotes.end(), [] (const auto& lhs, const auto& rhs) {
        if (lhs.pitch().value() == rhs.pitch().value())
            return lhs.startInClip() < rhs.startInClip();

        return lhs.pitch().value() < rhs.pitch().value();
    });

    std::vector<sequencing::MidiNote> uniquePitches;
    for (const auto& note : sourceNotes)
    {
        const auto duplicatePitch = std::any_of (uniquePitches.begin(), uniquePitches.end(), [&note] (const auto& existing) {
            return existing.pitch().value() == note.pitch().value();
        });

        if (! duplicatePitch)
            uniquePitches.push_back (note.withStartInClip (start).withDuration (duration));
    }

    return uniquePitches;
}

std::vector<ChordStackGroup> selectedChordStackGroups (const sequencing::MidiClip& clip,
                                                       const std::vector<std::string>& selectedNoteIds,
                                                       time::TickDuration tolerance)
{
    std::vector<sequencing::MidiNote> selectedNotes;
    for (const auto& noteId : selectedNoteIds)
    {
        const auto* note = clip.findNoteById (noteId);
        if (note != nullptr)
            selectedNotes.push_back (*note);
    }

    std::stable_sort (selectedNotes.begin(), selectedNotes.end(), [] (const auto& lhs, const auto& rhs) {
        if (lhs.startInClip() == rhs.startInClip())
            return lhs.pitch().value() < rhs.pitch().value();

        return lhs.startInClip() < rhs.startInClip();
    });

    std::vector<ChordStackGroup> groups;
    for (const auto& note : selectedNotes)
    {
        if (! groups.empty()
            && note.startInClip().ticks() - groups.back().start.ticks() <= tolerance.ticks())
        {
            groups.back().notes.push_back (note);
            if (note.endInClip() > groups.back().end)
                groups.back().end = note.endInClip();
            continue;
        }

        groups.push_back (ChordStackGroup {
            note.startInClip(),
            note.endInClip(),
            { note }
        });
    }

    return groups;
}

std::string nextGeneratedNoteId (const sequencing::MidiClip& clip, const std::vector<sequencing::MidiNote>& pendingNotes)
{
    auto index = clip.notes().size() + pendingNotes.size() + 1;
    while (true)
    {
        auto id = "note-" + std::to_string (index);
        const auto existsInClip = clip.findNoteById (id) != nullptr;
        const auto existsInPending = std::any_of (pendingNotes.begin(), pendingNotes.end(), [&id] (const auto& note) {
            return note.id() == id;
        });

        if (! existsInClip && ! existsInPending)
            return id;

        ++index;
    }
}

std::optional<time::GridDivisionDefinition> arpeggioSubdivisionById (const sequencing::Project& project,
                                                                     const std::string& subdivisionId)
{
    const auto subdivisions = sequencing::availableArpeggioSubdivisions (project.rhythmSettings());
    const auto match = std::find_if (subdivisions.begin(), subdivisions.end(), [&subdivisionId] (const auto& subdivision) {
        return subdivision.id == subdivisionId;
    });

    if (match == subdivisions.end())
        return std::nullopt;

    return *match;
}

std::vector<std::size_t> arpeggioPatternOrder (std::size_t noteCount, sequencing::ArpeggioPattern pattern)
{
    std::vector<std::size_t> order;
    if (noteCount == 0)
        return order;

    switch (pattern)
    {
        case sequencing::ArpeggioPattern::up:
            for (auto index = std::size_t {}; index < noteCount; ++index)
                order.push_back (index);
            break;

        case sequencing::ArpeggioPattern::down:
            for (auto index = noteCount; index > 0; --index)
                order.push_back (index - 1);
            break;

        case sequencing::ArpeggioPattern::upDown:
            for (auto index = std::size_t {}; index < noteCount; ++index)
                order.push_back (index);
            if (noteCount > 2)
            {
                for (auto index = noteCount - 1; index > 1; --index)
                    order.push_back (index - 1);
            }
            break;

        case sequencing::ArpeggioPattern::downUp:
            for (auto index = noteCount; index > 0; --index)
                order.push_back (index - 1);
            if (noteCount > 2)
            {
                for (auto index = std::size_t { 1 }; index + 1 < noteCount; ++index)
                    order.push_back (index);
            }
            break;

        case sequencing::ArpeggioPattern::outsideIn:
        {
            auto left = std::size_t {};
            auto right = noteCount - 1;
            while (left <= right)
            {
                order.push_back (left);
                if (left != right)
                    order.push_back (right);

                ++left;
                if (right == 0)
                    break;

                --right;
            }
            break;
        }

        case sequencing::ArpeggioPattern::insideOut:
        {
            if ((noteCount % 2) != 0)
                order.push_back (noteCount / 2);

            auto left = (noteCount - 1) / 2;
            auto right = noteCount / 2;
            if ((noteCount % 2) == 0)
            {
                order.push_back (left);
                order.push_back (right);
            }

            while (left > 0 || right + 1 < noteCount)
            {
                if (left > 0)
                    order.push_back (--left);

                if (right + 1 < noteCount)
                    order.push_back (++right);
            }
            break;
        }
    }

    return order;
}

std::vector<sequencing::MidiNote> arpeggiatedReplacementNotes (const sequencing::MidiClip& clip,
                                                               const std::vector<sequencing::MidiNote>& chordNotes,
                                                               time::TickDuration subdivision,
                                                               sequencing::ArpeggioPattern pattern,
                                                               const std::vector<sequencing::MidiNote>& pendingNotes = {})
{
    const auto patternOrder = arpeggioPatternOrder (chordNotes.size(), pattern);
    if (patternOrder.empty())
        return {};

    std::vector<sequencing::MidiNote> result;
    auto pending = pendingNotes;
    const auto startTicks = chordNotes.front().startInClip().ticks();
    const auto endTicks = chordNotes.front().endInClip().ticks();
    const auto subdivisionTicks = std::max<std::int64_t> (1, subdivision.ticks());

    auto step = std::size_t {};
    for (auto tick = startTicks; tick < endTicks; tick += subdivisionTicks)
    {
        const auto& source = chordNotes[patternOrder[step % patternOrder.size()]];
        const auto noteId = step < chordNotes.size()
            ? chordNotes[step].id()
            : nextGeneratedNoteId (clip, pending);
        const auto durationTicks = std::min<std::int64_t> (subdivisionTicks, endTicks - tick);

        result.push_back (sequencing::MidiNote {
            noteId,
            source.pitch(),
            time::TickPosition::fromTicks (tick),
            time::TickDuration::fromTicks (durationTicks),
            source.velocity(),
            source.spelling(),
            source.harmonicInterpretation()
        });
        pending.push_back (result.back());
        ++step;
    }

    return result;
}

std::optional<std::size_t> scaleDegreeIndexForPitch (music_theory::MidiPitch pitch,
                                                     const std::vector<music_theory::PitchClass>& pitchClasses)
{
    for (std::size_t index = 0; index < pitchClasses.size(); ++index)
    {
        if (pitchClasses[index] == pitch.pitchClass())
            return index;
    }

    return std::nullopt;
}

std::optional<sequencing::MidiNote> diatonicThirdAbove (const sequencing::MidiClip& clip,
                                                        const sequencing::MidiNote& source,
                                                        const sequencing::HarmonicContext& harmonicContext,
                                                        const music_theory::ScaleLibrary& scaleLibrary,
                                                        const std::vector<sequencing::MidiNote>& pendingNotes)
{
    const auto scale = harmonicContext.scaleInstance (scaleLibrary);
    const auto pitchClasses = scale.pitchClasses();
    const auto spellings = scale.visibleNoteSpellings();

    if (pitchClasses.empty())
        return std::nullopt;

    const auto sourceIndex = scaleDegreeIndexForPitch (source.pitch(), pitchClasses);
    if (! sourceIndex.has_value())
        return std::nullopt;

    const auto targetAbsoluteIndex = *sourceIndex + 2;
    const auto targetIndex = targetAbsoluteIndex % pitchClasses.size();
    const auto sourceScalePosition = pitchClassIntervalFromRoot (harmonicContext.keyCenter(), source.pitch().pitchClass());
    auto targetScalePosition = pitchClassIntervalFromRoot (harmonicContext.keyCenter(), pitchClasses[targetIndex]);
    targetScalePosition += static_cast<int> (targetAbsoluteIndex / pitchClasses.size()) * 12;
    auto semitones = targetScalePosition - sourceScalePosition;
    while (semitones <= 0)
        semitones += 12;

    return sequencing::MidiNote {
        nextGeneratedNoteId (clip, pendingNotes),
        source.pitch().transposedBy (semitones),
        source.startInClip(),
        source.duration(),
        source.velocity(),
        spellings[targetIndex],
        sequencing::interpretNoteHarmonically (source.pitch().transposedBy (semitones), harmonicContext, scaleLibrary)
    };
}

sequencing::MidiClip chromaticallyTransposedClip (const sequencing::MidiClip& clip,
                                                  const sequencing::HarmonicContext& targetContext)
{
    auto result = clip;

    for (const auto& note : clip.notes())
    {
        const auto sourceContext = clip.harmonicMetadata().contextAt (note.startInClip());
        const auto semitones = centeredPitchClassDelta (sourceContext.keyCenter(), targetContext.keyCenter());
        const auto transposedPitch = note.pitch().transposedBy (semitones);
        const auto spelling = music_theory::spellPitchClass (transposedPitch.pitchClass());
        result.replaceNote (note.id(), note.withPitch (transposedPitch, spelling));
    }

    return result;
}

sequencing::MidiClip scaleDegreeTransposedClip (const sequencing::Project& project,
                                                const sequencing::MidiClip& clip,
                                                const sequencing::HarmonicContext& targetContext)
{
    const auto library = scaleLibraryForProject (project);

    auto result = clip;

    for (const auto& note : clip.notes())
    {
        const auto sourceContext = clip.harmonicMetadata().contextAt (note.startInClip());
        const auto sourceInterpretation = note.harmonicInterpretation().value_or (
            sequencing::interpretNoteHarmonically (note.pitch(), sourceContext, library));
        const auto targetPitch = sequencing::pitchForInterpretation (sourceInterpretation, note.pitch(), targetContext, library);
        const auto spelling = sequencing::spellingForInterpretation (sourceInterpretation, targetPitch, targetContext, library);
        const auto targetInterpretation = sequencing::retargetInterpretation (sourceInterpretation, targetContext, library);
        result.replaceNote (note.id(), note.withPitch (targetPitch, spelling).withHarmonicInterpretation (targetInterpretation));
    }

    return result;
}

sequencing::MidiClip scaleDegreeTransposedSelectedNotesClip (const sequencing::Project& project,
                                                            const sequencing::MidiClip& clip,
                                                            const std::vector<std::string>& noteIds)
{
    if (noteIds.empty())
        throw std::invalid_argument ("Scale-degree transpose requires at least one selected note");

    const auto selectedIds = std::unordered_set<std::string> { noteIds.begin(), noteIds.end() };
    auto foundIds = std::unordered_set<std::string> {};
    const auto library = scaleLibraryForProject (project);
    const sequencing::HarmonicContextResolver resolver { project.musicalStructure() };

    auto result = clip;

    for (const auto& note : clip.notes())
    {
        if (selectedIds.find (note.id()) == selectedIds.end())
            continue;

        foundIds.insert (note.id());
        const auto fallbackSourceContext = clip.harmonicMetadata().contextAt (note.startInClip());
        const auto sourceInterpretation = note.harmonicInterpretation().value_or (
            sequencing::interpretNoteHarmonically (note.pitch(), fallbackSourceContext, library));
        const auto targetContext = resolver.resolveAt (clip.localToProject (note.startInClip()));
        const auto targetPitch = sequencing::pitchForInterpretation (sourceInterpretation, note.pitch(), targetContext, library);
        const auto spelling = sequencing::spellingForInterpretation (sourceInterpretation, targetPitch, targetContext, library);
        const auto targetInterpretation = sequencing::retargetInterpretation (sourceInterpretation, targetContext, library);

        result.replaceNote (note.id(), note.withPitch (targetPitch, spelling).withHarmonicInterpretation (targetInterpretation));
    }

    if (foundIds.size() != selectedIds.size())
        throw std::invalid_argument ("Scale-degree transpose selection contains a note that does not exist");

    return result;
}
}

AddTrackCommand::AddTrackCommand (sequencing::Track track)
    : track_ (std::move (track))
{
}

AddTrackCommand::AddTrackCommand (std::string trackId, std::string trackName, sequencing::TrackType trackType)
    : track_ (std::move (trackId), std::move (trackName), trackType)
{
}

std::string AddTrackCommand::name() const
{
    return "Add Track";
}

CommandResult AddTrackCommand::execute (ProjectCommandContext& context)
{
    try
    {
        context.project().addTrack (track_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddTrackCommand::undo (ProjectCommandContext& context)
{
    try
    {
        context.project().removeTrackById (track_.id());
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddCustomScaleCommand::AddCustomScaleCommand (music_theory::ScaleDefinition scale)
    : scale_ (std::move (scale))
{
}

std::string AddCustomScaleCommand::name() const
{
    return "Add Custom Scale";
}

CommandResult AddCustomScaleCommand::execute (ProjectCommandContext& context)
{
    try
    {
        context.project().addCustomScale (scale_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddCustomScaleCommand::undo (ProjectCommandContext& context)
{
    try
    {
        context.project().removeCustomScaleByName (scale_.name());
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AssignTrackInstrumentCommand::AssignTrackInstrumentCommand (std::string trackId, sequencing::TrackInstrumentReference instrument)
    : trackId_ (std::move (trackId)),
      instrument_ (std::move (instrument))
{
}

std::string AssignTrackInstrumentCommand::name() const
{
    return "Assign Track Instrument";
}

CommandResult AssignTrackInstrumentCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (! previousInstrument_.has_value() && ! hadPreviousInstrument_)
        {
            previousInstrument_ = track.instrument();
            hadPreviousInstrument_ = track.instrument().has_value();
        }

        track.setInstrument (instrument_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AssignTrackInstrumentCommand::undo (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (hadPreviousInstrument_)
            track.setInstrument (*previousInstrument_);
        else
            track.clearInstrument();

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddClipCommand::AddClipCommand (std::string trackId, sequencing::MidiClip clip)
    : trackId_ (std::move (trackId)),
      clip_ (std::move (clip))
{
}

AddClipCommand::AddClipCommand (std::string trackId, sequencing::AudioClip clip)
    : trackId_ (std::move (trackId)),
      clip_ (std::move (clip))
{
}

std::string AddClipCommand::name() const
{
    return std::holds_alternative<sequencing::AudioClip> (clip_) ? "Add Audio Clip" : "Add Clip";
}

CommandResult AddClipCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (auto* midiClip = std::get_if<sequencing::MidiClip> (&clip_))
        {
            if (midiClip->harmonicMetadata().regions().empty())
            {
                const sequencing::HarmonicContextResolver resolver { context.project().musicalStructure() };
                midiClip->snapshotHarmonicContext (resolver);
            }

            track.addClip (*midiClip);
        }
        else
        {
            track.addAudioClip (std::get<sequencing::AudioClip> (clip_));
        }

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddClipCommand::undo (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (const auto* midiClip = std::get_if<sequencing::MidiClip> (&clip_))
            track.removeClipById (midiClip->id());
        else
            track.removeAudioClipById (std::get<sequencing::AudioClip> (clip_).id());

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

MoveClipCommand::MoveClipCommand (std::string trackId, std::string clipId, time::TickPosition newStartInProject)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      newStartInProject_ (newStartInProject)
{
}

std::string MoveClipCommand::name() const
{
    return "Move Clip";
}

CommandResult MoveClipCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (auto* clip = track.findClipById (clipId_))
        {
            previousStartInProject_ = clip->startInProject();
            clipKind_ = TimelineClipKind::midi;
            track.moveClip (clipId_, newStartInProject_);
        }
        else if (auto* audioClip = track.findAudioClipById (clipId_))
        {
            previousStartInProject_ = audioClip->startInProject();
            clipKind_ = TimelineClipKind::audio;
            track.moveAudioClip (clipId_, newStartInProject_);
        }
        else
        {
            throw std::invalid_argument ("Track does not contain the requested clip");
        }

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult MoveClipCommand::undo (ProjectCommandContext& context)
{
    if (! previousStartInProject_.has_value() || ! clipKind_.has_value())
        return CommandResult::failure ("Move Clip cannot be undone before it has executed");

    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (*clipKind_ == TimelineClipKind::audio)
            track.moveAudioClip (clipId_, *previousStartInProject_);
        else
            track.moveClip (clipId_, *previousStartInProject_);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ChromaticTransposeClipCommand::ChromaticTransposeClipCommand (std::string trackId,
                                                              std::string clipId,
                                                              sequencing::HarmonicContext targetContext)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      targetContext_ (std::move (targetContext))
{
}

std::string ChromaticTransposeClipCommand::name() const
{
    return "Chromatic Transpose Clip";
}

CommandResult ChromaticTransposeClipCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        const auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (! originalClip_.has_value())
            originalClip_ = clip;

        if (! transformedClip_.has_value())
            transformedClip_ = chromaticallyTransposedClip (clip, targetContext_);

        track.replaceClip (clipId_, *transformedClip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ChromaticTransposeClipCommand::undo (ProjectCommandContext& context)
{
    if (! originalClip_.has_value())
        return CommandResult::failure ("Chromatic Transpose Clip cannot be undone before it has executed");

    try
    {
        requireTrack (context.project(), trackId_).replaceClip (clipId_, *originalClip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ScaleDegreeTransposeClipCommand::ScaleDegreeTransposeClipCommand (std::string trackId,
                                                                  std::string clipId,
                                                                  sequencing::HarmonicContext targetContext)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      targetContext_ (std::move (targetContext))
{
}

std::string ScaleDegreeTransposeClipCommand::name() const
{
    return "Scale-Degree Transpose Clip";
}

CommandResult ScaleDegreeTransposeClipCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        const auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (! originalClip_.has_value())
            originalClip_ = clip;

        if (! transformedClip_.has_value())
            transformedClip_ = scaleDegreeTransposedClip (context.project(), clip, targetContext_);

        track.replaceClip (clipId_, *transformedClip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ScaleDegreeTransposeClipCommand::undo (ProjectCommandContext& context)
{
    if (! originalClip_.has_value())
        return CommandResult::failure ("Scale-Degree Transpose Clip cannot be undone before it has executed");

    try
    {
        requireTrack (context.project(), trackId_).replaceClip (clipId_, *originalClip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ScaleDegreeTransposeSelectedNotesCommand::ScaleDegreeTransposeSelectedNotesCommand (std::string trackId,
                                                                                  std::string clipId,
                                                                                  std::vector<std::string> noteIds)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      noteIds_ (std::move (noteIds))
{
}

std::string ScaleDegreeTransposeSelectedNotesCommand::name() const
{
    return "Scale-Degree Transpose Selected Notes";
}

CommandResult ScaleDegreeTransposeSelectedNotesCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        const auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (! originalClip_.has_value())
            originalClip_ = clip;

        if (! transformedClip_.has_value())
            transformedClip_ = scaleDegreeTransposedSelectedNotesClip (context.project(), clip, noteIds_);

        track.replaceClip (clipId_, *transformedClip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ScaleDegreeTransposeSelectedNotesCommand::undo (ProjectCommandContext& context)
{
    if (! originalClip_.has_value())
        return CommandResult::failure ("Scale-Degree Transpose Selected Notes cannot be undone before it has executed");

    try
    {
        requireTrack (context.project(), trackId_).replaceClip (clipId_, *originalClip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ResizeClipCommand::ResizeClipCommand (std::string trackId, std::string clipId, time::TickDuration newLength)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      newLength_ (newLength)
{
}

ResizeClipCommand::ResizeClipCommand (std::string trackId,
                                      std::string clipId,
                                      time::TickDuration newLength,
                                      sequencing::ClipLoop newLoop)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      newLength_ (newLength),
      newLoop_ (std::move (newLoop))
{
}

std::string ResizeClipCommand::name() const
{
    return "Resize Clip";
}

CommandResult ResizeClipCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (auto* clip = track.findClipById (clipId_))
        {
            previousLength_ = clip->length();
            previousLoop_ = clip->loop();
            clipKind_ = TimelineClipKind::midi;

            auto resizedClip = newLoop_.has_value() ? clip->withLoop (*newLoop_) : *clip;
            resizedClip = resizedClip.withLength (newLength_);
            track.replaceClip (clipId_, std::move (resizedClip));
        }
        else if (auto* audioClip = track.findAudioClipById (clipId_))
        {
            if (newLoop_.has_value())
                return CommandResult::failure ("Audio clip resizing does not accept MIDI loop metadata");

            previousLength_ = audioClip->length();
            previousLoop_.reset();
            clipKind_ = TimelineClipKind::audio;
            track.resizeAudioClip (clipId_, newLength_);
        }
        else
        {
            throw std::invalid_argument ("Track does not contain the requested clip");
        }

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ResizeClipCommand::undo (ProjectCommandContext& context)
{
    if (! previousLength_.has_value() || ! clipKind_.has_value())
        return CommandResult::failure ("Resize Clip cannot be undone before it has executed");

    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (*clipKind_ == TimelineClipKind::audio)
        {
            track.resizeAudioClip (clipId_, *previousLength_);
        }
        else
        {
            if (! previousLoop_.has_value())
                return CommandResult::failure ("Resize Clip cannot restore missing MIDI loop metadata");

            auto& clip = requireClip (context.project(), trackId_, clipId_);
            auto restoredClip = clip.withLoop (*previousLoop_);
            restoredClip = restoredClip.withLength (*previousLength_);
            track.replaceClip (clipId_, std::move (restoredClip));
        }

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

DeleteClipCommand::DeleteClipCommand (std::string trackId, std::string clipId)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId))
{
}

std::string DeleteClipCommand::name() const
{
    return "Delete Clip";
}

CommandResult DeleteClipCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (track.findClipById (clipId_) != nullptr)
            deletedClip_ = track.removeClipById (clipId_);
        else if (track.findAudioClipById (clipId_) != nullptr)
            deletedClip_ = track.removeAudioClipById (clipId_);
        else
            throw std::invalid_argument ("Track does not contain the requested clip");

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult DeleteClipCommand::undo (ProjectCommandContext& context)
{
    if (! deletedClip_.has_value())
        return CommandResult::failure ("Delete Clip cannot be undone before it has executed");

    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (const auto* midiClip = std::get_if<sequencing::MidiClip> (&*deletedClip_))
            track.addClip (*midiClip);
        else
            track.addAudioClip (std::get<sequencing::AudioClip> (*deletedClip_));

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddNoteCommand::AddNoteCommand (std::string trackId, std::string clipId, sequencing::MidiNote note)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      note_ (std::move (note))
{
}

std::string AddNoteCommand::name() const
{
    return "Add Note";
}

CommandResult AddNoteCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);
        note_ = noteWithInferredHarmonicInterpretation (context.project(), clip, note_);
        clip.addNote (note_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddNoteCommand::undo (ProjectCommandContext& context)
{
    try
    {
        requireClip (context.project(), trackId_, clipId_).removeNoteById (note_.id());
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

MoveNoteCommand::MoveNoteCommand (std::string trackId, std::string clipId, std::string noteId, time::TickPosition newStartInClip)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      noteId_ (std::move (noteId)),
      newStartInClip_ (newStartInClip)
{
}

MoveNoteCommand::MoveNoteCommand (std::string trackId,
                                  std::string clipId,
                                  std::string noteId,
                                  time::TickPosition newStartInClip,
                                  music_theory::MidiPitch newPitch,
                                  std::optional<music_theory::NoteName> newSpelling)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      noteId_ (std::move (noteId)),
      newStartInClip_ (newStartInClip),
      newPitch_ (newPitch),
      newSpelling_ (newSpelling)
{
}

std::string MoveNoteCommand::name() const
{
    return "Move Note";
}

CommandResult MoveNoteCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& note = requireNote (context.project(), trackId_, clipId_, noteId_);
        previousStartInClip_ = note.startInClip();
        previousPitch_ = note.pitch();
        previousSpelling_ = note.spelling();
        previousHarmonicInterpretation_ = note.harmonicInterpretation();

        auto& clip = requireClip (context.project(), trackId_, clipId_);
        auto movedNote = note.withStartInClip (newStartInClip_);
        if (newPitch_.has_value())
        {
            movedNote = movedNote.withPitch (*newPitch_, newSpelling_);
            if (*newPitch_ != note.pitch())
            {
                movedNote = movedNote.withHarmonicInterpretation (std::nullopt);
                movedNote = noteWithInferredHarmonicInterpretation (context.project(), clip, movedNote);
            }
        }

        clip.replaceNote (noteId_, std::move (movedNote));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult MoveNoteCommand::undo (ProjectCommandContext& context)
{
    if (! previousStartInClip_.has_value())
        return CommandResult::failure ("Move Note cannot be undone before it has executed");

    try
    {
        auto& note = requireNote (context.project(), trackId_, clipId_, noteId_);
        auto restoredNote = note.withStartInClip (*previousStartInClip_);
        if (previousPitch_.has_value())
            restoredNote = restoredNote.withPitch (*previousPitch_, previousSpelling_);
        restoredNote = restoredNote.withHarmonicInterpretation (previousHarmonicInterpretation_);

        requireClip (context.project(), trackId_, clipId_).replaceNote (noteId_, std::move (restoredNote));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ResizeNoteCommand::ResizeNoteCommand (std::string trackId, std::string clipId, std::string noteId, time::TickDuration newDuration)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      noteId_ (std::move (noteId)),
      newDuration_ (newDuration)
{
}

ResizeNoteCommand::ResizeNoteCommand (std::string trackId,
                                      std::string clipId,
                                      std::string noteId,
                                      time::TickPosition newStartInClip,
                                      time::TickDuration newDuration)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      noteId_ (std::move (noteId)),
      newStartInClip_ (newStartInClip),
      newDuration_ (newDuration)
{
}

std::string ResizeNoteCommand::name() const
{
    return "Resize Note";
}

CommandResult ResizeNoteCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& note = requireNote (context.project(), trackId_, clipId_, noteId_);
        previousStartInClip_ = note.startInClip();
        previousDuration_ = note.duration();

        auto resizedNote = note.withDuration (newDuration_);
        if (newStartInClip_.has_value())
            resizedNote = resizedNote.withStartInClip (*newStartInClip_);

        requireClip (context.project(), trackId_, clipId_).replaceNote (noteId_, std::move (resizedNote));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ResizeNoteCommand::undo (ProjectCommandContext& context)
{
    if (! previousStartInClip_.has_value() || ! previousDuration_.has_value())
        return CommandResult::failure ("Resize Note cannot be undone before it has executed");

    try
    {
        auto& note = requireNote (context.project(), trackId_, clipId_, noteId_);
        auto restoredNote = note.withStartInClip (*previousStartInClip_).withDuration (*previousDuration_);
        requireClip (context.project(), trackId_, clipId_).replaceNote (noteId_, std::move (restoredNote));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

DeleteNoteCommand::DeleteNoteCommand (std::string trackId, std::string clipId, std::string noteId)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      noteId_ (std::move (noteId))
{
}

std::string DeleteNoteCommand::name() const
{
    return "Delete Note";
}

CommandResult DeleteNoteCommand::execute (ProjectCommandContext& context)
{
    try
    {
        deletedNote_ = requireClip (context.project(), trackId_, clipId_).removeNoteById (noteId_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult DeleteNoteCommand::undo (ProjectCommandContext& context)
{
    if (! deletedNote_.has_value())
        return CommandResult::failure ("Delete Note cannot be undone before it has executed");

    try
    {
        requireClip (context.project(), trackId_, clipId_).addNote (*deletedNote_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

StackDiatonicThirdCommand::StackDiatonicThirdCommand (std::string trackId,
                                                      std::string clipId,
                                                      std::vector<std::string> selectedNoteIds)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      selectedNoteIds_ (std::move (selectedNoteIds))
{
}

std::string StackDiatonicThirdCommand::name() const
{
    return "Stack Diatonic Third";
}

CommandResult StackDiatonicThirdCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (addedNotes_.empty())
        {
            const sequencing::HarmonicContextResolver resolver { context.project().musicalStructure() };
            const auto scaleLibrary = scaleLibraryForProject (context.project());
            const auto groups = selectedNoteGroups (clip, selectedNoteIds_);

            for (const auto& group : groups)
            {
                if (group.notes.empty())
                    continue;

                const auto& source = group.notes.back();
                const auto harmonicContext = resolver.resolveAt (clip.localToProject (source.startInClip()));
                if (auto addedNote = diatonicThirdAbove (clip, source, harmonicContext, scaleLibrary, addedNotes_))
                    addedNotes_.push_back (*addedNote);
            }

            if (addedNotes_.empty())
                return CommandResult::failure ("No diatonic chord tones could be added from the selected notes");

            resultingSelectionNoteIds_ = selectedNoteIds_;
            for (const auto& note : addedNotes_)
            {
                resultingSelectionNoteIds_.push_back (note.id());
                addedNoteIds_.push_back (note.id());
            }
        }

        for (const auto& note : addedNotes_)
            clip.addNote (note);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult StackDiatonicThirdCommand::undo (ProjectCommandContext& context)
{
    if (addedNotes_.empty())
        return CommandResult::failure ("Stack Diatonic Third cannot be undone before it has executed");

    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);
        for (const auto& note : addedNotes_)
            clip.removeNoteById (note.id());

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

const std::vector<std::string>& StackDiatonicThirdCommand::resultingSelectionNoteIds() const noexcept
{
    return resultingSelectionNoteIds_;
}

const std::vector<std::string>& StackDiatonicThirdCommand::addedNoteIds() const noexcept
{
    return addedNoteIds_;
}

RemoveHighestChordToneCommand::RemoveHighestChordToneCommand (std::string trackId,
                                                              std::string clipId,
                                                              std::vector<std::string> selectedNoteIds)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      selectedNoteIds_ (std::move (selectedNoteIds))
{
}

std::string RemoveHighestChordToneCommand::name() const
{
    return "Remove Highest Chord Tone";
}

CommandResult RemoveHighestChordToneCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (removedNotes_.empty())
        {
            const auto groups = selectedNoteGroups (clip, selectedNoteIds_);
            resultingSelectionNoteIds_ = selectedNoteIds_;

            for (const auto& group : groups)
            {
                if (group.notes.size() < 2)
                    continue;

                const auto& highest = group.notes.back();
                removedNotes_.push_back (highest);
                removedNoteIds_.push_back (highest.id());
            }

            if (removedNotes_.empty())
                return CommandResult::failure ("Select at least two chord tones before removing the highest tone");

            resultingSelectionNoteIds_.erase (
                std::remove_if (resultingSelectionNoteIds_.begin(),
                                resultingSelectionNoteIds_.end(),
                                [this] (const auto& noteId)
                                {
                                    return std::find (removedNoteIds_.begin(), removedNoteIds_.end(), noteId) != removedNoteIds_.end();
                                }),
                resultingSelectionNoteIds_.end());
        }

        for (const auto& note : removedNotes_)
            clip.removeNoteById (note.id());

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult RemoveHighestChordToneCommand::undo (ProjectCommandContext& context)
{
    if (removedNotes_.empty())
        return CommandResult::failure ("Remove Highest Chord Tone cannot be undone before it has executed");

    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);
        for (const auto& note : removedNotes_)
            clip.addNote (note);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

const std::vector<std::string>& RemoveHighestChordToneCommand::resultingSelectionNoteIds() const noexcept
{
    return resultingSelectionNoteIds_;
}

const std::vector<std::string>& RemoveHighestChordToneCommand::removedNoteIds() const noexcept
{
    return removedNoteIds_;
}

InvertChordCommand::InvertChordCommand (std::string trackId,
                                        std::string clipId,
                                        std::vector<std::string> selectedNoteIds,
                                        Direction direction)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      selectedNoteIds_ (std::move (selectedNoteIds)),
      direction_ (direction)
{
}

std::string InvertChordCommand::name() const
{
    return direction_ == Direction::upward ? "Invert Chord Up" : "Invert Chord Down";
}

CommandResult InvertChordCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (replacementNotes_.empty())
        {
            const auto groups = selectedNoteGroups (clip, selectedNoteIds_);
            std::vector<sequencing::MidiNote> previousNotes;
            std::vector<sequencing::MidiNote> replacementNotes;
            auto selectedNoteCount = std::size_t {};

            for (const auto& group : groups)
                selectedNoteCount += group.notes.size();

            if (selectedNoteCount == 0)
                return CommandResult::failure ("Select notes before inverting or moving by octave");

            for (const auto& group : groups)
            {
                if (group.notes.empty())
                    continue;

                if (selectedNoteCount > 1 && group.notes.size() < 2)
                    continue;

                const auto& source = direction_ == Direction::upward ? group.notes.front() : group.notes.back();
                const auto semitones = direction_ == Direction::upward ? 12 : -12;
                auto replacement = source.withPitch (source.pitch().transposedBy (semitones), source.spelling());
                previousNotes.push_back (source);
                replacementNotes.push_back (std::move (replacement));
            }

            if (replacementNotes.empty())
                return CommandResult::failure ("Select one note or at least one vertical chord stack before inverting");

            previousNotes_ = std::move (previousNotes);
            replacementNotes_ = std::move (replacementNotes);
        }

        for (const auto& note : replacementNotes_)
            clip.replaceNote (note.id(), note);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult InvertChordCommand::undo (ProjectCommandContext& context)
{
    if (previousNotes_.empty())
        return CommandResult::failure ("Invert Chord cannot be undone before it has executed");

    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);
        for (const auto& note : previousNotes_)
            clip.replaceNote (note.id(), note);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ArpeggiateSelectionCommand::ArpeggiateSelectionCommand (std::string trackId,
                                                        std::string clipId,
                                                        std::vector<std::string> selectedNoteIds,
                                                        std::string subdivisionId,
                                                        sequencing::ArpeggioPattern pattern)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      selectedNoteIds_ (std::move (selectedNoteIds)),
      subdivisionId_ (std::move (subdivisionId)),
      pattern_ (pattern)
{
}

std::string ArpeggiateSelectionCommand::name() const
{
    return "Arpeggiate Selection";
}

CommandResult ArpeggiateSelectionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (replacementNotes_.empty())
        {
            const auto subdivision = arpeggioSubdivisionById (context.project(), subdivisionId_);
            if (! subdivision.has_value())
                return CommandResult::failure ("Requested arpeggio subdivision is not enabled in project rhythm settings");

            previousNotes_ = selectedNotesById (clip, selectedNoteIds_);
            if (previousNotes_.empty())
                return CommandResult::failure ("Select a block chord before arpeggiating");

            constexpr auto chordStackToleranceTicks = std::int64_t { 15 };
            const auto groups = selectedChordStackGroups (
                clip,
                selectedNoteIds_,
                time::TickDuration::fromTicks (chordStackToleranceTicks));

            for (const auto& group : groups)
            {
                const auto sourceNotes = arpeggiationSourceNotes (group.notes);
                if (sourceNotes.size() < 2)
                    continue;

                auto groupReplacementNotes = arpeggiatedReplacementNotes (clip,
                                                                           sourceNotes,
                                                                           subdivision->tickDuration,
                                                                           pattern_,
                                                                           replacementNotes_);
                replacementNotes_.insert (replacementNotes_.end(),
                                          groupReplacementNotes.begin(),
                                          groupReplacementNotes.end());
            }

            if (replacementNotes_.empty())
                return CommandResult::failure ("Select at least one block chord before arpeggiating");

            resultingSelectionNoteIds_.clear();
            for (const auto& note : replacementNotes_)
                resultingSelectionNoteIds_.push_back (note.id());
        }

        for (const auto& note : previousNotes_)
            clip.removeNoteById (note.id());

        for (const auto& note : replacementNotes_)
            clip.addNote (note);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ArpeggiateSelectionCommand::undo (ProjectCommandContext& context)
{
    if (previousNotes_.empty() || replacementNotes_.empty())
        return CommandResult::failure ("Arpeggiate Selection cannot be undone before it has executed");

    try
    {
        auto& clip = requireClip (context.project(), trackId_, clipId_);
        for (const auto& note : replacementNotes_)
            clip.removeNoteById (note.id());

        for (const auto& note : previousNotes_)
            clip.addNote (note);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

const std::vector<std::string>& ArpeggiateSelectionCommand::resultingSelectionNoteIds() const noexcept
{
    return resultingSelectionNoteIds_;
}

GlobalizeChordProgressionCommand::GlobalizeChordProgressionCommand (std::string trackId,
                                                                    std::string clipId,
                                                                    std::vector<std::string> selectedNoteIds)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId)),
      selectedNoteIds_ (std::move (selectedNoteIds))
{
}

std::string GlobalizeChordProgressionCommand::name() const
{
    return "Globalize Chord Progression";
}

CommandResult GlobalizeChordProgressionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        const auto& clip = requireClip (context.project(), trackId_, clipId_);

        if (addedRegions_.empty())
        {
            constexpr auto chordStackToleranceTicks = std::int64_t { 15 };
            const auto groups = selectedChordStackGroups (
                clip,
                selectedNoteIds_,
                time::TickDuration::fromTicks (chordStackToleranceTicks));

            struct DetectedRegion
            {
                time::TickPosition start {};
                time::TickPosition end {};
                music_theory::Chord chord { music_theory::PitchClass::c(), music_theory::ChordQuality::major, { music_theory::PitchClass::c() } };
                std::string name;
            };

            const music_theory::ChordRecognizer recognizer;
            const music_theory::ChordNameFormatter formatter;
            std::vector<DetectedRegion> detectedRegions;

            for (const auto& group : groups)
            {
                std::vector<music_theory::MidiPitch> pitches;
                pitches.reserve (group.notes.size());
                for (const auto& note : group.notes)
                    pitches.push_back (note.pitch());

                const auto chord = recognizer.recognize (pitches);
                if (! chord.has_value())
                    continue;

                detectedRegions.push_back (DetectedRegion {
                    group.start,
                    group.end,
                    *chord,
                    formatter.format (*chord, music_theory::SpellingPreference::preferFlats)
                });
            }

            if (detectedRegions.empty())
                return CommandResult::failure ("No recognizable chord stacks were found in the selected notes");

            for (std::size_t index = 0; index < detectedRegions.size(); ++index)
            {
                const auto& detected = detectedRegions[index];
                const auto regionEnd = index + 1 < detectedRegions.size()
                    ? detectedRegions[index + 1].start
                    : detected.end;

                if (regionEnd <= detected.start)
                    continue;

                addedRegions_.push_back (sequencing::ChordRegion {
                    sequencing::Region { clip.localToProject (detected.start), clip.localToProject (regionEnd) },
                    detected.chord.root(),
                    detected.chord.quality(),
                    detected.chord.pitchClasses(),
                    detected.name
                });
            }

            if (addedRegions_.empty())
                return CommandResult::failure ("Recognized chord stacks did not produce any valid chord regions");
        }

        for (const auto& region : addedRegions_)
            context.project().musicalStructure().addChordRegion (region);

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult GlobalizeChordProgressionCommand::undo (ProjectCommandContext& context)
{
    if (addedRegions_.empty())
        return CommandResult::failure ("Globalize Chord Progression cannot be undone before it has executed");

    try
    {
        for (const auto& region : addedRegions_)
        {
            if (! context.project().musicalStructure().removeChordRegion (region))
                return CommandResult::failure ("Unable to remove a globalized chord region");
        }

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

const std::vector<sequencing::ChordRegion>& GlobalizeChordProgressionCommand::addedRegions() const noexcept
{
    return addedRegions_;
}

ReplaceChordRegionCommand::ReplaceChordRegionCommand (sequencing::ChordRegion previousRegion, sequencing::ChordRegion newRegion)
    : previousRegion_ (std::move (previousRegion)),
      newRegion_ (std::move (newRegion))
{
}

std::string ReplaceChordRegionCommand::name() const
{
    return "Replace Chord Region";
}

CommandResult ReplaceChordRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        if (! context.project().musicalStructure().removeChordRegion (previousRegion_))
            return CommandResult::failure ("Previous chord region does not exist");

        context.project().musicalStructure().addChordRegion (newRegion_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ReplaceChordRegionCommand::undo (ProjectCommandContext& context)
{
    try
    {
        if (! context.project().musicalStructure().removeChordRegion (newRegion_))
            return CommandResult::failure ("New chord region does not exist");

        context.project().musicalStructure().addChordRegion (previousRegion_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

DeleteChordRegionCommand::DeleteChordRegionCommand (sequencing::ChordRegion region)
    : region_ (std::move (region))
{
}

std::string DeleteChordRegionCommand::name() const
{
    return "Delete Chord Region";
}

CommandResult DeleteChordRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        if (! context.project().musicalStructure().removeChordRegion (region_))
            return CommandResult::failure ("Chord region does not exist");

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult DeleteChordRegionCommand::undo (ProjectCommandContext& context)
{
    try
    {
        context.project().musicalStructure().addChordRegion (region_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddChordRegionCommand::AddChordRegionCommand (sequencing::ChordRegion region)
    : region_ (std::move (region))
{
}

std::string AddChordRegionCommand::name() const
{
    return "Add Chord Region";
}

CommandResult AddChordRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        context.project().musicalStructure().addChordRegion (region_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddChordRegionCommand::undo (ProjectCommandContext& context)
{
    if (! context.project().musicalStructure().removeChordRegion (region_))
        return CommandResult::failure ("Chord region was not present during undo");

    return CommandResult::success();
}

SetProjectRhythmSettingsCommand::SetProjectRhythmSettingsCommand (time::ProjectRhythmSettings newSettings)
    : newSettings_ (std::move (newSettings))
{
}

std::string SetProjectRhythmSettingsCommand::name() const
{
    return "Set Project Rhythm Settings";
}

CommandResult SetProjectRhythmSettingsCommand::execute (ProjectCommandContext& context)
{
    try
    {
        previousSettings_ = context.project().rhythmSettings();
        context.project().rhythmSettings() = newSettings_;
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetProjectRhythmSettingsCommand::undo (ProjectCommandContext& context)
{
    if (! previousSettings_.has_value())
        return CommandResult::failure ("Set Project Rhythm Settings cannot be undone before it has executed");

    context.project().rhythmSettings() = *previousSettings_;
    return CommandResult::success();
}

AddKeyCenterRegionCommand::AddKeyCenterRegionCommand (sequencing::KeyCenterRegion region)
    : region_ (std::move (region))
{
}

std::string AddKeyCenterRegionCommand::name() const
{
    return "Add Key Center Region";
}

CommandResult AddKeyCenterRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        context.project().musicalStructure().addKeyCenterRegion (region_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddKeyCenterRegionCommand::undo (ProjectCommandContext& context)
{
    if (! context.project().musicalStructure().removeKeyCenterRegion (region_))
        return CommandResult::failure ("Key center region was not present during undo");

    return CommandResult::success();
}

DeleteKeyCenterRegionCommand::DeleteKeyCenterRegionCommand (sequencing::KeyCenterRegion region)
    : region_ (std::move (region))
{
}

std::string DeleteKeyCenterRegionCommand::name() const
{
    return "Delete Key Center Region";
}

CommandResult DeleteKeyCenterRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        if (! context.project().musicalStructure().removeKeyCenterRegion (region_))
            return CommandResult::failure ("Key center region does not exist");

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult DeleteKeyCenterRegionCommand::undo (ProjectCommandContext& context)
{
    try
    {
        context.project().musicalStructure().addKeyCenterRegion (region_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddScaleModeRegionCommand::AddScaleModeRegionCommand (sequencing::ScaleModeRegion region)
    : region_ (std::move (region))
{
}

std::string AddScaleModeRegionCommand::name() const
{
    return "Add Scale/Mode Region";
}

CommandResult AddScaleModeRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        context.project().musicalStructure().addScaleModeRegion (region_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddScaleModeRegionCommand::undo (ProjectCommandContext& context)
{
    if (! context.project().musicalStructure().removeScaleModeRegion (region_))
        return CommandResult::failure ("Scale/mode region was not present during undo");

    return CommandResult::success();
}

DeleteScaleModeRegionCommand::DeleteScaleModeRegionCommand (sequencing::ScaleModeRegion region)
    : region_ (std::move (region))
{
}

std::string DeleteScaleModeRegionCommand::name() const
{
    return "Delete Scale/Mode Region";
}

CommandResult DeleteScaleModeRegionCommand::execute (ProjectCommandContext& context)
{
    try
    {
        if (! context.project().musicalStructure().removeScaleModeRegion (region_))
            return CommandResult::failure ("Scale/mode region does not exist");

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult DeleteScaleModeRegionCommand::undo (ProjectCommandContext& context)
{
    try
    {
        context.project().musicalStructure().addScaleModeRegion (region_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddTempoNodeCommand::AddTempoNodeCommand (time::TickPosition position, time::Tempo tempo)
    : position_ (position),
      tempo_ (tempo)
{
}

std::string AddTempoNodeCommand::name() const
{
    return "Add Tempo Node";
}

CommandResult AddTempoNodeCommand::execute (ProjectCommandContext& context)
{
    try
    {
        previousMap_ = context.project().tempoMap();
        context.project().tempoMap().addNode (position_, tempo_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddTempoNodeCommand::undo (ProjectCommandContext& context)
{
    if (! previousMap_.has_value())
        return CommandResult::failure ("Add Tempo Node cannot be undone before it has executed");

    context.project().tempoMap() = *previousMap_;
    return CommandResult::success();
}

AddTimeSignatureMarkerCommand::AddTimeSignatureMarkerCommand (time::TickPosition position, time::TimeSignature timeSignature)
    : position_ (position),
      timeSignature_ (timeSignature)
{
}

std::string AddTimeSignatureMarkerCommand::name() const
{
    return "Add Time Signature Marker";
}

CommandResult AddTimeSignatureMarkerCommand::execute (ProjectCommandContext& context)
{
    try
    {
        previousMap_ = context.project().timeSignatureMap();
        context.project().timeSignatureMap().addMarker (position_, timeSignature_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddTimeSignatureMarkerCommand::undo (ProjectCommandContext& context)
{
    if (! previousMap_.has_value())
        return CommandResult::failure ("Add Time Signature Marker cannot be undone before it has executed");

    context.project().timeSignatureMap() = *previousMap_;
    return CommandResult::success();
}

ReplaceKeyCenterRegionCommand::ReplaceKeyCenterRegionCommand (sequencing::KeyCenterRegion previousRegion,
                                                              sequencing::KeyCenterRegion newRegion)
    : previousRegion_ (std::move (previousRegion)),
      newRegion_ (std::move (newRegion))
{
}

std::string ReplaceKeyCenterRegionCommand::name() const
{
    return "Replace Key Center Region";
}

CommandResult ReplaceKeyCenterRegionCommand::execute (ProjectCommandContext& context)
{
    auto& structure = context.project().musicalStructure();
    if (! structure.removeKeyCenterRegion (previousRegion_))
        return CommandResult::failure ("Key center region was not present during replace");

    try
    {
        structure.addKeyCenterRegion (newRegion_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        structure.addKeyCenterRegion (previousRegion_);
        return failureFromException (exception);
    }
}

CommandResult ReplaceKeyCenterRegionCommand::undo (ProjectCommandContext& context)
{
    auto& structure = context.project().musicalStructure();
    if (! structure.removeKeyCenterRegion (newRegion_))
        return CommandResult::failure ("Replacement key center region was not present during undo");

    try
    {
        structure.addKeyCenterRegion (previousRegion_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        structure.addKeyCenterRegion (newRegion_);
        return failureFromException (exception);
    }
}

ReplaceScaleModeRegionCommand::ReplaceScaleModeRegionCommand (sequencing::ScaleModeRegion previousRegion,
                                                              sequencing::ScaleModeRegion newRegion)
    : previousRegion_ (std::move (previousRegion)),
      newRegion_ (std::move (newRegion))
{
}

std::string ReplaceScaleModeRegionCommand::name() const
{
    return "Replace Scale/Mode Region";
}

CommandResult ReplaceScaleModeRegionCommand::execute (ProjectCommandContext& context)
{
    auto& structure = context.project().musicalStructure();
    if (! structure.removeScaleModeRegion (previousRegion_))
        return CommandResult::failure ("Scale/mode region was not present during replace");

    try
    {
        structure.addScaleModeRegion (newRegion_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        structure.addScaleModeRegion (previousRegion_);
        return failureFromException (exception);
    }
}

CommandResult ReplaceScaleModeRegionCommand::undo (ProjectCommandContext& context)
{
    auto& structure = context.project().musicalStructure();
    if (! structure.removeScaleModeRegion (newRegion_))
        return CommandResult::failure ("Replacement scale/mode region was not present during undo");

    try
    {
        structure.addScaleModeRegion (previousRegion_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        structure.addScaleModeRegion (newRegion_);
        return failureFromException (exception);
    }
}
}
