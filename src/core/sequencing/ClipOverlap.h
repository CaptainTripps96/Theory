#pragma once

#include "core/sequencing/MidiClip.h"

namespace tsq::core::sequencing
{
bool clipsOverlap (const MidiClip& lhs, const MidiClip& rhs) noexcept;
}
