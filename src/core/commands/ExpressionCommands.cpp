#include "core/commands/ExpressionCommands.h"

#include "core/commands/ProjectCommandContext.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

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

sequencing::ExpressionLane& requireLane (sequencing::ExpressionState& expressionState,
                                         const sequencing::ExpressionLaneId& laneId)
{
    auto* lane = expressionState.findLane (laneId);
    if (lane == nullptr)
        throw std::invalid_argument ("Expression state does not contain the requested lane");

    return *lane;
}

void replacePhraseEnvelopeClip (sequencing::ExpressionLane& lane,
                                const std::optional<sequencing::ExpressionClipId>& previousEnvelopeId,
                                const sequencing::PhraseEnvelopeClip& envelope)
{
    if (previousEnvelopeId.has_value() && lane.findPhraseEnvelopeClip (*previousEnvelopeId) != nullptr)
        lane.removePhraseEnvelopeClip (*previousEnvelopeId);

    if (lane.findPhraseEnvelopeClip (envelope.id()) != nullptr)
        lane.removePhraseEnvelopeClip (envelope.id());

    lane.addPhraseEnvelopeClip (envelope);
}

void replaceCyclicClip (sequencing::ExpressionLane& lane,
                        const std::optional<sequencing::ExpressionClipId>& previousCyclicId,
                        const sequencing::CyclicExpressionClip& cyclic)
{
    if (previousCyclicId.has_value())
        if (const auto exists = std::any_of (lane.cyclicClips().begin(), lane.cyclicClips().end(), [&] (const auto& existing)
            {
                return existing.id() == *previousCyclicId;
            }); exists)
        {
            lane.removeCyclicClip (*previousCyclicId);
        }

    if (const auto duplicate = std::any_of (lane.cyclicClips().begin(), lane.cyclicClips().end(), [&] (const auto& existing)
        {
            return existing.id() == cyclic.id();
        }); duplicate)
    {
        lane.removeCyclicClip (cyclic.id());
    }

    lane.addCyclicClip (cyclic);
}

void replacePitchSlurs (sequencing::ExpressionLane& lane, const std::vector<sequencing::PitchSlur>& slurs)
{
    auto orderedSlurs = lane.pitchSlurs();
    for (const auto& slur : slurs)
    {
        auto match = std::find_if (orderedSlurs.begin(), orderedSlurs.end(), [&] (const auto& existing)
        {
            return existing.id() == slur.id();
        });

        if (match == orderedSlurs.end())
            orderedSlurs.push_back (slur);
        else
            *match = slur;
    }

    std::vector<sequencing::ExpressionClipId> existingIds;
    existingIds.reserve (lane.pitchSlurs().size());
    for (const auto& slur : lane.pitchSlurs())
        existingIds.push_back (slur.id());

    for (const auto& slurId : existingIds)
        lane.removePitchSlur (slurId);

    for (const auto& slur : orderedSlurs)
        lane.addPitchSlur (slur);
}

void replaceVibratoExpression (sequencing::ExpressionLane& lane, const sequencing::VibratoExpression& vibrato)
{
    if (lane.findVibratoExpression (vibrato.id()) != nullptr)
        lane.removeVibratoExpression (vibrato.id());

    lane.addVibratoExpression (vibrato);
}

template <typename Mutation>
CommandResult applyExpressionMutation (ProjectCommandContext& context,
                                       const std::string& trackId,
                                       const std::string& clipId,
                                       std::optional<sequencing::ExpressionState>& previousExpressionState,
                                       Mutation mutate)
{
    try
    {
        auto& clip = requireClip (context.project(), trackId, clipId);
        previousExpressionState = clip.expressionState();
        auto nextExpressionState = *previousExpressionState;
        mutate (nextExpressionState);
        clip.setExpressionState (std::move (nextExpressionState));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}
}

ClipExpressionCommand::ClipExpressionCommand (std::string trackId, std::string clipId)
    : trackId_ (std::move (trackId)),
      clipId_ (std::move (clipId))
{
}

sequencing::MidiClip& ClipExpressionCommand::requireClip (ProjectCommandContext& context) const
{
    return commands::requireClip (context.project(), trackId_, clipId_);
}

