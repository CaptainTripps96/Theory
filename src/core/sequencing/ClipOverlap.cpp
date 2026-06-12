#include "core/sequencing/ClipOverlap.h"

namespace tsq::core::sequencing
{
bool clipsOverlap (const MidiClip& lhs, const MidiClip& rhs) noexcept
{
    return lhs.startInProject() < rhs.endInProject() && rhs.startInProject() < lhs.endInProject();
}
}
