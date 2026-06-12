#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class DiagnosticsComponent final : public juce::Component
{
public:
    explicit DiagnosticsComponent (app::AppServices& appServices);

    void refresh();
    void paint (juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onClose;

private:
    app::AppServices& appServices_;
    juce::Label titleLabel_;
    juce::TextButton refreshButton_;
    juce::TextButton closeButton_;
    juce::TextEditor logEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiagnosticsComponent)
};
}
