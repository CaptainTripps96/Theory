#include "ui/StatusBarComponent.h"

#include "app/AppServices.h"

#include <string>
#include <string_view>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff101419 };
const auto outlineColour = juce::Colour { 0xff2a323c };
const auto textColour = juce::Colour { 0xff9aa7b7 };
const auto titleColour = juce::Colour { 0xffedf2f7 };

juce::String toJuceString (std::string_view text)
{
    return juce::String::fromUTF8 (std::string { text }.c_str());
}
}

StatusBarComponent::StatusBarComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    setTitle ("Status Bar");
    setDescription ("Shows build, platform, undo depth, current messages, and the diagnostics log button.");

    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    statusLabel_.setTitle ("Application Status");
    statusLabel_.setColour (juce::Label::textColourId, textColour);
    statusLabel_.setFont (juce::FontOptions { 12.0f });
    addAndMakeVisible (statusLabel_);

    diagnosticsButton_.setButtonText ("Log");
    diagnosticsButton_.setTooltip ("Diagnostics log");
    diagnosticsButton_.setTitle ("Diagnostics Log");
    diagnosticsButton_.setDescription ("Opens the diagnostics log overlay.");
    diagnosticsButton_.setColour (juce::TextButton::buttonColourId, outlineColour);
    diagnosticsButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    diagnosticsButton_.onClick = [this]
    {
        if (onDiagnosticsRequested)
            onDiagnosticsRequested();
    };
    addAndMakeVisible (diagnosticsButton_);

    refresh();
}

void StatusBarComponent::refresh()
{
    auto text = "Build " + toJuceString (appServices_.buildType())
                + " / " + toJuceString (appServices_.platformString())
                + " / Undo " + juce::String (static_cast<int> (appServices_.commandStack().undoDepth()));

    if (! appServices_.lastUserMessage().empty())
        text += " / " + toJuceString (appServices_.lastUserMessage());

    statusLabel_.setText (text, juce::dontSendNotification);
}

void StatusBarComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (surfaceColour);
    graphics.setColour (outlineColour);
    graphics.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
}

void StatusBarComponent::resized()
{
    auto bounds = getLocalBounds().reduced (12, 3);
    diagnosticsButton_.setBounds (bounds.removeFromRight (58));
    bounds.removeFromRight (8);
    statusLabel_.setBounds (bounds);
}
}
