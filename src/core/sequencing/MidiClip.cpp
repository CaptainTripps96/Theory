#include "core/sequencing/MidiClip.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
MidiClip::MidiClip (std::string id,
                    std::string name,
                    time::TickPosition startInProject,
                    time::TickDuration length,
                    ClipLoop loop)
    : id_ (std::move (id)),
      name_ (std::move (name)),
      startInProject_ (startInProject),
      length_ (length),
      loop_ (std::move (loop))
{
    if (id_.empty())
        throw std::invalid_argument ("MidiClip requires a non-empty ID");

    if (name_.empty())
        throw std::invalid_argument ("MidiClip requires a non-empty name");

    if (startInProject_.ticks() < 0)
        throw std::invalid_argument ("MidiClip project start tick must not be negative");

    if (length_.ticks() <= 0)
        throw std::invalid_argument ("MidiClip length must be positive");
}

const std::string& MidiClip::id() const noexcept
{
    return id_;
}

const std::string& MidiClip::name() const noexcept
{
    return name_;
}

time::TickPosition MidiClip::startInProject() const noexcept
{
    return startInProject_;
}

time::TickPosition MidiClip::endInProject() const noexcept
{
    return startInProject_ + length_;
}

time::TickDuration MidiClip::length() const noexcept
{
    return length_;
}

time::TickDuration MidiClip::sourceLength() const noexcept
{
    return loop_.isEnabled() ? loop_.loopDuration() : length_;
}

Region MidiClip::projectRegion() const
{
    return Region { startInProject_, endInProject() };
}

const ClipLoop& MidiClip::loop() const noexcept
{
    return loop_;
}

MidiClip MidiClip::withStartInProject (time::TickPosition startInProject) const
{
    auto result = *this;
    if (startInProject.ticks() < 0)
        throw std::invalid_argument ("MidiClip project start tick must not be negative");

    result.startInProject_ = startInProject;
    return result;
}

MidiClip MidiClip::withLength (time::TickDuration length) const
{
    auto result = *this;
    if (length.ticks() <= 0)
        throw std::invalid_argument ("MidiClip length must be positive");

    result.length_ = length;

    for (const auto& note : notes_)
    {
        if (note.endInClip() > time::TickPosition {} + result.sourceLength())
            throw std::invalid_argument ("MidiClip source cannot be shorter than an existing note end");
    }

    return result;
}

MidiClip MidiClip::withLoop (ClipLoop loop) const
{
    auto result = *this;
    result.loop_ = std::move (loop);

    for (const auto& note : notes_)
    {
        if (note.endInClip() > time::TickPosition {} + result.sourceLength())
            throw std::invalid_argument ("MidiClip source cannot be shorter than an existing note end");
    }

    return result;
}

time::TickPosition MidiClip::localToProject (time::TickPosition localPosition) const
{
    if (localPosition.ticks() < 0 || localPosition > time::TickPosition {} + length_)
        throw std::invalid_argument ("Clip-local tick is outside the clip");

    return startInProject_ + (localPosition - time::TickPosition {});
}

time::TickPosition MidiClip::projectToLocal (time::TickPosition projectPosition) const
{
    if (projectPosition < startInProject_ || projectPosition > endInProject())
        throw std::invalid_argument ("Project tick is outside the clip");

    return time::TickPosition {} + (projectPosition - startInProject_);
}

std::vector<Region> MidiClip::loopRepetitions() const
{
    return loop_.repetitionsForLength (length_);
}

void MidiClip::addNote (MidiNote note)
{
    if (note.endInClip() > time::TickPosition {} + sourceLength())
        throw std::invalid_argument ("MidiNote must end inside the clip source");

    const auto duplicateId = std::any_of (notes_.begin(), notes_.end(), [&note] (const auto& existingNote) {
        return existingNote.id() == note.id();
    });

    if (duplicateId)
        throw std::invalid_argument ("MidiClip already contains a note with this ID");

    notes_.push_back (std::move (note));
    std::stable_sort (notes_.begin(), notes_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInClip() < rhs.startInClip();
    });
}

MidiNote MidiClip::removeNoteById (const std::string& noteId)
{
    const auto match = std::find_if (notes_.begin(), notes_.end(), [&noteId] (const auto& note) {
        return note.id() == noteId;
    });

    if (match == notes_.end())
        throw std::invalid_argument ("MidiClip does not contain a note with this ID");

    auto removedNote = *match;
    notes_.erase (match);
    return removedNote;
}

void MidiClip::moveNote (const std::string& noteId, time::TickPosition startInClip)
{
    auto* note = findNoteById (noteId);
    if (note == nullptr)
        throw std::invalid_argument ("MidiClip does not contain a note with this ID");

    const auto movedNote = note->withStartInClip (startInClip);
    if (movedNote.endInClip() > time::TickPosition {} + sourceLength())
        throw std::invalid_argument ("MidiNote must end inside the clip source");

    *note = movedNote;
    std::stable_sort (notes_.begin(), notes_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInClip() < rhs.startInClip();
    });
}

void MidiClip::resizeNote (const std::string& noteId, time::TickDuration duration)
{
    auto* note = findNoteById (noteId);
    if (note == nullptr)
        throw std::invalid_argument ("MidiClip does not contain a note with this ID");

    const auto resizedNote = note->withDuration (duration);
    if (resizedNote.endInClip() > time::TickPosition {} + sourceLength())
        throw std::invalid_argument ("MidiNote must end inside the clip source");

    *note = resizedNote;
}

void MidiClip::replaceNote (const std::string& noteId, MidiNote replacement)
{
    auto* note = findNoteById (noteId);
    if (note == nullptr)
        throw std::invalid_argument ("MidiClip does not contain a note with this ID");

    if (replacement.id() != noteId)
        throw std::invalid_argument ("Replacement MidiNote ID must match the note being replaced");

    if (replacement.endInClip() > time::TickPosition {} + sourceLength())
        throw std::invalid_argument ("MidiNote must end inside the clip source");

    *note = std::move (replacement);
    std::stable_sort (notes_.begin(), notes_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInClip() < rhs.startInClip();
    });
}

const std::vector<MidiNote>& MidiClip::notes() const noexcept
{
    return notes_;
}

MidiNote* MidiClip::findNoteById (const std::string& noteId) noexcept
{
    const auto match = std::find_if (notes_.begin(), notes_.end(), [&noteId] (const auto& note) {
        return note.id() == noteId;
    });

    if (match == notes_.end())
        return nullptr;

    return &*match;
}

const MidiNote* MidiClip::findNoteById (const std::string& noteId) const noexcept
{
    const auto match = std::find_if (notes_.begin(), notes_.end(), [&noteId] (const auto& note) {
        return note.id() == noteId;
    });

    if (match == notes_.end())
        return nullptr;

    return &*match;
}

void MidiClip::snapshotHarmonicContext (const HarmonicContextResolver& resolver)
{
    harmonicMetadata_ = ClipHarmonicMap::snapshotFromProject (startInProject_, length_, resolver);
}

void MidiClip::setHarmonicMetadata (ClipHarmonicMap harmonicMetadata)
{
    harmonicMetadata_ = std::move (harmonicMetadata);
}

ClipHarmonicMap& MidiClip::harmonicMetadata() noexcept
{
    return harmonicMetadata_;
}

const ClipHarmonicMap& MidiClip::harmonicMetadata() const noexcept
{
    return harmonicMetadata_;
}
}
