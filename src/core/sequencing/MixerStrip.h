#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tsq::core::sequencing
{
class MixerStrip
{
public:
    static constexpr double minimumFiniteVolumeDb = -60.0;
    static constexpr double maximumVolumeDb = 6.0;
    static constexpr double minimumPan = -1.0;
    static constexpr double maximumPan = 1.0;

    static double silenceDb() noexcept;
    static bool isSilenceDb (double value) noexcept;
    static double clampVolumeDb (double value);
    static double gainFromDecibels (double decibels);
    static double decibelsFromGain (double gain);

    MixerStrip() = default;
    explicit MixerStrip (double volumeDb, double pan = 0.0);

    double volumeDb() const noexcept;
    double linearGain() const noexcept;
    void setVolumeDb (double volumeDb);

    double pan() const noexcept;
    void setPan (double pan);

    bool active() const noexcept;
    bool muted() const noexcept;
    void setActive (bool active) noexcept;
    void setMuted (bool muted) noexcept;

    bool soloed() const noexcept;
    void setSoloed (bool soloed) noexcept;

    const std::string& meterSourceId() const noexcept;
    void setMeterSourceId (std::string meterSourceId);

    const std::optional<std::uint32_t>& colorArgb() const noexcept;
    void setColorArgb (std::optional<std::uint32_t> colorArgb) noexcept;

private:
    double volumeDb_ = 0.0;
    double pan_ = 0.0;
    bool active_ = true;
    bool soloed_ = false;
    std::string meterSourceId_;
    std::optional<std::uint32_t> colorArgb_;
};

bool operator== (const MixerStrip& lhs, const MixerStrip& rhs) noexcept;
bool operator!= (const MixerStrip& lhs, const MixerStrip& rhs) noexcept;
}
