#include "engine/plugins/PluginScanService.h"

#include "engine/plugins/PluginRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace tsq::engine::plugins
{
namespace
{
std::string toStdString (const juce::String& text)
{
    return text.toStdString();
}

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

PluginDescription toPluginDescription (const juce::PluginDescription& source)
{
    PluginDescription plugin;
    plugin.name = toStdString (source.name);
    plugin.manufacturer = toStdString (source.manufacturerName);
    plugin.format = toStdString (source.pluginFormatName);
    plugin.category = toStdString (source.category);
    plugin.version = toStdString (source.version);
    plugin.fileOrIdentifier = toStdString (source.fileOrIdentifier);
    plugin.uniqueIdentifier = toStdString (source.createIdentifierString());
    plugin.uniqueId = source.uniqueId;
    plugin.deprecatedUid = source.deprecatedUid;
    plugin.isInstrument = source.isInstrument;
    plugin.numInputChannels = source.numInputChannels;
    plugin.numOutputChannels = source.numOutputChannels;
    plugin.parametersScanned = false;
    plugin.parameterDiscoveryStatus = "deferred: scan does not instantiate plug-ins";
    normalizePluginMetadata (plugin);
    return plugin;
}

std::vector<PluginDescription> toPluginDescriptions (const juce::Array<juce::PluginDescription>& source)
{
    std::vector<PluginDescription> plugins;
    plugins.reserve (static_cast<size_t> (source.size()));

    for (const auto& plugin : source)
    {
        if (plugin.pluginFormatName == "VST3")
            plugins.push_back (toPluginDescription (plugin));
    }

    return plugins;
}

void accumulatePluginCounts (PluginScanStatus& status, const std::vector<PluginDescription>& plugins)
{
    status.pluginsFound = static_cast<int> (plugins.size());
    status.instrumentsFound = 0;
    status.audioEffectsFound = 0;
    status.ambiguousPluginsFound = 0;
    status.unsupportedPluginsFound = 0;

    for (const auto& plugin : plugins)
    {
        if (plugin.isInstrument)
            ++status.instrumentsFound;
        if (plugin.isAudioEffect)
            ++status.audioEffectsFound;
        if (plugin.hasAmbiguousCapabilities)
            ++status.ambiguousPluginsFound;
        if (plugin.unsupported)
            ++status.unsupportedPluginsFound;
    }
}

std::string scanCompletionMessage (const PluginScanStatus& status, bool saved)
{
    auto message = std::string { saved ? "VST3 scan complete" : "VST3 scan complete; cache save failed" };
    message += ": " + std::to_string (status.instrumentsFound) + " instruments";
    message += ", " + std::to_string (status.audioEffectsFound) + " audio effects";

    if (status.ambiguousPluginsFound > 0)
        message += ", " + std::to_string (status.ambiguousPluginsFound) + " ambiguous";

    if (status.unsupportedPluginsFound > 0)
        message += ", " + std::to_string (status.unsupportedPluginsFound) + " unsupported";

    return message;
}
}

PluginScanService::PluginScanService (PluginRegistry& registry, std::string deadMansPedalFilePath)
    : registry_ (registry),
      deadMansPedalFilePath_ (std::move (deadMansPedalFilePath))
{
    status_.message = "Ready";
    status_.searchPaths = defaultVst3SearchPaths();
}

PluginScanService::~PluginScanService()
{
    if (worker_.joinable())
        worker_.join();
}

bool PluginScanService::startVst3Scan()
{
    joinFinishedWorkerIfNeeded();

    {
        std::lock_guard lock { statusMutex_ };

        if (status_.running)
            return false;

        status_ = PluginScanStatus {};
        status_.running = true;
        status_.message = "Starting VST3 scan";
        status_.searchPaths = defaultVst3SearchPaths();
    }

    worker_ = std::thread ([this] { scanVst3(); });
    return true;
}

bool PluginScanService::isScanning() const
{
    std::lock_guard lock { statusMutex_ };
    return status_.running;
}

PluginScanStatus PluginScanService::status() const
{
    std::lock_guard lock { statusMutex_ };
    return status_;
}

std::string PluginScanService::defaultVst3SearchPaths() const
{
#if JUCE_PLUGINHOST_VST3
    juce::VST3PluginFormat format;
    return toStdString (format.getDefaultLocationsToSearch().toString());
#else
    return {};
#endif
}

void PluginScanService::scanVst3()
{
#if JUCE_PLUGINHOST_VST3
    try
    {
        juce::VST3PluginFormat format;
        const auto searchPaths = format.getDefaultLocationsToSearch();
        const auto searchPathText = toStdString (searchPaths.toString());
        const auto deadMansPedalFile = juce::File::createFileWithoutCheckingPath (toJuceString (deadMansPedalFilePath_));
        const auto candidates = format.searchPathsForPlugins (searchPaths, true, false);

        deadMansPedalFile.getParentDirectory().createDirectory();

        if (candidates.isEmpty())
        {
            registry_.replaceAll ({});
            const auto saved = registry_.save();

            PluginScanStatus finalStatus;
            finalStatus.running = false;
            finalStatus.completed = true;
            finalStatus.failed = ! saved;
            finalStatus.progress = 1.0f;
            finalStatus.pluginsFound = 0;
            finalStatus.searchPaths = searchPathText;
            finalStatus.message = saved ? "No VST3 plug-ins found in search paths" : "No VST3 plug-ins found; cache save failed";
            if (! saved)
                finalStatus.diagnostics.push_back ("Plugin cache could not be saved after empty scan");
            setStatus (std::move (finalStatus));
            return;
        }

        juce::KnownPluginList knownPlugins;
        juce::PluginDirectoryScanner scanner { knownPlugins,
                                               format,
                                               searchPaths,
                                               true,
                                               deadMansPedalFile,
                                               false };
        scanner.setFilesOrIdentifiersToScan (candidates);

        juce::String currentPlugin;
        bool keepScanning = true;

        while (keepScanning)
        {
            keepScanning = scanner.scanNextFile (false, currentPlugin);
            updateStatus (scanner.getProgress(),
                          toStdString (currentPlugin),
                          "Scanning VST3 plug-ins");
        }

        auto plugins = toPluginDescriptions (knownPlugins.getTypes());
        registry_.replaceAll (std::move (plugins));
        const auto saved = registry_.save();
        const auto registrySnapshot = registry_.plugins();

        PluginScanStatus finalStatus;
        finalStatus.running = false;
        finalStatus.completed = true;
        finalStatus.failed = ! saved;
        finalStatus.progress = 1.0f;
        finalStatus.searchPaths = searchPathText;
        accumulatePluginCounts (finalStatus, registrySnapshot);
        finalStatus.message = scanCompletionMessage (finalStatus, saved);
        finalStatus.scanFailures = finalStatus.failed ? 1 : 0;

        if (! saved)
            finalStatus.diagnostics.push_back ("Plugin cache could not be saved: " + registry_.cacheFilePath());
        if (finalStatus.ambiguousPluginsFound > 0)
            finalStatus.diagnostics.push_back ("Some VST3 plug-ins advertise both instrument and audio-effect capabilities");
        if (finalStatus.unsupportedPluginsFound > 0)
            finalStatus.diagnostics.push_back ("Some scanned VST3 plug-ins are not usable as instruments or audio effects");

        setStatus (std::move (finalStatus));
    }
    catch (const std::exception& error)
    {
        auto failedStatus = status();
        failedStatus.running = false;
        failedStatus.completed = true;
        failedStatus.failed = true;
        failedStatus.scanFailures += 1;
        failedStatus.message = std::string { "VST3 scan failed: " } + error.what();
        failedStatus.diagnostics.push_back (failedStatus.message);
        setStatus (std::move (failedStatus));
    }
    catch (...)
    {
        auto failedStatus = status();
        failedStatus.running = false;
        failedStatus.completed = true;
        failedStatus.failed = true;
        failedStatus.scanFailures += 1;
        failedStatus.message = "Unknown VST3 scan error";
        failedStatus.diagnostics.push_back (failedStatus.message);
        setStatus (std::move (failedStatus));
    }
#else
    PluginScanStatus failedStatus;
    failedStatus.running = false;
    failedStatus.completed = true;
    failedStatus.failed = true;
    failedStatus.unsupportedPluginsFound = 1;
    failedStatus.scanFailures = 1;
    failedStatus.message = "VST3 hosting is not enabled in this build";
    failedStatus.diagnostics.push_back (failedStatus.message);
    setStatus (std::move (failedStatus));
#endif
}

void PluginScanService::setStatus (PluginScanStatus status)
{
    std::lock_guard lock { statusMutex_ };
    status_ = std::move (status);
}

void PluginScanService::updateStatus (float progress, std::string currentItem, std::string message)
{
    std::lock_guard lock { statusMutex_ };
    status_.progress = std::clamp (progress, 0.0f, 1.0f);
    status_.currentItem = std::move (currentItem);
    status_.message = std::move (message);
    status_.pluginsFound = registry_.pluginCount();
}

void PluginScanService::joinFinishedWorkerIfNeeded()
{
    if (worker_.joinable() && ! isScanning())
        worker_.join();
}
}
