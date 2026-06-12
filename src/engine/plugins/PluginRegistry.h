#pragma once

#include "engine/plugins/PluginDescription.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::engine::plugins
{
class PluginRegistry final
{
public:
    explicit PluginRegistry (std::string cacheFilePath);

    std::vector<PluginDescription> plugins() const;
    std::vector<PluginDescription> instruments() const;
    std::vector<PluginDescription> audioEffects() const;
    int pluginCount() const;
    std::uint64_t revision() const;
    std::optional<PluginDescription> findByStableId (std::string_view stableId) const;

    void replaceAll (std::vector<PluginDescription> plugins);
    void clear();

    bool load();
    bool save() const;

    const std::string& cacheFilePath() const noexcept;

private:
    std::string cacheFilePath_;
    mutable std::mutex mutex_;
    std::vector<PluginDescription> plugins_;
    std::uint64_t revision_ = 0;
};
}
