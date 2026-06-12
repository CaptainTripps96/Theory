#include "app/TheorySequencerApplication.h"

#include "app/AppServices.h"
#include "ui/MainWindow.h"

namespace tsq::app
{
TheorySequencerApplication::TheorySequencerApplication() = default;

TheorySequencerApplication::~TheorySequencerApplication() = default;

const juce::String TheorySequencerApplication::getApplicationName()
{
    return JUCE_APPLICATION_NAME_STRING;
}

const juce::String TheorySequencerApplication::getApplicationVersion()
{
    return JUCE_APPLICATION_VERSION_STRING;
}

bool TheorySequencerApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void TheorySequencerApplication::initialise (const juce::String& commandLine)
{
    juce::ignoreUnused (commandLine);
    appServices_ = std::make_unique<AppServices>();
    mainWindow_ = std::make_unique<ui::MainWindow> (getApplicationName(), *appServices_);
}

void TheorySequencerApplication::shutdown()
{
    mainWindow_.reset();
    appServices_.reset();
}

void TheorySequencerApplication::systemRequestedQuit()
{
    quit();
}

void TheorySequencerApplication::anotherInstanceStarted (const juce::String& commandLine)
{
    juce::ignoreUnused (commandLine);
}
}
