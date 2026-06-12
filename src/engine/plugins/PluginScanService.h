#pragma once

#include "engine/plugins/PluginDescription.h"

#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tsq::engine::plugins
{
class PluginRegistry;

struct PluginScanStatus
{
    bool running = false;
    bool completed = false;
    bool failed = false;
    float progress = 0.0f;
    int pluginsFound = 0;
    int instrumentsFound = 0;
    int audioEffectsFound = 0;
    int ambiguousPluginsFound = 0;
    int unsupportedPluginsFound = 0;
    int scanFailures = 0;
    std::string currentItem;
    std::string message;
    std::string searchPaths;
    std::vector<std::string> diagnostics;
};

class PluginScanService final
{
public:
    PluginScanService (PluginRegistry& registry, std::string deadMansPedalFilePath);
    ~PluginScanService();

    bool startVst3Scan();
    bool isScanning() const;
    PluginScanStatus status() const;
    std::string defaultVst3SearchPaths() const;

private:
    void scanVst3();
    void setStatus (PluginScanStatus status);
    void updateStatus (float progress, std::string currentItem, std::string message);
    void joinFinishedWorkerIfNeeded();

    PluginRegistry& registry_;
    std::string deadMansPedalFilePath_;
    mutable std::mutex statusMutex_;
    PluginScanStatus status_;
    std::thread worker_;
};
}
