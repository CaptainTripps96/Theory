#include "ui/DetailEditorComponent.h"

#include "core/diagnostics/PerformanceTrace.h"

#include <utility>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff141a22 };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff93a1b0 };
const auto accentColour = juce::Colour { 0xff5bbad5 };
constexpr int headerHeight = 34;

void styleModeButton (juce::TextButton& button, bool active)
{
    button.setColour (juce::TextButton::buttonColourId, active ? accentColour.withAlpha (0.28f) : juce::Colour { 0xff1b222c });
    button.setColour (juce::TextButton::textColourOffId, active ? textColour : mutedTextColour);
    button.setColour (juce::TextButton::textColourOnId, textColour);
}
}

DetailEditorComponent::DetailEditorComponent (app::AppServices& appServices)
    : pianoRollComponent_ (appServices),
      deviceChainComponent_ (appServices)
{
    setTitle ("Lower Detail Editor");
    setDescription ("Lower editor area that switches between the piano roll and the selected track's device chain.");

    pianoRollButton_.setButtonText ("Piano Roll");
    pianoRollButton_.setTooltip ("Show piano roll");
    pianoRollButton_.setTitle ("Show Piano Roll");
    pianoRollButton_.setDescription ("Switches the lower editor to MIDI piano-roll editing.");
    pianoRollButton_.onClick = [this] { showPianoRoll(); };
    addAndMakeVisible (pianoRollButton_);

    deviceChainButton_.setButtonText ("Device Chain");
    deviceChainButton_.setTooltip ("Show device chain");
    deviceChainButton_.setTitle ("Show Device Chain");
    deviceChainButton_.setDescription ("Switches the lower editor to the selected track's device chain.");
    deviceChainButton_.onClick = [this] { showDeviceChain(); };
    addAndMakeVisible (deviceChainButton_);

    addAndMakeVisible (pianoRollComponent_);
    addAndMakeVisible (deviceChainComponent_);
    setMode (Mode::pianoRoll);
}

void DetailEditorComponent::openClip (std::string trackId, std::string clipId)
{
    pianoRollComponent_.openClip (std::move (trackId), std::move (clipId));
    setMode (Mode::pianoRoll);
}

void DetailEditorComponent::clearClip()
{
    pianoRollComponent_.clearClip();
}

void DetailEditorComponent::setPlayheadTick (core::time::TickPosition playheadTick)
{
    pianoRollComponent_.setPlayheadTick (playheadTick);
}

void DetailEditorComponent::refresh()
{
    core::diagnostics::ScopedPerformanceTimer timer { "DetailEditorComponent::refresh" };

    if (mode_ == Mode::pianoRoll)
        pianoRollComponent_.repaint();
    else
        deviceChainComponent_.refresh();

    repaint();
}

bool DetailEditorComponent::toggleMode()
{
    setMode (mode_ == Mode::pianoRoll ? Mode::deviceChain : Mode::pianoRoll);
    return true;
}

bool DetailEditorComponent::showPianoRoll()
{
    setMode (Mode::pianoRoll);
    return true;
}

bool DetailEditorComponent::showDeviceChain()
{
    setMode (Mode::deviceChain);
    return true;
}

bool DetailEditorComponent::hasOpenClip() const
{
    return pianoRollComponent_.hasOpenClip();
}

bool DetailEditorComponent::hasSelectedNotes() const
{
    return pianoRollComponent_.hasSelectedNotes();
}

bool DetailEditorComponent::selectAllNotes()
{
    return pianoRollComponent_.selectAllNotes();
}

bool DetailEditorComponent::copySelectedNotes()
{
    return pianoRollComponent_.copySelectedNotes();
}

bool DetailEditorComponent::pasteCopiedNotes()
{
    return pianoRollComponent_.pasteCopiedNotes();
}

std::optional<std::pair<std::string, std::string>> DetailEditorComponent::openClipIds() const
{
    return pianoRollComponent_.openClipIds();
}

std::vector<std::string> DetailEditorComponent::selectedNoteIdsForClip (const std::string& trackId, const std::string& clipId) const
{
    return pianoRollComponent_.selectedNoteIdsForClip (trackId, clipId);
}

void DetailEditorComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (surfaceColour);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    graphics.setColour (juce::Colour { 0xff10161e });
    graphics.fillRect (header);

    graphics.setColour (outlineColour);
    graphics.drawRect (getLocalBounds());
    graphics.drawHorizontalLine (header.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));
}

void DetailEditorComponent::resized()
{
    auto bounds = getLocalBounds().reduced (1);
    auto header = bounds.removeFromTop (headerHeight - 1).reduced (8, 5);

    pianoRollButton_.setBounds (header.removeFromLeft (98));
    header.removeFromLeft (6);
    deviceChainButton_.setBounds (header.removeFromLeft (112));

    pianoRollComponent_.setBounds (bounds);
    deviceChainComponent_.setBounds (bounds);
}

void DetailEditorComponent::setMode (Mode mode)
{
    mode_ = mode;
    pianoRollComponent_.setVisible (mode_ == Mode::pianoRoll);
    deviceChainComponent_.setVisible (mode_ == Mode::deviceChain);
    if (mode_ == Mode::deviceChain)
        deviceChainComponent_.refresh();

    updateModeButtons();
    resized();
    repaint();
}

void DetailEditorComponent::updateModeButtons()
{
    styleModeButton (pianoRollButton_, mode_ == Mode::pianoRoll);
    styleModeButton (deviceChainButton_, mode_ == Mode::deviceChain);
}
}
