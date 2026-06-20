#include "ui/TransportComponent.h"

#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "engine/PlaybackEngine.h"

#include <sstream>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff151922 };
const auto outlineColour = juce::Colour { 0xff2a323c };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff9aa7b7 };
const auto accentColour = juce::Colour { 0xff4fb3c8 };

std::string formatEngineStatus (const engine::PlaybackEngineStatus& status)
{
    std::ostringstream text;
    text << status.backendName << " / ";

    if (! status.initialized)
    {
        text << status.message;
        return text.str();
    }

    text << (status.playing ? "Playing" : "Stopped");

    if (! status.audioDeviceName.empty())
        text << " / " << status.audioDeviceName;
    else if (! status.message.empty())
        text << " / " << status.message;

    return text.str();
}

void styleButton (juce::TextButton& button, juce::Colour colour)
{
    button.setColour (juce::TextButton::buttonColourId, colour);
    button.setColour (juce::TextButton::buttonOnColourId, colour.brighter (0.12f));
    button.setColour (juce::TextButton::textColourOffId, textColour);
    button.setColour (juce::TextButton::textColourOnId, textColour);
}
}

TransportComponent::TransportComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    setTitle ("Transport");
    setDescription ("Playback, recording, MIDI input, audio, plugin, and project controls.");

    startButton_.setButtonText ("Start");
    startButton_.setTooltip ("Start playback");
    startButton_.setTitle ("Start Playback");
    startButton_.setDescription ("Starts project playback from the current playhead position.");
    styleButton (startButton_, accentColour.withAlpha (0.38f));
    startButton_.onClick = [this]
    {
        appServices_.startProjectPlayback();
        refreshStatus();
    };
    addAndMakeVisible (startButton_);

    stopButton_.setButtonText ("Stop");
    stopButton_.setTooltip ("Stop playback");
    stopButton_.setTitle ("Stop Playback");
    stopButton_.setDescription ("Stops project playback.");
    styleButton (stopButton_, outlineColour);
    stopButton_.onClick = [this]
    {
        appServices_.stopProjectPlayback();
        refreshStatus();
    };
    addAndMakeVisible (stopButton_);

    returnButton_.setButtonText ("|<");
    returnButton_.setTooltip ("Return to start");
    returnButton_.setTitle ("Return To Start");
    returnButton_.setDescription ("Moves the playback playhead to the start of the project.");
    styleButton (returnButton_, outlineColour);
    returnButton_.onClick = [this]
    {
        appServices_.returnPlaybackToStart();
        refreshStatus();
    };
    addAndMakeVisible (returnButton_);

    loopButton_.setButtonText ("Loop");
    loopButton_.setTooltip ("Loop project playback range");
    loopButton_.setTitle ("Loop Playback");
    loopButton_.setDescription ("Toggles project playback looping.");
    loopButton_.setColour (juce::TextButton::buttonColourId, outlineColour);
    loopButton_.setColour (juce::TextButton::buttonOnColourId, accentColour.withAlpha (0.38f));
    loopButton_.setColour (juce::TextButton::textColourOffId, textColour);
    loopButton_.setColour (juce::TextButton::textColourOnId, textColour);
    loopButton_.onClick = [this]
    {
        appServices_.setPlaybackLoopEnabled (loopButton_.getToggleState());
        refreshStatus();
    };
    addAndMakeVisible (loopButton_);

    midiInputCombo_.setTooltip ("MIDI input device");
    midiInputCombo_.setTitle ("MIDI Input Device");
    midiInputCombo_.setDescription ("Selects the MIDI input device used for recording.");
    midiInputCombo_.onChange = [this]
    {
        const auto devices = appServices_.availableMidiInputDevices();
        const auto selectedIndex = midiInputCombo_.getSelectedId() - 1;
        if (selectedIndex >= 0 && selectedIndex < static_cast<int> (devices.size()))
            appServices_.selectMidiInputDevice (devices[static_cast<std::size_t> (selectedIndex)].identifier);

        refreshStatus();
    };
    addAndMakeVisible (midiInputCombo_);

    recordButton_.setButtonText ("Rec");
    recordButton_.setTooltip ("Record MIDI input into the armed track");
    recordButton_.setTitle ("MIDI Record");
    recordButton_.setDescription ("Records incoming MIDI into the armed track.");
    recordButton_.setColour (juce::TextButton::buttonColourId, outlineColour);
    recordButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colour { 0xffd95d5d });
    recordButton_.setColour (juce::TextButton::textColourOffId, textColour);
    recordButton_.setColour (juce::TextButton::textColourOnId, textColour);
    recordButton_.onClick = [this]
    {
        const auto enabled = appServices_.setMidiRecordingEnabled (recordButton_.getToggleState());
        if (! enabled)
            recordButton_.setToggleState (false, juce::dontSendNotification);

        refreshStatus();
    };
    addAndMakeVisible (recordButton_);

    inputQuantizeButton_.setButtonText ("Q");
    inputQuantizeButton_.setTooltip ("Input quantize to the current piano-roll grid");
    inputQuantizeButton_.setTitle ("Input Quantize");
    inputQuantizeButton_.setDescription ("Quantizes recorded MIDI input to the current piano-roll grid.");
    inputQuantizeButton_.setColour (juce::TextButton::buttonColourId, outlineColour);
    inputQuantizeButton_.setColour (juce::TextButton::buttonOnColourId, accentColour.withAlpha (0.38f));
    inputQuantizeButton_.setColour (juce::TextButton::textColourOffId, textColour);
    inputQuantizeButton_.setColour (juce::TextButton::textColourOnId, textColour);
    inputQuantizeButton_.onClick = [this]
    {
        appServices_.setInputQuantizationEnabled (inputQuantizeButton_.getToggleState());
        refreshStatus();
    };
    addAndMakeVisible (inputQuantizeButton_);

    scaleLockCombo_.setTooltip ("Scale Lock for MIDI recording");
    scaleLockCombo_.setTitle ("Scale Lock");
    scaleLockCombo_.setDescription ("Selects scale-lock behavior for MIDI recording.");
    scaleLockCombo_.addItem ("Lock Off", 1);
    scaleLockCombo_.addItem ("Nearest", 2);
    scaleLockCombo_.addItem ("Up", 3);
    scaleLockCombo_.addItem ("Down", 4);
    scaleLockCombo_.setSelectedId (1, juce::dontSendNotification);
    scaleLockCombo_.onChange = [this]
    {
        switch (scaleLockCombo_.getSelectedId())
        {
            case 2: appServices_.setScaleLockMode (core::sequencing::ScaleLockMode::nearest); break;
            case 3: appServices_.setScaleLockMode (core::sequencing::ScaleLockMode::roundUp); break;
            case 4: appServices_.setScaleLockMode (core::sequencing::ScaleLockMode::roundDown); break;
            default: appServices_.setScaleLockMode (core::sequencing::ScaleLockMode::off); break;
        }

        refreshStatus();
    };
    addAndMakeVisible (scaleLockCombo_);

    projectButton_.setButtonText ("Project");
    projectButton_.setTooltip ("Project menu");
    projectButton_.setTitle ("Project Menu");
    styleButton (projectButton_, outlineColour);
    projectButton_.onClick = [this]
    {
        if (onProjectMenuRequested)
            onProjectMenuRequested();
    };
    addAndMakeVisible (projectButton_);

    audioButton_.setButtonText ("Audio");
    audioButton_.setTooltip ("Audio settings");
    audioButton_.setTitle ("Audio Settings");
    styleButton (audioButton_, outlineColour);
    audioButton_.onClick = [this]
    {
        if (onAudioSettingsRequested)
            onAudioSettingsRequested();
    };
    addAndMakeVisible (audioButton_);

    pluginsButton_.setButtonText ("Plugins");
    pluginsButton_.setTooltip ("Plugin browser");
    pluginsButton_.setTitle ("Plugin Browser");
    styleButton (pluginsButton_, outlineColour);
    pluginsButton_.onClick = [this]
    {
        if (onPluginBrowserRequested)
            onPluginBrowserRequested();
    };
    addAndMakeVisible (pluginsButton_);

    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    statusLabel_.setTitle ("Playback Status");
    statusLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    statusLabel_.setFont (juce::FontOptions { 13.0f });
    addAndMakeVisible (statusLabel_);

    refreshMidiInputDevices();
    refreshStatus();
    startTimerHz (2);
}

void TransportComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (surfaceColour);
    graphics.setColour (outlineColour);
    graphics.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void TransportComponent::resized()
{
    auto bounds = getLocalBounds().reduced (14, 8);

    startButton_.setBounds (bounds.removeFromLeft (76));
    bounds.removeFromLeft (8);
    stopButton_.setBounds (bounds.removeFromLeft (76));
    bounds.removeFromLeft (8);
    returnButton_.setBounds (bounds.removeFromLeft (48));
    bounds.removeFromLeft (8);
    loopButton_.setBounds (bounds.removeFromLeft (78));
    bounds.removeFromLeft (14);
    recordButton_.setBounds (bounds.removeFromLeft (60));
    bounds.removeFromLeft (8);
    inputQuantizeButton_.setBounds (bounds.removeFromLeft (42));
    bounds.removeFromLeft (8);
    scaleLockCombo_.setBounds (bounds.removeFromLeft (112));
    bounds.removeFromLeft (10);
    midiInputCombo_.setBounds (bounds.removeFromLeft (170));
    bounds.removeFromLeft (14);
    audioButton_.setBounds (bounds.removeFromRight (84));
    bounds.removeFromRight (8);
    pluginsButton_.setBounds (bounds.removeFromRight (94));
    bounds.removeFromRight (8);
    projectButton_.setBounds (bounds.removeFromRight (98));
    bounds.removeFromRight (14);
    statusLabel_.setBounds (bounds);
}

