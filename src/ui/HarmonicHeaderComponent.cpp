#include "ui/HarmonicHeaderComponent.h"

#include "app/AppServices.h"
#include "core/music_theory/EnharmonicSpelling.h"
#include "core/sequencing/HarmonicContextResolver.h"

#include <sstream>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff1a2029 };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xfff4f7fb };
const auto mutedTextColour = juce::Colour { 0xffa8b3c2 };
const auto accentColour = juce::Colour { 0xff81d6c5 };

std::string keyNameFor (core::music_theory::PitchClass pitchClass)
{
    return core::music_theory::spellPitchClass (pitchClass).toString();
}
}

HarmonicHeaderComponent::HarmonicHeaderComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    contextLabel_.setJustificationType (juce::Justification::centredLeft);
    contextLabel_.setColour (juce::Label::textColourId, textColour);
    contextLabel_.setFont (juce::FontOptions { 54.0f, juce::Font::bold });
    addAndMakeVisible (contextLabel_);

    projectLabel_.setJustificationType (juce::Justification::centredLeft);
    projectLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    projectLabel_.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
    addAndMakeVisible (projectLabel_);

    meterLabel_.setJustificationType (juce::Justification::centredRight);
    meterLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    meterLabel_.setFont (juce::FontOptions { 15.0f });
    addAndMakeVisible (meterLabel_);

    refresh();
}

void HarmonicHeaderComponent::setPlayheadTick (core::time::TickPosition playheadTick)
{
    playheadTick_ = playheadTick;
    refresh();
}

void HarmonicHeaderComponent::paint (juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    graphics.setColour (surfaceColour);
    graphics.fillRoundedRectangle (bounds, 8.0f);
    graphics.setColour (outlineColour);
    graphics.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    graphics.setColour (accentColour.withAlpha (0.24f));
    graphics.fillRoundedRectangle (getLocalBounds().removeFromLeft (5).toFloat(), 3.0f);
}

void HarmonicHeaderComponent::resized()
{
    auto bounds = getLocalBounds().reduced (24, 16);
    auto top = bounds.removeFromTop (24);
    meterLabel_.setBounds (top.removeFromRight (220));
    projectLabel_.setBounds (top);
    bounds.removeFromTop (6);
    contextLabel_.setBounds (bounds);
}

void HarmonicHeaderComponent::refresh()
{
    const core::sequencing::HarmonicContextResolver resolver { appServices_.project().musicalStructure() };
    const auto context = resolver.resolveAt (playheadTick_);
    contextLabel_.setText (keyNameFor (context.keyCenter()) + " " + context.scaleDefinitionName(), juce::dontSendNotification);
    projectLabel_.setText (appServices_.project().name(), juce::dontSendNotification);

    std::ostringstream meterText;
    const auto tempo = appServices_.project().tempoMap().tempoAt (playheadTick_);
    const auto timeSignature = appServices_.project().timeSignatureMap().timeSignatureAt (playheadTick_);
    meterText << static_cast<int> (tempo.bpm()) << " BPM"
              << " / " << timeSignature.numerator()
              << "/" << timeSignature.denominator();
    meterLabel_.setText (meterText.str(), juce::dontSendNotification);
}
}