void ClipExpressionCommand::storePrevious (const sequencing::MidiClip& clip)
{
    previousExpressionState_ = clip.expressionState();
}

CommandResult ClipExpressionCommand::restorePrevious (ProjectCommandContext& context)
{
    if (! previousExpressionState_.has_value())
        return CommandResult::failure ("Expression command has no previous state to restore");

    try
    {
        requireClip (context).setExpressionState (*previousExpressionState_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

SetClipExpressionStateCommand::SetClipExpressionStateCommand (std::string trackId,
                                                              std::string clipId,
                                                              sequencing::ExpressionState expressionState)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      expressionState_ (std::move (expressionState))
{
}

std::string SetClipExpressionStateCommand::name() const { return "Set Clip Expression State"; }

CommandResult SetClipExpressionStateCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& clip = requireClip (context);
        storePrevious (clip);
        clip.setExpressionState (expressionState_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetClipExpressionStateCommand::undo (ProjectCommandContext& context)
{
    return restorePrevious (context);
}

CreateExpressionLaneCommand::CreateExpressionLaneCommand (std::string trackId,
                                                          std::string clipId,
                                                          sequencing::ExpressionLane lane)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      lane_ (std::move (lane))
{
}

std::string CreateExpressionLaneCommand::name() const { return "Create Expression Lane"; }

CommandResult CreateExpressionLaneCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        expressionState.addLane (lane_);
    });
}

CommandResult CreateExpressionLaneCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

RenameExpressionLaneCommand::RenameExpressionLaneCommand (std::string trackId,
                                                          std::string clipId,
                                                          sequencing::ExpressionLaneId laneId,
                                                          std::string nameToUse)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      name_ (std::move (nameToUse))
{
}

std::string RenameExpressionLaneCommand::name() const { return "Rename Expression Lane"; }

CommandResult RenameExpressionLaneCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).rename (name_);
    });
}

CommandResult RenameExpressionLaneCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

SetExpressionLaneEnabledCommand::SetExpressionLaneEnabledCommand (std::string trackId,
                                                                  std::string clipId,
                                                                  sequencing::ExpressionLaneId laneId,
                                                                  bool enabled)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      enabled_ (enabled)
{
}

std::string SetExpressionLaneEnabledCommand::name() const { return "Set Expression Lane Enabled"; }

CommandResult SetExpressionLaneEnabledCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).setEnabled (enabled_);
    });
}

CommandResult SetExpressionLaneEnabledCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

SetExpressionLanePolarityCommand::SetExpressionLanePolarityCommand (std::string trackId,
                                                                    std::string clipId,
                                                                    sequencing::ExpressionLaneId laneId,
                                                                    sequencing::ExpressionLanePolarity polarity)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      polarity_ (polarity)
{
}

std::string SetExpressionLanePolarityCommand::name() const { return "Set Expression Lane Polarity"; }

CommandResult SetExpressionLanePolarityCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).setPolarity (polarity_);
    });
}

CommandResult SetExpressionLanePolarityCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

AddExpressionRouteCommand::AddExpressionRouteCommand (std::string trackId,
                                                      std::string clipId,
                                                      sequencing::ExpressionLaneId laneId,
                                                      sequencing::ExpressionRoute route)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      route_ (std::move (route))
{
}

std::string AddExpressionRouteCommand::name() const { return "Add Expression Route"; }

CommandResult AddExpressionRouteCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).addRoute (route_);
    });
}

CommandResult AddExpressionRouteCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

RemoveExpressionRouteCommand::RemoveExpressionRouteCommand (std::string trackId,
                                                            std::string clipId,
                                                            sequencing::ExpressionLaneId laneId,
                                                            sequencing::ExpressionRouteId routeId)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      routeId_ (std::move (routeId))
{
}

std::string RemoveExpressionRouteCommand::name() const { return "Remove Expression Route"; }

CommandResult RemoveExpressionRouteCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).removeRoute (routeId_);
    });
}

CommandResult RemoveExpressionRouteCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

