#include "ui/DiagnosticsComponent.h"

#include "app/AppServices.h"

#include <string>
#include <vector>

namespace tsq::ui
{
namespace
{
const auto titleColour = juce::Colour { 0xfff4f7fb };
const auto textColour = juce::Colour { 0xffd9e2ec };
const auto subtitleColour = juce::Colour { 0xff9aa4b2 };
const auto panelBackgroundColour = juce::Colour { 0xff171b22 };
const auto panelOutlineColour = juce::Colour { 0xff2b3440 };
const auto fieldBackgroundColour = juce::Colour { 0xff101216 };

juce::String linesToText (const std::vector<std::string>& lines)
{
    juce::String text;
    for (const auto& line : lines)
    {
        text += juce::String::fromUTF8 (line.c_str());
        text += "\n";
    }

    return text;
}
}

DiagnosticsComponent::DiagnosticsComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    setTitle ("Diagnostics");
    setDescription ("Diagnostics log overlay with refresh and close controls.");

    titleLabel_.setText ("Diagnostics", juce::dontSendNotification);
    titleLabel_.setTitle ("Diagnostics");
    titleLabel_.setJustificationType (juce::Justification::centredLeft);
    titleLabel_.setColour (juce::Label::textColourId, titleColour);
    titleLabel_.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
    addAndMakeVisible (titleLabel_);

    refreshButton_.setButtonText ("Refresh");
    refreshButton_.setTooltip ("Refresh diagnostics log");
    refreshButton_.setTitle ("Refresh Diagnostics");
    refreshButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    refreshButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    refreshButton_.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshButton_);

    closeButton_.setButtonText ("Close");
    closeButton_.setTooltip ("Close diagnostics");
    closeButton_.setTitle ("Close Diagnostics");
    closeButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    closeButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    closeButton_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton_);

    logEditor_.setMultiLine (true);
    logEditor_.setTitle ("Diagnostics Log");
    logEditor_.setDescription ("Read-only diagnostics log for the current app session.");
    logEditor_.setReadOnly (true);
    logEditor_.setScrollbarsShown (true);
    logEditor_.setCaretVisible (false);
    logEditor_.setPopupMenuEnabled (true);
    logEditor_.setColour (juce::TextEditor::backgroundColourId, fieldBackgroundColour);
    logEditor_.setColour (juce::TextEditor::outlineColourId, panelOutlineColour);
    logEditor_.setColour (juce::TextEditor::focusedOutlineColourId, panelOutlineColour);
    logEditor_.setColour (juce::TextEditor::textColourId, textColour);
    logEditor_.setColour (juce::TextEditor::highlightColourId, subtitleColour.withAlpha (0.35f));
    logEditor_.setFont (juce::FontOptions { 12.0f });
    addAndMakeVisible (logEditor_);

    refresh();
}

void DiagnosticsComponent::refresh()
{
    logEditor_.setText (linesToText (appServices_.diagnosticLines()), false);
    logEditor_.moveCaretToEnd();
}

void DiagnosticsComponent::paint (juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    graphics.setColour (panelBackgroundColour);
    graphics.fillRoundedRectangle (bounds, 8.0f);
    graphics.setColour (panelOutlineColour);
    graphics.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void DiagnosticsComponent::resized()
{
    auto bounds = getLocalBounds().reduced (16, 14);
    auto header = bounds.removeFromTop (28);

    closeButton_.setBounds (header.removeFromRight (76));
    header.removeFromRight (8);
    refreshButton_.setBounds (header.removeFromRight (84));
    titleLabel_.setBounds (header);

    bounds.removeFromTop (10);
    logEditor_.setBounds (bounds);
}
}
