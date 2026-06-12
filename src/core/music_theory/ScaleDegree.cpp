#include "core/music_theory/ScaleDegree.h"

#include <stdexcept>

namespace tsq::core::music_theory
{
ScaleDegree::ScaleDegree (int degree, int alteration)
    : degree_ (degree),
      alteration_ (alteration)
{
    if (degree_ < 1 || degree_ > 7)
        throw std::invalid_argument ("Scale degree must be between 1 and 7");

    if (alteration_ < -2 || alteration_ > 2)
        throw std::invalid_argument ("Scale degree alteration must be between -2 and 2");
}

ScaleDegree ScaleDegree::natural (int degree)
{
    return ScaleDegree { degree };
}

ScaleDegree ScaleDegree::flat (int degree)
{
    return ScaleDegree { degree, -1 };
}

ScaleDegree ScaleDegree::sharp (int degree)
{
    return ScaleDegree { degree, 1 };
}

int ScaleDegree::degree() const noexcept
{
    return degree_;
}

int ScaleDegree::alteration() const noexcept
{
    return alteration_;
}

std::string ScaleDegree::toString() const
{
    std::string text;

    if (alteration_ < 0)
        text.append (static_cast<size_t> (-alteration_), 'b');
    else if (alteration_ > 0)
        text.append (static_cast<size_t> (alteration_), '#');

    text += std::to_string (degree_);
    return text;
}

bool operator== (ScaleDegree lhs, ScaleDegree rhs) noexcept
{
    return lhs.degree() == rhs.degree() && lhs.alteration() == rhs.alteration();
}

bool operator!= (ScaleDegree lhs, ScaleDegree rhs) noexcept
{
    return ! (lhs == rhs);
}
}
