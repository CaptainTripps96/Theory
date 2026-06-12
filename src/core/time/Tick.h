#pragma once

#include <cstdint>

namespace tsq::core::time
{
constexpr std::int64_t ticksPerQuarterNote = 960;

class TickDuration
{
public:
    constexpr TickDuration() noexcept = default;
    explicit constexpr TickDuration (std::int64_t ticks) noexcept
        : ticks_ (ticks)
    {
    }

    static constexpr TickDuration fromTicks (std::int64_t ticks) noexcept
    {
        return TickDuration { ticks };
    }

    constexpr std::int64_t ticks() const noexcept
    {
        return ticks_;
    }

    constexpr TickDuration operator-() const noexcept
    {
        return TickDuration { -ticks_ };
    }

private:
    std::int64_t ticks_ = 0;
};

class TickPosition
{
public:
    constexpr TickPosition() noexcept = default;
    explicit constexpr TickPosition (std::int64_t ticks) noexcept
        : ticks_ (ticks)
    {
    }

    static constexpr TickPosition fromTicks (std::int64_t ticks) noexcept
    {
        return TickPosition { ticks };
    }

    constexpr std::int64_t ticks() const noexcept
    {
        return ticks_;
    }

private:
    std::int64_t ticks_ = 0;
};

constexpr TickDuration quarterNoteDuration() noexcept
{
    return TickDuration { ticksPerQuarterNote };
}

constexpr TickDuration eighthNoteDuration() noexcept
{
    return TickDuration { ticksPerQuarterNote / 2 };
}

constexpr TickDuration sixteenthNoteDuration() noexcept
{
    return TickDuration { ticksPerQuarterNote / 4 };
}

TickDuration durationFromQuarterNotes (double quarterNotes);
double quarterNotesFromDuration (TickDuration duration) noexcept;

constexpr bool operator== (TickDuration lhs, TickDuration rhs) noexcept
{
    return lhs.ticks() == rhs.ticks();
}

constexpr bool operator!= (TickDuration lhs, TickDuration rhs) noexcept
{
    return ! (lhs == rhs);
}

constexpr bool operator< (TickDuration lhs, TickDuration rhs) noexcept
{
    return lhs.ticks() < rhs.ticks();
}

constexpr bool operator<= (TickDuration lhs, TickDuration rhs) noexcept
{
    return lhs.ticks() <= rhs.ticks();
}

constexpr bool operator> (TickDuration lhs, TickDuration rhs) noexcept
{
    return rhs < lhs;
}

constexpr bool operator>= (TickDuration lhs, TickDuration rhs) noexcept
{
    return rhs <= lhs;
}

constexpr TickDuration operator+ (TickDuration lhs, TickDuration rhs) noexcept
{
    return TickDuration { lhs.ticks() + rhs.ticks() };
}

constexpr TickDuration operator- (TickDuration lhs, TickDuration rhs) noexcept
{
    return TickDuration { lhs.ticks() - rhs.ticks() };
}

constexpr TickDuration operator* (TickDuration duration, std::int64_t multiplier) noexcept
{
    return TickDuration { duration.ticks() * multiplier };
}

constexpr TickDuration operator* (std::int64_t multiplier, TickDuration duration) noexcept
{
    return duration * multiplier;
}

constexpr TickDuration operator/ (TickDuration duration, std::int64_t divisor) noexcept
{
    return TickDuration { duration.ticks() / divisor };
}

constexpr bool operator== (TickPosition lhs, TickPosition rhs) noexcept
{
    return lhs.ticks() == rhs.ticks();
}

constexpr bool operator!= (TickPosition lhs, TickPosition rhs) noexcept
{
    return ! (lhs == rhs);
}

constexpr bool operator< (TickPosition lhs, TickPosition rhs) noexcept
{
    return lhs.ticks() < rhs.ticks();
}

constexpr bool operator<= (TickPosition lhs, TickPosition rhs) noexcept
{
    return lhs.ticks() <= rhs.ticks();
}

constexpr bool operator> (TickPosition lhs, TickPosition rhs) noexcept
{
    return rhs < lhs;
}

constexpr bool operator>= (TickPosition lhs, TickPosition rhs) noexcept
{
    return rhs <= lhs;
}

constexpr TickPosition operator+ (TickPosition position, TickDuration duration) noexcept
{
    return TickPosition { position.ticks() + duration.ticks() };
}

constexpr TickPosition operator- (TickPosition position, TickDuration duration) noexcept
{
    return TickPosition { position.ticks() - duration.ticks() };
}

constexpr TickDuration operator- (TickPosition lhs, TickPosition rhs) noexcept
{
    return TickDuration { lhs.ticks() - rhs.ticks() };
}
}
