#include "ui/AudioSettingsComponent.h"

#include "app/AppServices.h"
#include "engine/PlaybackEngine.h"

#include <cmath>
#include <string>
#include <string_view>

namespace tsq::ui
{
namespace
{
const auto titleColour = juce::Colour { 0xfff4f7fb };
const auto subtitleColour = juce::Colour { 0xff9aa4b2 };
const auto accentColour = juce::Colour { 0xff5bbad5 };
const auto panelBackgroundColour = juce::Colour { 0xff171b22 };
const auto panelOutlineColour = juce::Colour { 0xff2b3440 };
const auto fieldBackgroundColour = juce::Colour { 0xff101216 };

juce::String toJuceString (std::string_view text)
{
    return juce::String::fromUTF8 (std::string { text }.c_str());
}

juce::String formatCurrentDevice (const engine::AudioDeviceSettings& settings)
{
    if (settings.outputDeviceName.empty())
        return "Current output: No audio output open";

    if (settings.outputDeviceType.empty())
        return "Current output: " + toJuceString (settings.outputDeviceName);

    return "Current output: "
           + toJuceString (settings.outputDeviceType)
           + " - "
           + toJuceString (settings.outputDeviceName);
}

juce::String formatSampleRate (double sampleRate)
{
    if (sampleRate <= 0.0)
        return "Sample rate: Unavailable";

    return "Sample rate: " + juce::String { static_cast<int> (std::lround (sampleRate)) } + " Hz";
}

juce::String formatBufferSize (int bufferSize)
{
    if (bufferSize <= 0)
        return "Buffer size: Unavailable";

    return "Buffer size: " + juce::String { bufferSize } + " samples";
}
}

AudioSettingsComponent::AudioSettingsComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    setTitle ("Audio Settings");
    setDescription ("Audio output selection and device status panel.");

    titleLabel_.setText ("Audio Settings", juce::dontSendNotification);
    titleLabel_.setTitle ("Audio Settings");
    titleLabel_.setJustificationType (juce::Justification::centredLeft);
    titleLabel_.setColour (juce::Label::textColourId, titleColour);
    titleLabel_.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
    addAndMakeVisible (titleLabel_);

    outputDeviceLabel_.setText ("Output", juce::dontSendNotification);
    outputDeviceLabel_.setTitle ("Output Device Label");
    outputDeviceLabel_.setJustificationType (juce::Justification::centredLeft);
    outputDeviceLabel_.setColour (juce::Label::textColourId, subtitleColour);
    outputDeviceLabel_.setFont (juce::FontOptions { 13.0f });
    addAndMakeVisible (outputDeviceLabel_);

    outputDeviceCombo_.setColour (juce::ComboBox::backgroundColourId, fieldBackgroundColour);
    outputDeviceCombo_.setTooltip ("Audio output device");
    outputDeviceCombo_.setTitle ("Audio Output Device");
    outputDeviceCombo_.setDescription ("Selects the audio output device.");
    outputDeviceCombo_.setColour (juce::ComboBox::outlineColourId, panelOutlineColour);
    outputDeviceCombo_.setColour (juce::ComboBox::focusedOutlineColourId, accentColour.withAlpha (0.72f));
    outputDeviceCombo_.setColour (juce::ComboBox::textColourId, titleColour);
    addAndMakeVisible (outputDeviceCombo_);

    applyButton_.setButtonText ("Apply");
    applyButton_.setTooltip ("Apply selected audio output");
    applyButton_.setTitle ("Apply Audio Output");
    applyButton_.setColour (juce::TextButton::buttonColourId, accentColour.withAlpha (0.26f));
    applyButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    applyButton_.onClick = [this] { applySelectedOutputDevice(); };
    addAndMakeVisible (applyButton_);

    refreshButton_.setButtonText ("Refresh");
    refreshButton_.setTooltip ("Refresh audio devices");
    refreshButton_.setTitle ("Refresh Audio Devices");
    refreshButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    refreshButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    refreshButton_.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshButton_);

    closeButton_.setButtonText ("Close");
    closeButton_.setTooltip ("Close audio settings");
    closeButton_.setTitle ("Close Audio Settings");
    closeButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    closeButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    closeButton_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton_);

    for (auto* label : { &currentDeviceLabel_, &sampleRateLabel_, &bufferSizeLabel_, &messageLabel_ })
    {
        label->setJustificationType (juce::Justification::centredLeft);
        label->setColour (juce::Label::textColourId, subtitleColour);
        label->setFont (juce::FontOptions { 13.0f });
        addAndMakeVisible (*label);
    }

    messageLabel_.setColour (juce::Label::textColourId, accentColour);

    refresh();
}

