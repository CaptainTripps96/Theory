#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class InspectorComponent final : public juce::Component
{
public:
    explicit InspectorComponent (app::AppServices& appServices);

    void paint (juce::Graphics& graphics) override;

private:
    app::AppServices& appServices_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorComponent)
};
}
