#include "core/sequencing/MidiNote.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
MidiNote::MidiNote (std::string id,
                    music_theory::MidiPitch pitch,
                    time::TickPosition startInClip,
                    time::TickDuration duration,
                    int velocity,
                    std::optional<music_theory::NoteName> spelling,
                    std::optional<NoteHarmonicInterpretation> harmonicInterpretation)
    : id_ (std::move (id)),
      pitch_ (pitch),
      startInClip_ (startInClip),
      duration_ (duration),
      velocity_ (velocity),
      spelling_ (spelling),
      harmonicInterpretation_ (std::move (harmonicInterpretation))
{
    if (id_.empty())
        throw std::invalid_argument ("MidiNote requires a non-empty ID");

    if (startInClip_.ticks() < 0)
        throw std::invalid_argument ("MidiNote start tick must not be negative");

    if (duration_.ticks() <= 0)
        throw std::invalid_argument ("MidiNote duration must be positive");

    if (velocity_ < 0 || velocity_ > 127)
        throw std::invalid_argument ("MidiNote velocity must be between 0 and 127");
}

const std::string& MidiNote::id() const noexcept
{
    return id_;
}

music_theory::MidiPitch MidiNote::pitch() const noexcept
{
    return pitch_;
}

time::TickPosition MidiNote::startInClip() const noexcept
{
    return startInClip_;
}

time::TickPosition MidiNote::endInClip() const noexcept
{
    return startInClip_ + duration_;
}

time::TickDuration MidiNote::duration() const noexcept
{
    return duration_;
}

int MidiNote::velocity() const noexcept
{
    return velocity_;
}

const std::optional<music_theory::NoteName>& MidiNote::spelling() const noexcept
{
    return spelling_;
}

const std::optional<NoteHarmonicInterpretation>& MidiNote::harmonicInterpretation() const noexcept
{
    return harmonicInterpretation_;
}

MidiNote MidiNote::withStartInClip (time::TickPosition startInClip) const
{
    return MidiNote { id_, pitch_, startInClip, duration_, velocity_, spelling_, harmonicInterpretation_ };
}

MidiNote MidiNote::withDuration (time::TickDuration duration) const
{
    return MidiNote { id_, pitch_, startInClip_, duration, velocity_, spelling_, harmonicInterpretation_ };
}

MidiNote MidiNote::withPitch (music_theory::MidiPitch pitch, std::optional<music_theory::NoteName> spelling) const
{
    return MidiNote { id_, pitch, startInClip_, duration_, velocity_, spelling, harmonicInterpretation_ };
}

MidiNote MidiNote::withHarmonicInterpretation (std::optional<NoteHarmonicInterpretation> harmonicInterpretation) const
{
    return MidiNote { id_, pitch_, startInClip_, duration_, velocity_, spelling_, std::move (harmonicInterpretation) };
}
}
