#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class TransportComponent final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit TransportComponent (app::AppServices& appServices);

    std::function<void()> onAudioSettingsRequested;
    std::function<void()> onPluginBrowserRequested;
    std::function<void()> onProjectMenuRequested;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshStatus();
    void refreshMidiInputDevices();

    app::AppServices& appServices_;
    juce::TextButton startButton_;
    juce::TextButton stopButton_;
    juce::TextButton returnButton_;
    juce::ToggleButton loopButton_;
    juce::ComboBox midiInputCombo_;
    juce::ToggleButton recordButton_;
    juce::ToggleButton inputQuantizeButton_;
    juce::ComboBox scaleLockCombo_;
    juce::TextButton projectButton_;
    juce::TextButton audioButton_;
    juce::TextButton pluginsButton_;
    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportComponent)
};
}
