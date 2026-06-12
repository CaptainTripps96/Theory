#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/HarmonicContext.h"
#include "core/sequencing/MidiClip.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class ChromaticTransposeClipCommand : public Command
{
public:
    ChromaticTransposeClipCommand (std::string trackId,
                                   std::string clipId,
                                   sequencing::HarmonicContext targetContext);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    sequencing::HarmonicContext targetContext_;
    std::optional<sequencing::MidiClip> originalClip_;
    std::optional<sequencing::MidiClip> transformedClip_;
};

class ScaleDegreeTransposeClipCommand : public Command
{
public:
    ScaleDegreeTransposeClipCommand (std::string trackId,
                                     std::string clipId,
                                     sequencing::HarmonicContext targetContext);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    sequencing::HarmonicContext targetContext_;
    std::optional<sequencing::MidiClip> originalClip_;
    std::optional<sequencing::MidiClip> transformedClip_;
};

class ScaleDegreeTransposeSelectedNotesCommand : public Command
{
public:
    ScaleDegreeTransposeSelectedNotesCommand (std::string trackId,
                                              std::string clipId,
                                              std::vector<std::string> noteIds);

    std::string name() const override;
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<std::string> noteIds_;
    std::optional<sequencing::MidiClip> originalClip_;
    std::optional<sequencing::MidiClip> transformedClip_;
};
}
