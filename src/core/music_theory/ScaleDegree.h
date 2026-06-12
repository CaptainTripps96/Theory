#pragma once

#include <string>

namespace tsq::core::music_theory
{
class ScaleDegree
{
public:
    ScaleDegree (int degree, int alteration = 0);

    static ScaleDegree natural (int degree);
    static ScaleDegree flat (int degree);
    static ScaleDegree sharp (int degree);

    int degree() const noexcept;
    int alteration() const noexcept;
    std::string toString() const;

private:
    int degree_ = 1;
    int alteration_ = 0;
};

bool operator== (ScaleDegree lhs, ScaleDegree rhs) noexcept;
bool operator!= (ScaleDegree lhs, ScaleDegree rhs) noexcept;
}