AddPhraseEnvelopeClipCommand::AddPhraseEnvelopeClipCommand (std::string trackId,
                                                            std::string clipId,
                                                            sequencing::ExpressionLaneId laneId,
                                                            sequencing::PhraseEnvelopeClip envelope)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      envelope_ (std::move (envelope))
{
}

std::string AddPhraseEnvelopeClipCommand::name() const { return "Add Phrase Envelope"; }

CommandResult AddPhraseEnvelopeClipCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).addPhraseEnvelopeClip (envelope_);
    });
}

CommandResult AddPhraseEnvelopeClipCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

ReplacePhraseEnvelopeClipCommand::ReplacePhraseEnvelopeClipCommand (std::string trackId,
                                                                    std::string clipId,
                                                                    sequencing::ExpressionLaneId laneId,
                                                                    std::optional<sequencing::ExpressionClipId> previousEnvelopeId,
                                                                    sequencing::PhraseEnvelopeClip envelope)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      previousEnvelopeId_ (std::move (previousEnvelopeId)),
      envelope_ (std::move (envelope))
{
}

std::string ReplacePhraseEnvelopeClipCommand::name() const { return "Replace Phrase Envelope"; }

CommandResult ReplacePhraseEnvelopeClipCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        replacePhraseEnvelopeClip (requireLane (expressionState, laneId_), previousEnvelopeId_, envelope_);
    });
}

CommandResult ReplacePhraseEnvelopeClipCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

RemovePhraseEnvelopeClipCommand::RemovePhraseEnvelopeClipCommand (std::string trackId,
                                                                  std::string clipId,
                                                                  sequencing::ExpressionLaneId laneId,
                                                                  sequencing::ExpressionClipId envelopeId)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      envelopeId_ (std::move (envelopeId))
{
}

std::string RemovePhraseEnvelopeClipCommand::name() const { return "Remove Phrase Envelope"; }

CommandResult RemovePhraseEnvelopeClipCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).removePhraseEnvelopeClip (envelopeId_);
    });
}

CommandResult RemovePhraseEnvelopeClipCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

AddCyclicExpressionClipCommand::AddCyclicExpressionClipCommand (std::string trackId,
                                                                std::string clipId,
                                                                sequencing::ExpressionLaneId laneId,
                                                                sequencing::CyclicExpressionClip cyclic)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      cyclic_ (std::move (cyclic))
{
}

std::string AddCyclicExpressionClipCommand::name() const { return "Add Cyclic Expression"; }

CommandResult AddCyclicExpressionClipCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).addCyclicClip (cyclic_);
    });
}

CommandResult AddCyclicExpressionClipCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

ReplaceCyclicExpressionClipCommand::ReplaceCyclicExpressionClipCommand (std::string trackId,
                                                                        std::string clipId,
                                                                        sequencing::ExpressionLaneId laneId,
                                                                        std::optional<sequencing::ExpressionClipId> previousCyclicId,
                                                                        sequencing::CyclicExpressionClip cyclic)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      previousCyclicId_ (std::move (previousCyclicId)),
      cyclic_ (std::move (cyclic))
{
}

std::string ReplaceCyclicExpressionClipCommand::name() const { return "Replace Cyclic Expression"; }

CommandResult ReplaceCyclicExpressionClipCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        replaceCyclicClip (requireLane (expressionState, laneId_), previousCyclicId_, cyclic_);
    });
}

CommandResult ReplaceCyclicExpressionClipCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

RemoveCyclicExpressionClipCommand::RemoveCyclicExpressionClipCommand (std::string trackId,
                                                                      std::string clipId,
                                                                      sequencing::ExpressionLaneId laneId,
                                                                      sequencing::ExpressionClipId cyclicId)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      cyclicId_ (std::move (cyclicId))
{
}

std::string RemoveCyclicExpressionClipCommand::name() const { return "Remove Cyclic Expression"; }

CommandResult RemoveCyclicExpressionClipCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).removeCyclicClip (cyclicId_);
    });
}

CommandResult RemoveCyclicExpressionClipCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

AddPitchSlurCommand::AddPitchSlurCommand (std::string trackId,
                                          std::string clipId,
                                          sequencing::ExpressionLaneId laneId,
                                          sequencing::PitchSlur slur)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      slur_ (std::move (slur))
{
}

