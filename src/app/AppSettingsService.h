#pragma once

#include "app/AppSettings.h"

#include <juce_core/juce_core.h>

#include <string>

namespace tsq::app
{
class AppSettingsService final
{
public:
    AppSettingsService();
    explicit AppSettingsService (juce::File settingsFile);

    AppSettings load() const;
    bool save (const AppSettings& settings) const;

    std::string settingsFilePath() const;
    std::string pluginRegistryCacheFilePath() const;
    std::string pluginScanDeadMansPedalFilePath() const;
    std::string diagnosticsLogFilePath() const;

private:
    static juce::File defaultSettingsFile();

    juce::File settingsFile_;
};
}
