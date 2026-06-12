#pragma once

#include "core/music_theory/ScaleInstance.h"

#include <string_view>
#include <vector>

namespace tsq::core::music_theory
{
class ScaleLibrary
{
public:
    ScaleLibrary();
    explicit ScaleLibrary (std::vector<ScaleDefinition> definitions);

    static ScaleLibrary createBuiltInLibrary();

    const std::vector<ScaleDefinition>& definitions() const noexcept;
    const ScaleDefinition* findByName (std::string_view name) const;
    std::vector<ScaleDefinition> search (std::string_view query) const;
    ScaleInstance instantiate (std::string_view name, NoteName root) const;
    void addDefinition (ScaleDefinition definition);

private:
    std::vector<ScaleDefinition> definitions_;
};
}
