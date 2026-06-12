#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tsq::core::sequencing
{
struct DecibelRange
{
    double minimumFiniteDb = -60.0;
    double maximumDb = 6.0;
};

struct StereoPanGains
{
    double left = 1.0;
    double right = 1.0;
};

enum class PanLaw
{
    linearBalance,
    constantPowerMinus3Db
};

double silenceDecibels() noexcept;
bool isSilenceDecibels (double value) noexcept;
double clampDecibels (double value, DecibelRange range = {});
double gainFromDecibels (double decibels, DecibelRange range = {});
double decibelsFromGain (double gain, DecibelRange range = {});
std::optional<double> parseDecibelText (std::string_view text, DecibelRange range = {});
std::string formatDecibelText (double decibels, int decimalPlaces = 1, DecibelRange range = {});

double clampPan (double pan);
StereoPanGains stereoPanGains (double pan, PanLaw panLaw = PanLaw::constantPowerMinus3Db);
double sendDecibelsFromNormalizedLevel (double normalizedLevel, double minimumDb = -60.0);

class LinearControlRamp
{
public:
    void reset (double value) noexcept;
    void setTarget (double targetValue, int steps) noexcept;
    double next() noexcept;
    double current() const noexcept;
    bool active() const noexcept;

private:
    double current_ = 0.0;
    double target_ = 0.0;
    double increment_ = 0.0;
    int remainingSteps_ = 0;
};
}
