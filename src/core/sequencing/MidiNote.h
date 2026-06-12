#pragma once

#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/NoteName.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"
#include "core/time/Tick.h"

#include <optional>
#include <string>

namespace tsq::core::sequencing
{
class MidiNote
{
public:
    MidiNote (std::string id,
              music_theory::MidiPitch pitch,
              time::TickPosition startInClip,
              time::TickDuration duration,
              int velocity,
              std::optional<music_theory::NoteName> spelling = std::nullopt,
              std::optional<NoteHarmonicInterpretation> harmonicInterpretation = std::nullopt);

    const std::string& id() const noexcept;
    music_theory::MidiPitch pitch() const noexcept;
    time::TickPosition startInClip() const noexcept;
    time::TickPosition endInClip() const noexcept;
    time::TickDuration duration() const noexcept;
    int velocity() const noexcept;
    const std::optional<music_theory::NoteName>& spelling() const noexcept;
    const std::optional<NoteHarmonicInterpretation>& harmonicInterpretation() const noexcept;

    MidiNote withStartInClip (time::TickPosition startInClip) const;
    MidiNote withDuration (time::TickDuration duration) const;
    MidiNote withPitch (music_theory::MidiPitch pitch,
                        std::optional<music_theory::NoteName> spelling = std::nullopt) const;
    MidiNote withHarmonicInterpretation (std::optional<NoteHarmonicInterpretation> harmonicInterpretation) const;

private:
    std::string id_;
    music_theory::MidiPitch pitch_;
    time::TickPosition startInClip_ {};
    time::TickDuration duration_ {};
    int velocity_ = 100;
    std::optional<music_theory::NoteName> spelling_;
    std::optional<NoteHarmonicInterpretation> harmonicInterpretation_;
};
}
