#include "core/sequencing/Metering.h"

#include "core/sequencing/MixerMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tsq::core::sequencing
{
namespace
{
void validateConfig (const MeterBallisticsConfig& config)
{
    if (std::isnan (config.floorDb) || std::isnan (config.attackMs) || std::isnan (config.releaseDbPerSecond))
        throw std::invalid_argument ("Meter ballistics config must not contain NaN");

    if (config.attackMs < 0.0 || config.releaseDbPerSecond < 0.0)
        throw std::invalid_argument ("Meter ballistics timing must be non-negative");
}
}

MeterBallistics::MeterBallistics (MeterBallisticsConfig config)
{
    setConfig (config);
    reset();
}

void MeterBallistics::setConfig (MeterBallisticsConfig config)
{
    validateConfig (config);
    config_ = config;
    valueDb_ = std::max (valueDb_, config_.floorDb);
}

const MeterBallisticsConfig& MeterBallistics::config() const noexcept
{
    return config_;
}

void MeterBallistics::reset() noexcept
{
    valueDb_ = config_.floorDb;
}

void MeterBallistics::resetTo (double decibels)
{
    if (std::isnan (decibels))
        throw std::invalid_argument ("Meter value must not be NaN");

    valueDb_ = isSilenceDecibels (decibels) ? config_.floorDb : std::max (decibels, config_.floorDb);
}

double MeterBallistics::valueDb() const noexcept
{
    return valueDb_;
}

double MeterBallistics::process (double targetDb, double elapsedMs)
{
    if (std::isnan (targetDb) || std::isnan (elapsedMs))
        throw std::invalid_argument ("Meter process values must not be NaN");

    if (elapsedMs < 0.0)
        throw std::invalid_argument ("Meter elapsed time must be non-negative");

    const auto clampedTarget = isSilenceDecibels (targetDb) ? config_.floorDb : std::max (targetDb, config_.floorDb);

    if (clampedTarget >= valueDb_)
    {
        if (config_.attackMs <= 0.0 || elapsedMs >= config_.attackMs)
        {
            valueDb_ = clampedTarget;
        }
        else
        {
            const auto attackAlpha = std::clamp (elapsedMs / config_.attackMs, 0.0, 1.0);
            valueDb_ += (clampedTarget - valueDb_) * attackAlpha;
        }
    }
    else
    {
        const auto releaseAmount = config_.releaseDbPerSecond * (elapsedMs / 1000.0);
        valueDb_ = std::max (clampedTarget, valueDb_ - releaseAmount);
    }

    valueDb_ = std::max (valueDb_, config_.floorDb);
    return valueDb_;
}
}
