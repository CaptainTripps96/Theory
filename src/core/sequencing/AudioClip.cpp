#include "core/sequencing/AudioClip.h"

#include "core/sequencing/MixerStrip.h"

#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
bool AudioSourceReference::isValid() const noexcept
{
    return ! sourceId.empty() && ! filePath.empty();
}

AudioClip::AudioClip (std::string id,
                      std::string name,
                      AudioSourceReference source,
                      time::TickPosition startInProject,
                      time::TickDuration length,
                      time::TickDuration sourceOffset)
    : id_ (std::move (id)),
      name_ (std::move (name)),
      source_ (std::move (source)),
      startInProject_ (startInProject),
      length_ (length),
      sourceOffset_ (sourceOffset)
{
    if (id_.empty())
        throw std::invalid_argument ("Audio clip requires a non-empty ID");

    if (name_.empty())
        throw std::invalid_argument ("Audio clip requires a non-empty name");

    if (! source_.isValid())
        throw std::invalid_argument ("Audio clip requires a valid source reference");

    if (length_.ticks() <= 0)
        throw std::invalid_argument ("Audio clip length must be positive");

    if (sourceOffset_.ticks() < 0)
        throw std::invalid_argument ("Audio clip source offset must not be negative");
}

const std::string& AudioClip::id() const noexcept
{
    return id_;
}

const std::string& AudioClip::name() const noexcept
{
    return name_;
}

const AudioSourceReference& AudioClip::source() const noexcept
{
    return source_;
}

time::TickPosition AudioClip::startInProject() const noexcept
{
    return startInProject_;
}

time::TickPosition AudioClip::endInProject() const noexcept
{
    return startInProject_ + length_;
}

time::TickDuration AudioClip::length() const noexcept
{
    return length_;
}

time::TickDuration AudioClip::sourceOffset() const noexcept
{
    return sourceOffset_;
}

bool AudioClip::loopEnabled() const noexcept
{
    return loopEnabled_;
}

void AudioClip::setLoopEnabled (bool enabled) noexcept
{
    loopEnabled_ = enabled;
}

bool AudioClip::stretchToTempo() const noexcept
{
    return stretchToTempo_;
}

void AudioClip::setStretchToTempo (bool stretch) noexcept
{
    stretchToTempo_ = stretch;
}

double AudioClip::gainDb() const noexcept
{
    return gainDb_;
}

void AudioClip::setGainDb (double gainDb)
{
    gainDb_ = MixerStrip::clampVolumeDb (gainDb);
}

AudioClip AudioClip::withStartInProject (time::TickPosition startInProject) const
{
    auto copy = *this;
    copy.startInProject_ = startInProject;
    return copy;
}

AudioClip AudioClip::withLength (time::TickDuration length) const
{
    auto copy = *this;
    if (length.ticks() <= 0)
        throw std::invalid_argument ("Audio clip length must be positive");

    copy.length_ = length;
    return copy;
}

bool audioClipsOverlap (const AudioClip& lhs, const AudioClip& rhs) noexcept
{
    return lhs.startInProject() < rhs.endInProject() && rhs.startInProject() < lhs.endInProject();
}
}
