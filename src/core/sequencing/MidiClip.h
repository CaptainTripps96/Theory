#pragma once

#include "core/sequencing/ClipHarmonicMap.h"
#include "core/sequencing/ClipLoop.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiNote.h"

#include <cstddef>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
class MidiClip
{
public:
    MidiClip (std::string id,
              std::string name,
              time::TickPosition startInProject,
              time::TickDuration length,
              ClipLoop loop = ClipLoop::disabled());

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    time::TickPosition startInProject() const noexcept;
    time::TickPosition endInProject() const noexcept;
    time::TickDuration length() const noexcept;
    time::TickDuration sourceLength() const noexcept;
    Region projectRegion() const;
    const ClipLoop& loop() const noexcept;
    MidiClip withStartInProject (time::TickPosition startInProject) const;
    MidiClip withLength (time::TickDuration length) const;
    MidiClip withLoop (ClipLoop loop) const;

    time::TickPosition localToProject (time::TickPosition localPosition) const;
    time::TickPosition projectToLocal (time::TickPosition projectPosition) const;
    std::vector<Region> loopRepetitions() const;

    void addNote (MidiNote note);
    void addNotes (std::vector<MidiNote> notes);
    void reserveNotes (std::size_t noteCapacity);
    MidiNote removeNoteById (const std::string& noteId);
    void moveNote (const std::string& noteId, time::TickPosition startInClip);
    void resizeNote (const std::string& noteId, time::TickDuration duration);
    void replaceNote (const std::string& noteId, MidiNote replacement);
    const std::vector<MidiNote>& notes() const noexcept;
    MidiNote* findNoteById (const std::string& noteId) noexcept;
    const MidiNote* findNoteById (const std::string& noteId) const noexcept;

    void snapshotHarmonicContext (const HarmonicContextResolver& resolver);
    void setHarmonicMetadata (ClipHarmonicMap harmonicMetadata);
    ClipHarmonicMap& harmonicMetadata() noexcept;
    const ClipHarmonicMap& harmonicMetadata() const noexcept;
    void setExpressionState (ExpressionState expressionState);
    ExpressionState& expressionState() noexcept;
    const ExpressionState& expressionState() const noexcept;

private:
    std::string id_;
    std::string name_;
    time::TickPosition startInProject_ {};
    time::TickDuration length_ {};
    ClipLoop loop_;
    std::vector<MidiNote> notes_;
    ClipHarmonicMap harmonicMetadata_;
    ExpressionState expressionState_;
};
}
