#include "ui/MainWindow.h"

#include "ui/MainComponent.h"

#include <utility>

namespace tsq::ui
{
MainWindow::MainWindow (juce::String title, app::AppServices& appServices)
    : juce::DocumentWindow (std::move (title),
                            juce::Colour { 0xff101216 },
                            juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (800, 500, 3200, 2200);
    setContentOwned (new MainComponent (appServices), true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void MainWindow::closeButtonPressed()
{
    if (auto* application = juce::JUCEApplication::getInstance())
        application->systemRequestedQuit();
}
}