std::string AddPitchSlurCommand::name() const { return "Add Pitch Slur"; }

CommandResult AddPitchSlurCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).addPitchSlur (slur_);
    });
}

CommandResult AddPitchSlurCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

AddPitchSlursCommand::AddPitchSlursCommand (std::string trackId,
                                            std::string clipId,
                                            sequencing::ExpressionLaneId laneId,
                                            std::vector<sequencing::PitchSlur> slurs)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      slurs_ (std::move (slurs))
{
}

std::string AddPitchSlursCommand::name() const { return "Add Pitch Slurs"; }

CommandResult AddPitchSlursCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        auto& lane = requireLane (expressionState, laneId_);
        for (const auto& slur : slurs_)
            lane.addPitchSlur (slur);
    });
}

CommandResult AddPitchSlursCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

ReplacePitchSlursCommand::ReplacePitchSlursCommand (std::string trackId,
                                                    std::string clipId,
                                                    sequencing::ExpressionLaneId laneId,
                                                    std::vector<sequencing::PitchSlur> slurs)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      slurs_ (std::move (slurs))
{
}

std::string ReplacePitchSlursCommand::name() const { return "Replace Pitch Slurs"; }

CommandResult ReplacePitchSlursCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        replacePitchSlurs (requireLane (expressionState, laneId_), slurs_);
    });
}

CommandResult ReplacePitchSlursCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

RemovePitchSlurCommand::RemovePitchSlurCommand (std::string trackId,
                                                std::string clipId,
                                                sequencing::ExpressionLaneId laneId,
                                                sequencing::ExpressionClipId slurId)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      slurId_ (std::move (slurId))
{
}

std::string RemovePitchSlurCommand::name() const { return "Remove Pitch Slur"; }

CommandResult RemovePitchSlurCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).removePitchSlur (slurId_);
    });
}

CommandResult RemovePitchSlurCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

AddVibratoExpressionCommand::AddVibratoExpressionCommand (std::string trackId,
                                                          std::string clipId,
                                                          sequencing::ExpressionLaneId laneId,
                                                          sequencing::VibratoExpression vibrato)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      vibrato_ (std::move (vibrato))
{
}

std::string AddVibratoExpressionCommand::name() const { return "Add Vibrato Expression"; }

CommandResult AddVibratoExpressionCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).addVibratoExpression (vibrato_);
    });
}

CommandResult AddVibratoExpressionCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

ReplaceVibratoExpressionCommand::ReplaceVibratoExpressionCommand (std::string trackId,
                                                                  std::string clipId,
                                                                  sequencing::ExpressionLaneId laneId,
                                                                  sequencing::VibratoExpression vibrato)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      vibrato_ (std::move (vibrato))
{
}

std::string ReplaceVibratoExpressionCommand::name() const { return "Replace Vibrato Expression"; }

CommandResult ReplaceVibratoExpressionCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        replaceVibratoExpression (requireLane (expressionState, laneId_), vibrato_);
    });
}

CommandResult ReplaceVibratoExpressionCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }

RemoveVibratoExpressionCommand::RemoveVibratoExpressionCommand (std::string trackId,
                                                                std::string clipId,
                                                                sequencing::ExpressionLaneId laneId,
                                                                sequencing::ExpressionClipId vibratoId)
    : ClipExpressionCommand (std::move (trackId), std::move (clipId)),
      laneId_ (std::move (laneId)),
      vibratoId_ (std::move (vibratoId))
{
}

std::string RemoveVibratoExpressionCommand::name() const { return "Remove Vibrato Expression"; }

CommandResult RemoveVibratoExpressionCommand::execute (ProjectCommandContext& context)
{
    return applyExpressionMutation (context, trackId_, clipId_, previousExpressionState_, [&] (auto& expressionState) {
        requireLane (expressionState, laneId_).removeVibratoExpression (vibratoId_);
    });
}

CommandResult RemoveVibratoExpressionCommand::undo (ProjectCommandContext& context) { return restorePrevious (context); }
}