void TransportComponent::timerCallback()
{
    refreshStatus();
}

void TransportComponent::refreshStatus()
{
    refreshMidiInputDevices();
    loopButton_.setToggleState (appServices_.isPlaybackLoopEnabled(), juce::dontSendNotification);
    recordButton_.setToggleState (appServices_.midiRecordingEnabled(), juce::dontSendNotification);
    inputQuantizeButton_.setToggleState (appServices_.inputQuantizationEnabled(), juce::dontSendNotification);

    switch (appServices_.scaleLockMode())
    {
        case core::sequencing::ScaleLockMode::nearest: scaleLockCombo_.setSelectedId (2, juce::dontSendNotification); break;
        case core::sequencing::ScaleLockMode::roundUp: scaleLockCombo_.setSelectedId (3, juce::dontSendNotification); break;
        case core::sequencing::ScaleLockMode::roundDown: scaleLockCombo_.setSelectedId (4, juce::dontSendNotification); break;
        case core::sequencing::ScaleLockMode::off: scaleLockCombo_.setSelectedId (1, juce::dontSendNotification); break;
    }

    statusLabel_.setText (formatEngineStatus (appServices_.playbackEngine().getCurrentStatus())
                              + " / "
                              + appServices_.midiRecordingStatusText(),
                          juce::dontSendNotification);
}

void TransportComponent::refreshMidiInputDevices()
{
    const auto devices = appServices_.availableMidiInputDevices();
    const auto selectedIdentifier = appServices_.selectedMidiInputIdentifier();
    std::vector<std::string> fingerprint;
    fingerprint.reserve (devices.size() * 2);
    for (const auto& device : devices)
    {
        fingerprint.push_back (device.identifier);
        fingerprint.push_back (device.displayName);
    }

    if (midiInputDeviceListValid_
        && fingerprint == midiInputDeviceFingerprint_
        && selectedIdentifier == selectedMidiInputIdentifier_)
        return;

    midiInputDeviceFingerprint_ = std::move (fingerprint);
    selectedMidiInputIdentifier_ = selectedIdentifier;
    midiInputDeviceListValid_ = true;

    midiInputCombo_.clear (juce::dontSendNotification);
    if (devices.empty())
    {
        midiInputCombo_.addItem ("No MIDI Input", 1);
        midiInputCombo_.setSelectedId (1, juce::dontSendNotification);
        return;
    }

    auto selectedId = 1;
    auto itemId = 1;
    for (const auto& device : devices)
    {
        midiInputCombo_.addItem (device.displayName, itemId);
        if (device.identifier == selectedIdentifier)
            selectedId = itemId;

        ++itemId;
    }

    midiInputCombo_.setSelectedId (selectedId, juce::dontSendNotification);
}
}
