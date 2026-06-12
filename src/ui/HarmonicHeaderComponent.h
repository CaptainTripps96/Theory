#pragma once

#include "core/time/Tick.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class HarmonicHeaderComponent final : public juce::Component
{
public:
    explicit HarmonicHeaderComponent (app::AppServices& appServices);

    void setPlayheadTick (core::time::TickPosition playheadTick);
    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void refresh();

    app::AppServices& appServices_;
    core::time::TickPosition playheadTick_ {};
    juce::Label contextLabel_;
    juce::Label projectLabel_;
    juce::Label meterLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicHeaderComponent)
};
}
