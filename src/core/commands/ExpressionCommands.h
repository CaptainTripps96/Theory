#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiClip.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class ClipExpressionCommand : public Command
{
public:
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::expression; }

protected:
    ClipExpressionCommand (std::string trackId, std::string clipId);

    sequencing::MidiClip& requireClip (ProjectCommandContext& context) const;
    CommandResult restorePrevious (ProjectCommandContext& context);
    void storePrevious (const sequencing::MidiClip& clip);

    std::string trackId_;
    std::string clipId_;
    std::optional<sequencing::ExpressionState> previousExpressionState_;
};

class SetClipExpressionStateCommand final : public ClipExpressionCommand
{
public:
    SetClipExpressionStateCommand (std::string trackId, std::string clipId, sequencing::ExpressionState expressionState);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionState expressionState_;
};

class CreateExpressionLaneCommand final : public ClipExpressionCommand
{
public:
    CreateExpressionLaneCommand (std::string trackId, std::string clipId, sequencing::ExpressionLane lane);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLane lane_;
};

class RenameExpressionLaneCommand final : public ClipExpressionCommand
{
public:
    RenameExpressionLaneCommand (std::string trackId,
                                 std::string clipId,
                                 sequencing::ExpressionLaneId laneId,
                                 std::string name);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    std::string name_;
};

class SetExpressionLaneEnabledCommand final : public ClipExpressionCommand
{
public:
    SetExpressionLaneEnabledCommand (std::string trackId,
                                     std::string clipId,
                                     sequencing::ExpressionLaneId laneId,
                                     bool enabled);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    bool enabled_ = true;
};

class SetExpressionLanePolarityCommand final : public ClipExpressionCommand
{
public:
    SetExpressionLanePolarityCommand (std::string trackId,
                                      std::string clipId,
                                      sequencing::ExpressionLaneId laneId,
                                      sequencing::ExpressionLanePolarity polarity);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionLanePolarity polarity_ = sequencing::ExpressionLanePolarity::unipolar;
};

class AddExpressionRouteCommand final : public ClipExpressionCommand
{
public:
    AddExpressionRouteCommand (std::string trackId,
                               std::string clipId,
                               sequencing::ExpressionLaneId laneId,
                               sequencing::ExpressionRoute route);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionRoute route_;
};

class RemoveExpressionRouteCommand final : public ClipExpressionCommand
{
public:
    RemoveExpressionRouteCommand (std::string trackId,
                                  std::string clipId,
                                  sequencing::ExpressionLaneId laneId,
                                  sequencing::ExpressionRouteId routeId);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionRouteId routeId_;
};

class AddPhraseEnvelopeClipCommand final : public ClipExpressionCommand
{
public:
    AddPhraseEnvelopeClipCommand (std::string trackId,
                                  std::string clipId,
                                  sequencing::ExpressionLaneId laneId,
                                  sequencing::PhraseEnvelopeClip envelope);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::PhraseEnvelopeClip envelope_;
};

class ReplacePhraseEnvelopeClipCommand final : public ClipExpressionCommand
{
public:
    ReplacePhraseEnvelopeClipCommand (std::string trackId,
                                      std::string clipId,
                                      sequencing::ExpressionLaneId laneId,
                                      std::optional<sequencing::ExpressionClipId> previousEnvelopeId,
                                      sequencing::PhraseEnvelopeClip envelope);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    std::optional<sequencing::ExpressionClipId> previousEnvelopeId_;
    sequencing::PhraseEnvelopeClip envelope_;
};

class RemovePhraseEnvelopeClipCommand final : public ClipExpressionCommand
{
public:
    RemovePhraseEnvelopeClipCommand (std::string trackId,
                                     std::string clipId,
                                     sequencing::ExpressionLaneId laneId,
                                     sequencing::ExpressionClipId envelopeId);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionClipId envelopeId_;
};

class AddCyclicExpressionClipCommand final : public ClipExpressionCommand
{
public:
    AddCyclicExpressionClipCommand (std::string trackId,
                                    std::string clipId,
                                    sequencing::ExpressionLaneId laneId,
                                    sequencing::CyclicExpressionClip cyclic);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::CyclicExpressionClip cyclic_;
};

class ReplaceCyclicExpressionClipCommand final : public ClipExpressionCommand
{
public:
    ReplaceCyclicExpressionClipCommand (std::string trackId,
                                        std::string clipId,
                                        sequencing::ExpressionLaneId laneId,
                                        std::optional<sequencing::ExpressionClipId> previousCyclicId,
                                        sequencing::CyclicExpressionClip cyclic);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    std::optional<sequencing::ExpressionClipId> previousCyclicId_;
    sequencing::CyclicExpressionClip cyclic_;
};

class RemoveCyclicExpressionClipCommand final : public ClipExpressionCommand
{
public:
    RemoveCyclicExpressionClipCommand (std::string trackId,
                                       std::string clipId,
                                       sequencing::ExpressionLaneId laneId,
                                       sequencing::ExpressionClipId cyclicId);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionClipId cyclicId_;
};

class AddPitchSlurCommand final : public ClipExpressionCommand
{
public:
    AddPitchSlurCommand (std::string trackId,
                         std::string clipId,
                         sequencing::ExpressionLaneId laneId,
                         sequencing::PitchSlur slur);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::PitchSlur slur_;
};

class AddPitchSlursCommand final : public ClipExpressionCommand
{
public:
    AddPitchSlursCommand (std::string trackId,
                          std::string clipId,
                          sequencing::ExpressionLaneId laneId,
                          std::vector<sequencing::PitchSlur> slurs);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    std::vector<sequencing::PitchSlur> slurs_;
};

class ReplacePitchSlursCommand final : public ClipExpressionCommand
{
public:
    ReplacePitchSlursCommand (std::string trackId,
                              std::string clipId,
                              sequencing::ExpressionLaneId laneId,
                              std::vector<sequencing::PitchSlur> slurs);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    std::vector<sequencing::PitchSlur> slurs_;
};

class RemovePitchSlurCommand final : public ClipExpressionCommand
{
public:
    RemovePitchSlurCommand (std::string trackId,
                            std::string clipId,
                            sequencing::ExpressionLaneId laneId,
                            sequencing::ExpressionClipId slurId);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionClipId slurId_;
};

class AddVibratoExpressionCommand final : public ClipExpressionCommand
{
public:
    AddVibratoExpressionCommand (std::string trackId,
                                 std::string clipId,
                                 sequencing::ExpressionLaneId laneId,
                                 sequencing::VibratoExpression vibrato);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::VibratoExpression vibrato_;
};

class ReplaceVibratoExpressionCommand final : public ClipExpressionCommand
{
public:
    ReplaceVibratoExpressionCommand (std::string trackId,
                                     std::string clipId,
                                     sequencing::ExpressionLaneId laneId,
                                     sequencing::VibratoExpression vibrato);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::VibratoExpression vibrato_;
};

class RemoveVibratoExpressionCommand final : public ClipExpressionCommand
{
public:
    RemoveVibratoExpressionCommand (std::string trackId,
                                    std::string clipId,
                                    sequencing::ExpressionLaneId laneId,
                                    sequencing::ExpressionClipId vibratoId);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    sequencing::ExpressionLaneId laneId_;
    sequencing::ExpressionClipId vibratoId_;
};
}
