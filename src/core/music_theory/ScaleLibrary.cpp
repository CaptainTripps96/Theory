#include "core/music_theory/ScaleLibrary.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace tsq::core::music_theory
{
namespace
{
std::string normalizedText (std::string_view text)
{
    std::string result { text };
    std::transform (result.begin(), result.end(), result.begin(), [] (unsigned char c) {
        return static_cast<char> (std::tolower (c));
    });
    return result;
}

bool textEquals (std::string_view lhs, std::string_view rhs)
{
    return normalizedText (lhs) == normalizedText (rhs);
}

ScaleDegree degree (int degreeNumber)
{
    return ScaleDegree::natural (degreeNumber);
}

ScaleDegree flat (int degreeNumber)
{
    return ScaleDegree::flat (degreeNumber);
}

ScaleDegree sharp (int degreeNumber)
{
    return ScaleDegree::sharp (degreeNumber);
}

ScaleMetadata metadata (std::string name,
                        std::string category,
                        std::vector<std::string> tags,
                        std::string description)
{
    return ScaleMetadata { std::move (name), std::move (category), std::move (tags), std::move (description) };
}

ScaleDefinition makeScale (ScaleMetadata scaleMetadata,
                           std::vector<int> offsets,
                           std::vector<ScaleDegree> degrees,
                           SpellingPreference spelling = SpellingPreference::preferSharps)
{
    return ScaleDefinition { std::move (scaleMetadata), std::move (offsets), std::move (degrees), spelling };
}

std::vector<ScaleDefinition> createBuiltIns()
{
    return {
        makeScale (metadata ("Chromatic",
                             "Utility",
                             { "chromatic", "all notes", "twelve tone" },
                             "All twelve pitch classes from the chosen root."),
                   { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
                   { degree (1), sharp (1), degree (2), sharp (2), degree (3), degree (4), sharp (4), degree (5), sharp (5), degree (6), sharp (6), degree (7) }),

        makeScale (metadata ("Major",
                             "Diatonic Mode",
                             { "ionian", "major", "diatonic", "mode" },
                             "The standard major scale, also known as Ionian."),
                   { 0, 2, 4, 5, 7, 9, 11 },
                   { degree (1), degree (2), degree (3), degree (4), degree (5), degree (6), degree (7) }),

        makeScale (metadata ("Natural Minor",
                             "Diatonic Mode",
                             { "aeolian", "minor", "natural minor", "diatonic", "mode" },
                             "The natural minor scale, also known as Aeolian."),
                   { 0, 2, 3, 5, 7, 8, 10 },
                   { degree (1), degree (2), flat (3), degree (4), degree (5), flat (6), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Dorian",
                             "Diatonic Mode",
                             { "minor", "mode", "diatonic" },
                             "A minor mode with a natural sixth degree."),
                   { 0, 2, 3, 5, 7, 9, 10 },
                   { degree (1), degree (2), flat (3), degree (4), degree (5), degree (6), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Phrygian",
                             "Diatonic Mode",
                             { "minor", "mode", "diatonic", "flat two" },
                             "A minor mode with a lowered second degree."),
                   { 0, 1, 3, 5, 7, 8, 10 },
                   { degree (1), flat (2), flat (3), degree (4), degree (5), flat (6), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Lydian",
                             "Diatonic Mode",
                             { "major", "mode", "diatonic", "sharp four" },
                             "A major mode with a raised fourth degree."),
                   { 0, 2, 4, 6, 7, 9, 11 },
                   { degree (1), degree (2), degree (3), sharp (4), degree (5), degree (6), degree (7) }),

        makeScale (metadata ("Mixolydian",
                             "Diatonic Mode",
                             { "major", "mode", "diatonic", "flat seven" },
                             "A major mode with a lowered seventh degree."),
                   { 0, 2, 4, 5, 7, 9, 10 },
                   { degree (1), degree (2), degree (3), degree (4), degree (5), degree (6), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Locrian",
                             "Diatonic Mode",
                             { "minor", "mode", "diatonic", "flat five" },
                             "A diminished mode with lowered second and fifth degrees."),
                   { 0, 1, 3, 5, 6, 8, 10 },
                   { degree (1), flat (2), flat (3), degree (4), flat (5), flat (6), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Harmonic Minor",
                             "Minor",
                             { "minor", "harmonic minor", "raised seven" },
                             "Natural minor with a raised seventh degree."),
                   { 0, 2, 3, 5, 7, 8, 11 },
                   { degree (1), degree (2), flat (3), degree (4), degree (5), flat (6), degree (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Melodic Minor",
                             "Minor",
                             { "minor", "melodic minor", "jazz minor" },
                             "Minor with natural sixth and seventh degrees."),
                   { 0, 2, 3, 5, 7, 9, 11 },
                   { degree (1), degree (2), flat (3), degree (4), degree (5), degree (6), degree (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Major Pentatonic",
                             "Pentatonic",
                             { "major", "pentatonic", "five note" },
                             "A five-note major scale omitting the fourth and seventh degrees."),
                   { 0, 2, 4, 7, 9 },
                   { degree (1), degree (2), degree (3), degree (5), degree (6) }),

        makeScale (metadata ("Minor Pentatonic",
                             "Pentatonic",
                             { "minor", "pentatonic", "five note" },
                             "A five-note minor scale omitting the second and sixth degrees."),
                   { 0, 3, 5, 7, 10 },
                   { degree (1), flat (3), degree (4), degree (5), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Major Blues",
                             "Blues",
                             { "major", "blues", "hexatonic" },
                             "Major pentatonic with an added lowered third color tone."),
                   { 0, 2, 3, 4, 7, 9 },
                   { degree (1), degree (2), flat (3), degree (3), degree (5), degree (6) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Minor Blues",
                             "Blues",
                             { "minor", "blues", "hexatonic" },
                             "Minor pentatonic with an added lowered fifth color tone."),
                   { 0, 3, 5, 6, 7, 10 },
                   { degree (1), flat (3), degree (4), flat (5), degree (5), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Whole Tone",
                             "Symmetric",
                             { "whole tone", "augmented", "symmetric" },
                             "A six-note scale made entirely of whole steps."),
                   { 0, 2, 4, 6, 8, 10 },
                   { degree (1), degree (2), degree (3), sharp (4), sharp (5), flat (7) }),

        makeScale (metadata ("Diminished Half-Whole",
                             "Symmetric",
                             { "diminished", "half whole", "octatonic", "symmetric" },
                             "An eight-note diminished collection alternating half steps and whole steps."),
                   { 0, 1, 3, 4, 6, 7, 9, 10 },
                   { degree (1), flat (2), flat (3), degree (3), flat (5), degree (5), degree (6), flat (7) },
                   SpellingPreference::preferFlats),

        makeScale (metadata ("Diminished Whole-Half",
                             "Symmetric",
                             { "diminished", "whole half", "octatonic", "symmetric" },
                             "An eight-note diminished collection alternating whole steps and half steps."),
                   { 0, 2, 3, 5, 6, 8, 9, 11 },
                   { degree (1), degree (2), flat (3), degree (4), flat (5), flat (6), degree (6), degree (7) },
                   SpellingPreference::preferFlats),
    };
}
}

ScaleLibrary::ScaleLibrary()
    : ScaleLibrary (createBuiltIns())
{
}

ScaleLibrary::ScaleLibrary (std::vector<ScaleDefinition> definitions)
    : definitions_ (std::move (definitions))
{
}

ScaleLibrary ScaleLibrary::createBuiltInLibrary()
{
    return ScaleLibrary { createBuiltIns() };
}

const std::vector<ScaleDefinition>& ScaleLibrary::definitions() const noexcept
{
    return definitions_;
}

const ScaleDefinition* ScaleLibrary::findByName (std::string_view name) const
{
    const auto match = std::find_if (definitions_.begin(), definitions_.end(), [name] (const auto& definition) {
        if (textEquals (definition.name(), name))
            return true;

        return std::any_of (definition.tags().begin(), definition.tags().end(), [name] (const auto& tag) {
            return textEquals (tag, name);
        });
    });

    if (match == definitions_.end())
        return nullptr;

    return &*match;
}

std::vector<ScaleDefinition> ScaleLibrary::search (std::string_view query) const
{
    std::vector<ScaleDefinition> result;

    for (const auto& definition : definitions_)
    {
        if (matchesScaleMetadataText (definition.metadata(), query))
            result.push_back (definition);
    }

    return result;
}

ScaleInstance ScaleLibrary::instantiate (std::string_view name, NoteName root) const
{
    if (const auto* definition = findByName (name))
        return ScaleInstance { root, *definition };

    throw std::invalid_argument ("Unknown scale name");
}

void ScaleLibrary::addDefinition (ScaleDefinition definition)
{
    if (findByName (definition.name()) != nullptr)
        throw std::invalid_argument ("Scale library already contains a scale with this name");

    definitions_.push_back (std::move (definition));
}
}
