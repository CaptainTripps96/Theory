#pragma once

namespace tsq::core::sequencing
{
struct MeterBallisticsConfig
{
    double floorDb = -60.0;
    double attackMs = 10.0;
    double releaseDbPerSecond = 24.0;
};

class MeterBallistics
{
public:
    explicit MeterBallistics (MeterBallisticsConfig config = {});

    void setConfig (MeterBallisticsConfig config);
    const MeterBallisticsConfig& config() const noexcept;

    void reset() noexcept;
    void resetTo (double decibels);
    double valueDb() const noexcept;
    double process (double targetDb, double elapsedMs);

private:
    MeterBallisticsConfig config_;
    double valueDb_ = -60.0;
};
}
