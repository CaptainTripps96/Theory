#pragma once

#include "core/devices/FirstPartyDeviceRegistry.h"
#include "engine/plugins/PluginDescription.h"
#include "ui/BrowserDragPayload.h"
#include "ui/ScalePaletteComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class BrowserPanelComponent final : public juce::Component,
                                    private juce::Timer,
                                    private juce::ListBoxModel
{
public:
    explicit BrowserPanelComponent (app::AppServices& appServices);
    ~BrowserPanelComponent() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void refresh (bool forceProjectFileScan = false);

private:
    enum class ActiveTab
    {
        plugins,
        devices,
        scales
    };

    enum class RowKind
    {
        section,
        plugin,
        firstPartyDevice,
        projectFile,
        message
    };

    struct ProjectFileEntry
    {
        std::string kind;
        std::string relativePath;
        std::string absolutePath;
        std::string displayName;
        std::string detail;
    };

    struct RowItem
    {
        RowKind kind = RowKind::message;
        std::string title;
        std::string detail;
        int pluginIndex = -1;
        int firstPartyDeviceIndex = -1;
        int projectFileIndex = -1;
    };

    class RowComponent;

    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    juce::Component* refreshComponentForRow (int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void timerCallback() override;

    void setActiveTab (ActiveTab tab);
    void updateTabButtons();
    void startScan();
    void refreshPluginCache();
    void applyFilters();
    void rebuildProjectFiles (bool force = false);
    std::size_t projectFileSourceFingerprint() const;
    void refreshStatus();
    void selectBrowserRow (int rowNumber);
    juce::var dragPayloadForRow (int rowNumber) const;

    app::AppServices& appServices_;
    ActiveTab activeTab_ = ActiveTab::plugins;
    std::vector<engine::plugins::PluginDescription> allPlugins_;
    std::vector<core::devices::FirstPartyDeviceDefinition> firstPartyDevices_;
    std::vector<std::size_t> filteredPluginIndices_;
    std::vector<ProjectFileEntry> projectFiles_;
    std::vector<RowItem> rows_;
    std::string projectFileStatus_;
    std::uint64_t pluginRegistryRevision_ = 0;
    std::optional<std::filesystem::path> projectFilesPackagePath_;
    std::size_t projectFilesSourceFingerprint_ = 0;
    int filteredFilterId_ = 0;
    std::string filteredSearch_;
    bool pluginCacheValid_ = false;
    bool projectFilesCacheValid_ = false;
    bool rowsDirty_ = true;

    juce::Label titleLabel_;
    juce::TextButton pluginsTabButton_;
    juce::TextButton devicesTabButton_;
    juce::TextButton scalesTabButton_;
    juce::ComboBox filterSelector_;
    juce::TextEditor searchEditor_;
    juce::TextButton scanButton_;
    juce::TextButton refreshButton_;
    juce::Label statusLabel_;
    juce::Label pathsLabel_;
    juce::ListBox browserList_;
    juce::Label emptyLabel_;
    ScalePaletteComponent scalePaletteComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanelComponent)
};
}
