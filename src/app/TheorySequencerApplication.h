#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class MainWindow;
}

namespace tsq::app
{
class TheorySequencerApplication final : public juce::JUCEApplication
{
public:
    TheorySequencerApplication();
    ~TheorySequencerApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;

    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

private:
    std::unique_ptr<AppServices> appServices_;
    std::unique_ptr<ui::MainWindow> mainWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheorySequencerApplication)
};
}
