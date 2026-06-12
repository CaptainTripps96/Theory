#include "core/sequencing/MixerMath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace tsq::core::sequencing
{
namespace
{
constexpr double pi = 3.14159265358979323846264338327950288;

std::string trimmedLower (std::string_view text)
{
    auto first = text.begin();
    auto last = text.end();

    while (first != last && std::isspace (static_cast<unsigned char> (*first)))
        ++first;

    while (first != last)
    {
        const auto previous = last - 1;
        if (! std::isspace (static_cast<unsigned char> (*previous)))
            break;

        last = previous;
    }

    std::string result;
    result.reserve (static_cast<std::size_t> (last - first));
    for (auto it = first; it != last; ++it)
        result.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (*it))));

    return result;
}
}

double silenceDecibels() noexcept
{
    return -std::numeric_limits<double>::infinity();
}

bool isSilenceDecibels (double value) noexcept
{
    return std::isinf (value) && value < 0.0;
}

double clampDecibels (double value, DecibelRange range)
{
    if (std::isnan (value))
        throw std::invalid_argument ("Decibel value must not be NaN");

    if (isSilenceDecibels (value))
        return silenceDecibels();

    if (! std::isfinite (value))
        throw std::invalid_argument ("Decibel value must be finite or -inf");

    if (std::isnan (range.minimumFiniteDb) || std::isnan (range.maximumDb) || range.minimumFiniteDb > range.maximumDb)
        throw std::invalid_argument ("Invalid decibel range");

    return std::clamp (value, range.minimumFiniteDb, range.maximumDb);
}

double gainFromDecibels (double decibels, DecibelRange range)
{
    const auto clamped = clampDecibels (decibels, range);
    if (isSilenceDecibels (clamped))
        return 0.0;

    return std::pow (10.0, clamped / 20.0);
}

double decibelsFromGain (double gain, DecibelRange range)
{
    if (std::isnan (gain) || gain < 0.0)
        throw std::invalid_argument ("Gain must be non-negative");

    if (gain <= 0.0)
        return silenceDecibels();

    return clampDecibels (20.0 * std::log10 (gain), range);
}

std::optional<double> parseDecibelText (std::string_view text, DecibelRange range)
{
    auto valueText = trimmedLower (text);
    if (valueText.empty())
        return std::nullopt;

    if (valueText == "-inf" || valueText == "-infinity" || valueText == "inf-" || valueText == "off")
        return silenceDecibels();

    if (valueText.size() >= 2 && valueText.substr (valueText.size() - 2) == "db")
        valueText.erase (valueText.size() - 2);

    valueText = trimmedLower (valueText);
    if (valueText.empty())
        return std::nullopt;

    try
    {
        std::size_t consumed = 0;
        const auto parsed = std::stod (valueText, &consumed);
        if (consumed != valueText.size())
            return std::nullopt;

        return clampDecibels (parsed, range);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::string formatDecibelText (double decibels, int decimalPlaces, DecibelRange range)
{
    if (isSilenceDecibels (decibels))
        return "-inf dB";

    const auto clamped = clampDecibels (decibels, range);
    std::ostringstream stream;
    stream << std::fixed << std::setprecision (std::max (0, decimalPlaces)) << clamped << " dB";
    return stream.str();
}

double clampPan (double pan)
{
    if (std::isnan (pan))
        throw std::invalid_argument ("Pan must not be NaN");

    return std::clamp (pan, -1.0, 1.0);
}

StereoPanGains stereoPanGains (double pan, PanLaw panLaw)
{
    const auto clamped = clampPan (pan);

    if (panLaw == PanLaw::linearBalance)
    {
        if (clamped < 0.0)
            return { 1.0, 1.0 + clamped };

        return { 1.0 - clamped, 1.0 };
    }

    const auto angle = (clamped + 1.0) * (pi / 4.0);
    return { std::cos (angle), std::sin (angle) };
}

double sendDecibelsFromNormalizedLevel (double normalizedLevel, double minimumDb)
{
    if (std::isnan (normalizedLevel))
        throw std::invalid_argument ("Send level must not be NaN");

    const auto clamped = std::clamp (normalizedLevel, 0.0, 1.0);
    if (clamped <= 0.0)
        return minimumDb;

    return std::max (minimumDb, 20.0 * std::log10 (clamped));
}

void LinearControlRamp::reset (double value) noexcept
{
    current_ = value;
    target_ = value;
    increment_ = 0.0;
    remainingSteps_ = 0;
}

void LinearControlRamp::setTarget (double targetValue, int steps) noexcept
{
    target_ = targetValue;
    remainingSteps_ = std::max (0, steps);

    if (remainingSteps_ == 0)
    {
        current_ = target_;
        increment_ = 0.0;
        return;
    }

    increment_ = (target_ - current_) / static_cast<double> (remainingSteps_);
}

double LinearControlRamp::next() noexcept
{
    if (remainingSteps_ <= 0)
        return current_;

    --remainingSteps_;
    if (remainingSteps_ == 0)
    {
        current_ = target_;
        increment_ = 0.0;
    }
    else
    {
        current_ += increment_;
    }

    return current_;
}

double LinearControlRamp::current() const noexcept
{
    return current_;
}

bool LinearControlRamp::active() const noexcept
{
    return remainingSteps_ > 0;
}
}
