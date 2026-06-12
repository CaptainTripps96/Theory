#include "core/sequencing/MixerStrip.h"

#include "core/sequencing/MixerMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
double MixerStrip::silenceDb() noexcept
{
    return silenceDecibels();
}

bool MixerStrip::isSilenceDb (double value) noexcept
{
    return isSilenceDecibels (value);
}

double MixerStrip::clampVolumeDb (double value)
{
    return clampDecibels (value, { minimumFiniteVolumeDb, maximumVolumeDb });
}

double MixerStrip::gainFromDecibels (double decibels)
{
    return core::sequencing::gainFromDecibels (decibels, { minimumFiniteVolumeDb, maximumVolumeDb });
}

double MixerStrip::decibelsFromGain (double gain)
{
    return core::sequencing::decibelsFromGain (gain, { minimumFiniteVolumeDb, maximumVolumeDb });
}

MixerStrip::MixerStrip (double volumeDb, double pan)
{
    setVolumeDb (volumeDb);
    setPan (pan);
}

double MixerStrip::volumeDb() const noexcept
{
    return volumeDb_;
}

double MixerStrip::linearGain() const noexcept
{
    return isSilenceDb (volumeDb_) ? 0.0 : std::pow (10.0, volumeDb_ / 20.0);
}

void MixerStrip::setVolumeDb (double volumeDb)
{
    volumeDb_ = clampVolumeDb (volumeDb);
}

double MixerStrip::pan() const noexcept
{
    return pan_;
}

void MixerStrip::setPan (double pan)
{
    pan_ = clampPan (pan);
}

bool MixerStrip::active() const noexcept
{
    return active_;
}

bool MixerStrip::muted() const noexcept
{
    return ! active_;
}

void MixerStrip::setActive (bool active) noexcept
{
    active_ = active;
}

void MixerStrip::setMuted (bool muted) noexcept
{
    active_ = ! muted;
}

bool MixerStrip::soloed() const noexcept
{
    return soloed_;
}

void MixerStrip::setSoloed (bool soloed) noexcept
{
    soloed_ = soloed;
}

const std::string& MixerStrip::meterSourceId() const noexcept
{
    return meterSourceId_;
}

void MixerStrip::setMeterSourceId (std::string meterSourceId)
{
    meterSourceId_ = std::move (meterSourceId);
}

const std::optional<std::uint32_t>& MixerStrip::colorArgb() const noexcept
{
    return colorArgb_;
}

void MixerStrip::setColorArgb (std::optional<std::uint32_t> colorArgb) noexcept
{
    colorArgb_ = colorArgb;
}

bool operator== (const MixerStrip& lhs, const MixerStrip& rhs) noexcept
{
    return lhs.volumeDb() == rhs.volumeDb()
        && lhs.pan() == rhs.pan()
        && lhs.active() == rhs.active()
        && lhs.soloed() == rhs.soloed()
        && lhs.meterSourceId() == rhs.meterSourceId()
        && lhs.colorArgb() == rhs.colorArgb();
}

bool operator!= (const MixerStrip& lhs, const MixerStrip& rhs) noexcept
{
    return ! (lhs == rhs);
}
}
