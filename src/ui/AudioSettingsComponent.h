#pragma once

#include "engine/EngineTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class AudioSettingsComponent final : public juce::Component
{
public:
    explicit AudioSettingsComponent (app::AppServices& appServices);

    void paint (juce::Graphics& graphics) override;
    void resized() override;

    void refresh();

    std::function<void()> onClose;

private:
    void applySelectedOutputDevice();

    app::AppServices& appServices_;
    std::vector<engine::AudioOutputDevice> outputDevices_;

    juce::Label titleLabel_;
    juce::Label outputDeviceLabel_;
    juce::ComboBox outputDeviceCombo_;
    juce::TextButton applyButton_;
    juce::TextButton refreshButton_;
    juce::TextButton closeButton_;
    juce::Label currentDeviceLabel_;
    juce::Label sampleRateLabel_;
    juce::Label bufferSizeLabel_;
    juce::Label messageLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsComponent)
};
}
