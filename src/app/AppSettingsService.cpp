#include "app/AppSettingsService.h"

#include <algorithm>
#include <utility>

namespace tsq::app
{
namespace
{
constexpr auto rootTag = "TheorySequencerAppSettings";
constexpr auto audioSettingsTag = "AudioSettings";
constexpr auto audioDeviceStateTag = "AudioDeviceState";
constexpr auto testInstrumentTag = "TestInstrument";

std::string toStdString (const juce::String& text)
{
    return text.toStdString();
}
}

AppSettingsService::AppSettingsService()
    : settingsFile_ (defaultSettingsFile())
{
}

AppSettingsService::AppSettingsService (juce::File settingsFile)
    : settingsFile_ (std::move (settingsFile))
{
}

AppSettings AppSettingsService::load() const
{
    AppSettings settings;

    if (! settingsFile_.existsAsFile())
        return settings;

    auto xml = juce::parseXML (settingsFile_);

    if (xml == nullptr || ! xml->hasTagName (rootTag))
        return settings;

    const auto schemaVersion = xml->getIntAttribute ("schemaVersion", 0);

    if (schemaVersion <= 0)
        return settings;

    settings.schemaVersion = std::min (schemaVersion, AppSettings::currentSchemaVersion);

    if (auto* audioSettings = xml->getChildByName (audioSettingsTag))
    {
        settings.outputDeviceType = toStdString (audioSettings->getStringAttribute ("outputDeviceType"));
        settings.outputDeviceName = toStdString (audioSettings->getStringAttribute ("outputDeviceName"));
        settings.sampleRate = audioSettings->getDoubleAttribute ("sampleRate", 0.0);
        settings.bufferSize = audioSettings->getIntAttribute ("bufferSize", 0);
        settings.audioDeviceStateXml = toStdString (audioSettings->getChildElementAllSubText (audioDeviceStateTag, {}));
    }

    if (auto* testInstrument = xml->getChildByName (testInstrumentTag))
    {
        settings.selectedTestInstrumentIdentifier = toStdString (testInstrument->getStringAttribute ("identifier"));
        settings.selectedTestInstrumentName = toStdString (testInstrument->getStringAttribute ("name"));
    }

    return settings;
}

bool AppSettingsService::save (const AppSettings& settings) const
{
    if (! settingsFile_.getParentDirectory().createDirectory())
        return false;

    juce::XmlElement root { rootTag };
    root.setAttribute ("schemaVersion", AppSettings::currentSchemaVersion);

    auto* audioSettings = root.createNewChildElement (audioSettingsTag);
    audioSettings->setAttribute ("outputDeviceType", juce::String::fromUTF8 (settings.outputDeviceType.c_str()));
    audioSettings->setAttribute ("outputDeviceName", juce::String::fromUTF8 (settings.outputDeviceName.c_str()));
    audioSettings->setAttribute ("sampleRate", settings.sampleRate);
    audioSettings->setAttribute ("bufferSize", settings.bufferSize);
    audioSettings->createNewChildElement (audioDeviceStateTag)->addTextElement (juce::String::fromUTF8 (settings.audioDeviceStateXml.c_str()));

    auto* testInstrument = root.createNewChildElement (testInstrumentTag);
    testInstrument->setAttribute ("identifier", juce::String::fromUTF8 (settings.selectedTestInstrumentIdentifier.c_str()));
    testInstrument->setAttribute ("name", juce::String::fromUTF8 (settings.selectedTestInstrumentName.c_str()));

    return root.writeTo (settingsFile_);
}

std::string AppSettingsService::settingsFilePath() const
{
    return toStdString (settingsFile_.getFullPathName());
}

std::string AppSettingsService::pluginRegistryCacheFilePath() const
{
    return toStdString (settingsFile_.getParentDirectory().getChildFile ("plugin-registry.xml").getFullPathName());
}

std::string AppSettingsService::pluginScanDeadMansPedalFilePath() const
{
    return toStdString (settingsFile_.getParentDirectory().getChildFile ("vst3-scan-dead-mans-pedal.txt").getFullPathName());
}

std::string AppSettingsService::diagnosticsLogFilePath() const
{
    return toStdString (settingsFile_.getParentDirectory().getChildFile ("diagnostics.log").getFullPathName());
}

juce::File AppSettingsService::defaultSettingsFile()
{
    auto settingsDirectory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

#if JUCE_MAC
    settingsDirectory = settingsDirectory.getChildFile ("Application Support");
#endif

    return settingsDirectory.getChildFile ("TheorySequencer").getChildFile ("app-settings.xml");
}
}