void AudioSettingsComponent::paint (juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    graphics.setColour (panelBackgroundColour);
    graphics.fillRoundedRectangle (bounds, 8.0f);
    graphics.setColour (panelOutlineColour);
    graphics.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void AudioSettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced (16, 14);
    auto header = bounds.removeFromTop (28);

    closeButton_.setBounds (header.removeFromRight (76));
    header.removeFromRight (8);
    refreshButton_.setBounds (header.removeFromRight (84));
    titleLabel_.setBounds (header);

    bounds.removeFromTop (10);
    auto outputRow = bounds.removeFromTop (32);
    outputDeviceLabel_.setBounds (outputRow.removeFromLeft (72));
    outputRow.removeFromLeft (8);
    applyButton_.setBounds (outputRow.removeFromRight (78));
    outputRow.removeFromRight (8);
    outputDeviceCombo_.setBounds (outputRow);

    bounds.removeFromTop (10);
    currentDeviceLabel_.setBounds (bounds.removeFromTop (20));
    sampleRateLabel_.setBounds (bounds.removeFromTop (20));
    bufferSizeLabel_.setBounds (bounds.removeFromTop (20));
    messageLabel_.setBounds (bounds.removeFromTop (20));
}

void AudioSettingsComponent::refresh()
{
    const auto settings = appServices_.playbackEngine().getAudioDeviceSettings();

    outputDevices_ = appServices_.playbackEngine().getAvailableOutputDevices();
    outputDeviceCombo_.clear (juce::dontSendNotification);

    auto selectedDeviceId = 0;

    for (auto index = 0; index < static_cast<int> (outputDevices_.size()); ++index)
    {
        const auto itemId = index + 1;
        const auto& device = outputDevices_[static_cast<size_t> (index)];
        outputDeviceCombo_.addItem (toJuceString (device.displayName), itemId);

        if (device.deviceType == settings.outputDeviceType && device.deviceName == settings.outputDeviceName)
            selectedDeviceId = itemId;
    }

    const auto hasOutputDevices = ! outputDevices_.empty();
    outputDeviceCombo_.setEnabled (hasOutputDevices);
    applyButton_.setEnabled (hasOutputDevices);

    if (hasOutputDevices)
        outputDeviceCombo_.setSelectedId (selectedDeviceId > 0 ? selectedDeviceId : 1, juce::dontSendNotification);
    else
        outputDeviceCombo_.setText ("No output devices reported", juce::dontSendNotification);

    currentDeviceLabel_.setText (formatCurrentDevice (settings), juce::dontSendNotification);
    sampleRateLabel_.setText (formatSampleRate (settings.sampleRate), juce::dontSendNotification);
    bufferSizeLabel_.setText (formatBufferSize (settings.bufferSize), juce::dontSendNotification);
    messageLabel_.setText (toJuceString (settings.message.empty() ? std::string { "Ready" } : settings.message), juce::dontSendNotification);
}

void AudioSettingsComponent::applySelectedOutputDevice()
{
    const auto selectedDeviceId = outputDeviceCombo_.getSelectedId();

    if (selectedDeviceId <= 0 || selectedDeviceId > static_cast<int> (outputDevices_.size()))
    {
        messageLabel_.setText ("No output device selected", juce::dontSendNotification);
        return;
    }

    const auto& outputDevice = outputDevices_[static_cast<size_t> (selectedDeviceId - 1)];

    if (! appServices_.playbackEngine().setOutputDevice (outputDevice))
    {
        refresh();
        const auto statusMessage = appServices_.playbackEngine().getCurrentStatus().message;
        const auto message = statusMessage.empty() ? std::string { "Audio output change failed" }
                                                   : "Audio output change failed: " + statusMessage;
        appServices_.reportWarning (message);
        messageLabel_.setText (toJuceString (appServices_.lastUserMessage()), juce::dontSendNotification);
        return;
    }

    const auto saved = appServices_.persistAudioSettings();
    refresh();
    messageLabel_.setText (saved ? "Audio settings saved"
                                 : toJuceString (appServices_.lastUserMessage().empty()
                                                    ? std::string_view { "Audio output changed; settings save failed" }
                                                    : appServices_.lastUserMessage()),
                           juce::dontSendNotification);
}
}
