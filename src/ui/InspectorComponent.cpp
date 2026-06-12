#include "ui/InspectorComponent.h"

#include "app/AppServices.h"

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff151a22 };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff9aa7b7 };
}

InspectorComponent::InspectorComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
}

void InspectorComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (surfaceColour);
    graphics.setColour (outlineColour);
    graphics.drawRect (getLocalBounds());

    auto bounds = getLocalBounds().reduced (14, 12);
    graphics.setColour (mutedTextColour);
    graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
    graphics.drawText ("Inspector", bounds.removeFromTop (24), juce::Justification::centredLeft);

    graphics.setColour (textColour);
    graphics.setFont (juce::FontOptions { 14.0f });
    graphics.drawText (appServices_.project().name(), bounds.removeFromTop (28), juce::Justification::centredLeft);
    graphics.setColour (mutedTextColour);
    graphics.drawText ("Tracks: " + juce::String (static_cast<int> (appServices_.project().tracks().size())),
                       bounds.removeFromTop (24),
                       juce::Justification::centredLeft);
}
}
