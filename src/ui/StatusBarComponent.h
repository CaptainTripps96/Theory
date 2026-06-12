#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class StatusBarComponent final : public juce::Component
{
public:
    explicit StatusBarComponent (app::AppServices& appServices);

    void refresh();
    void paint (juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onDiagnosticsRequested;

private:
    app::AppServices& appServices_;
    juce::Label statusLabel_;
    juce::TextButton diagnosticsButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBarComponent)
};
}
