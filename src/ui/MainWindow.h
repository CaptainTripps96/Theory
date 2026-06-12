#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (juce::String title, app::AppServices& appServices);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
}
