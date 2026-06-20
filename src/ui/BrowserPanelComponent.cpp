#include "ui/BrowserPanelComponent.h"

#include "app/AppServices.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "engine/plugins/PluginRegistry.h"
#include "engine/plugins/PluginScanService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <set>
#include <system_error>
#include <utility>

namespace tsq::ui
{
namespace
{
const auto panelColour = juce::Colour { 0xff151a22 };
const auto panelRaisedColour = juce::Colour { 0xff1c2430 };
const auto rowColour = juce::Colour { 0xff202a36 };
const auto rowHoverColour = juce::Colour { 0xff263344 };
const auto selectedRowColour = juce::Colour { 0xff2b3b4a };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff9aa7b7 };
const auto accentColour = juce::Colour { 0xff5bbad5 };
constexpr int maxProjectFiles = 500;

enum FilterId
{
    filterAll = 1,
    filterInstruments = 2,
    filterAudioEffects = 3,
    filterProjectFiles = 4
};

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

std::string lowercase (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char character) {
        return static_cast<char> (std::tolower (character));
    });
    return text;
}

std::string extensionLower (const std::filesystem::path& path)
{
    return lowercase (path.extension().string());
}

bool containsSearch (const std::string& haystack, const std::string& search)
{
    return search.empty() || lowercase (haystack).find (search) != std::string::npos;
}

void hashCombine (std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

bool pluginMatchesFilter (const engine::plugins::PluginDescription& plugin, int filterId)
{
    switch (filterId)
    {
        case filterInstruments: return plugin.isInstrument;
        case filterAudioEffects: return plugin.isAudioEffect;
        case filterAll:
        default: return true;
    }
}

bool pluginMatchesSearch (const engine::plugins::PluginDescription& plugin, const std::string& search)
{
    if (search.empty())
        return true;

    return containsSearch (plugin.name + " " + plugin.manufacturer + " " + plugin.category + " "
                               + plugin.fileOrIdentifier + " " + engine::plugins::stablePluginIdentifier (plugin),
                           search);
}

std::string pluginCapabilityText (const engine::plugins::PluginDescription& plugin)
{
    return engine::plugins::pluginCapabilityDisplayName (engine::plugins::classifyPlugin (plugin));
}

std::string projectFileKindFor (const std::filesystem::path& path, bool isDirectory)
{
    const auto extension = extensionLower (path);
    if (isDirectory && extension == ".vst3")
        return "VST3";

    if (extension == ".mid" || extension == ".midi")
        return "MIDI";

    if (extension == ".wav" || extension == ".aif" || extension == ".aiff" || extension == ".flac"
        || extension == ".mp3" || extension == ".ogg")
    {
        return "Audio";
    }

    return {};
}

bool projectFileMatchesSearch (const auto& file, const std::string& search)
{
    if (search.empty())
        return true;

    return containsSearch (file.kind + " " + file.relativePath + " " + file.displayName + " " + file.detail, search);
}

bool firstPartyDeviceMatchesSearch (const core::devices::FirstPartyDeviceDefinition& device, const std::string& search)
{
    if (search.empty())
        return true;

    return containsSearch (device.name + " " + device.manufacturer + " " + device.shortDescription + " " + device.typeId, search);
}

std::string projectFileKindLabel (const std::string& kind)
{
    if (kind == "Audio")
        return "Audio file";

    if (kind == "MIDI")
        return "MIDI file";

    if (kind == "VST3")
        return "VST3 bundle";

    return kind.empty() ? "File" : kind;
}

std::string formatFileSize (std::uintmax_t bytes)
{
    if (bytes < 1024)
        return std::to_string (bytes) + " B";

    const auto kilobytes = static_cast<double> (bytes) / 1024.0;
    if (kilobytes < 1024.0)
        return (juce::String { kilobytes, 1 } + " KB").toStdString();

    return (juce::String { kilobytes / 1024.0, 1 } + " MB").toStdString();
}

std::string projectFileDetailFor (const std::string& kind,
                                  const std::filesystem::path& path,
                                  bool isDirectory)
{
    auto detail = projectFileKindLabel (kind);

    if (! isDirectory)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size (path, error);
        if (! error)
            detail += " - " + formatFileSize (size);
    }

    return detail;
}

std::filesystem::path resolvedAudioSourcePath (const std::filesystem::path& packagePath,
                                               const core::sequencing::AudioSourceReference& source)
{
    std::filesystem::path sourcePath { source.filePath };
    if (sourcePath.is_absolute() || packagePath.empty())
        return sourcePath;

    return packagePath / sourcePath;
}

std::string projectFileKey (const std::string& kind, const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute (path, error);
    return kind + "|" + (error ? path.lexically_normal().string() : absolute.lexically_normal().string());
}

juce::String formatScanStatus (const engine::plugins::PluginScanStatus& status, int pluginCount)
{
    if (status.running)
    {
        auto text = juce::String { "Scanning " } + juce::String { juce::roundToInt (status.progress * 100.0f) } + "%";
        if (! status.currentItem.empty())
            text += " - " + toJuceString (status.currentItem);
        return text;
    }

    if (! status.message.empty())
        return toJuceString (status.message) + " - " + juce::String { pluginCount } + " cached";

    return juce::String { pluginCount } + " cached";
}

std::pair<std::string, std::string> emptyBrowserMessage (int filterId,
                                                         bool hasSearch,
                                                         bool hasPlugins,
                                                         bool hasProjectFiles,
                                                         const std::string& projectFileStatus)
{
    if (hasSearch)
        return { "No matching items",
                 "Try a broader search or change the browser filter." };

    if (filterId == filterProjectFiles)
    {
        return { hasProjectFiles ? "No matching project files" : "No project files found",
                 projectFileStatus.empty() ? "Save or open a project package to browse project files."
                                           : projectFileStatus };
    }

    if (! hasPlugins && ! hasProjectFiles)
    {
        return { "No plugins or project files",
                 "Scan for VST3 plugins or save/open a project package to browse files." };
    }

    if (! hasPlugins)
    {
        return { "No plugins found",
                 "Click Scan to refresh cached VST3 instruments and effects." };
    }

    return { "No matching items",
             "Change the browser filter to show available plugins or project files." };
}

void styleButton (juce::TextButton& button, bool active)
{
    button.setColour (juce::TextButton::buttonColourId, active ? accentColour.withAlpha (0.30f) : panelRaisedColour);
    button.setColour (juce::TextButton::textColourOffId, active ? textColour : mutedTextColour);
}
}

class BrowserPanelComponent::RowComponent final : public juce::Component
{
public:
    explicit RowComponent (BrowserPanelComponent& owner)
        : owner_ (owner)
    {
    }

    void update (RowItem item, int rowNumber, bool selected)
    {
        item_ = std::move (item);
        rowNumber_ = rowNumber;
        selected_ = selected;
        repaint();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds();

        if (item_.kind == RowKind::section)
        {
            graphics.fillAll (panelColour);
            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
            graphics.drawText (item_.title, bounds.reduced (10, 0), juce::Justification::centredLeft);
            return;
        }

        if (item_.kind == RowKind::message)
        {
            graphics.fillAll (panelColour);
            graphics.setColour (mutedTextColour);
            auto textBounds = bounds.reduced (12, 6);
            graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
            graphics.drawFittedText (item_.title, textBounds.removeFromTop (20), juce::Justification::centred, 1);
            graphics.setFont (juce::FontOptions { 11.0f });
            graphics.drawFittedText (item_.detail, textBounds, juce::Justification::centred, 2);
            return;
        }

        graphics.setColour (selected_ ? selectedRowColour : (isMouseOver() ? rowHoverColour : rowColour));
        graphics.fillRect (bounds);
        graphics.setColour (outlineColour);
        graphics.drawHorizontalLine (bounds.getBottom() - 1, 10.0f, static_cast<float> (std::max (10, bounds.getRight() - 10)));

        auto textBounds = bounds.reduced (10, 7);
        graphics.setColour (textColour);
        graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        graphics.drawFittedText (item_.title, textBounds.removeFromTop (19), juce::Justification::centredLeft, 1);

        graphics.setColour (mutedTextColour);
        graphics.setFont (juce::FontOptions { 11.5f });
        graphics.drawFittedText (item_.detail, textBounds, juce::Justification::centredLeft, 2);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragStarted_ = false;
        owner_.selectBrowserRow (rowNumber_);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragStarted_ || event.getDistanceFromDragStart() < 5)
            return;

        auto payload = owner_.dragPayloadForRow (rowNumber_);
        if (payload.isVoid())
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragStarted_ = true;
            container->startDragging (payload, this);
        }
    }

private:
    BrowserPanelComponent& owner_;
    RowItem item_;
    int rowNumber_ = -1;
    bool selected_ = false;
    bool dragStarted_ = false;
};

BrowserPanelComponent::BrowserPanelComponent (app::AppServices& appServices)
    : appServices_ (appServices),
      browserList_ ("Browser", this),
      scalePaletteComponent_ (appServices)
{
    setTitle ("Browser Panel");
    setDescription ("Right-docked browser for plugins, project files, and scale tools.");

    titleLabel_.setText ("Browser", juce::dontSendNotification);
    titleLabel_.setTitle ("Browser Panel");
    titleLabel_.setJustificationType (juce::Justification::centredLeft);
    titleLabel_.setColour (juce::Label::textColourId, textColour);
    titleLabel_.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
    addAndMakeVisible (titleLabel_);

    pluginsTabButton_.setButtonText ("Plugins");
    pluginsTabButton_.setTooltip ("Show plugins and project files");
    pluginsTabButton_.setTitle ("Plugins tab");
    pluginsTabButton_.setDescription ("Shows cached plugins and project files that can be dragged into the arrangement.");
    pluginsTabButton_.onClick = [this] { setActiveTab (ActiveTab::plugins); };
    addAndMakeVisible (pluginsTabButton_);

    devicesTabButton_.setButtonText ("Devices");
    devicesTabButton_.setTooltip ("Show first-party devices");
    devicesTabButton_.setTitle ("Devices tab");
    devicesTabButton_.setDescription ("Shows native TheorySequencer devices.");
    devicesTabButton_.onClick = [this] { setActiveTab (ActiveTab::devices); };
    addAndMakeVisible (devicesTabButton_);

    scalesTabButton_.setButtonText ("Scales");
    scalesTabButton_.setTooltip ("Show scales");
    scalesTabButton_.setTitle ("Scales tab");
    scalesTabButton_.setDescription ("Shows built-in and custom scales that can be dragged into the scale lane.");
    scalesTabButton_.onClick = [this] { setActiveTab (ActiveTab::scales); };
    addAndMakeVisible (scalesTabButton_);

    filterSelector_.setColour (juce::ComboBox::backgroundColourId, rowColour);
    filterSelector_.setTooltip ("Browser filter");
    filterSelector_.setTitle ("Browser filter");
    filterSelector_.setDescription ("Filters the browser list by all items, instruments, audio effects, or project files.");
    filterSelector_.setColour (juce::ComboBox::outlineColourId, outlineColour);
    filterSelector_.setColour (juce::ComboBox::textColourId, textColour);
    filterSelector_.addItem ("All", filterAll);
    filterSelector_.addItem ("Instruments", filterInstruments);
    filterSelector_.addItem ("Audio Effects", filterAudioEffects);
    filterSelector_.addItem ("Project Files", filterProjectFiles);
    filterSelector_.setSelectedId (filterAll, juce::dontSendNotification);
    filterSelector_.onChange = [this] { applyFilters(); };
    addAndMakeVisible (filterSelector_);

    searchEditor_.setColour (juce::TextEditor::backgroundColourId, rowColour);
    searchEditor_.setTitle ("Browser search");
    searchEditor_.setDescription ("Searches plugin names, manufacturers, identifiers, and project file metadata.");
    searchEditor_.setColour (juce::TextEditor::outlineColourId, outlineColour);
    searchEditor_.setColour (juce::TextEditor::textColourId, textColour);
    searchEditor_.setColour (juce::TextEditor::highlightColourId, accentColour.withAlpha (0.35f));
    searchEditor_.setTextToShowWhenEmpty ("Search", mutedTextColour);
    searchEditor_.onTextChange = [this] { applyFilters(); };
    addAndMakeVisible (searchEditor_);

    scanButton_.setButtonText ("Scan");
    scanButton_.setTooltip ("Scan VST3 plugins");
    scanButton_.setTitle ("Scan VST3 plugins");
    scanButton_.onClick = [this] { startScan(); };
    addAndMakeVisible (scanButton_);

    refreshButton_.setButtonText ("Reload");
    refreshButton_.setTooltip ("Reload browser items");
    refreshButton_.setTitle ("Reload browser items");
    refreshButton_.onClick = [this] { refresh (true); };
    addAndMakeVisible (refreshButton_);

    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    statusLabel_.setTitle ("Browser status");
    statusLabel_.setColour (juce::Label::textColourId, accentColour);
    statusLabel_.setFont (juce::FontOptions { 12.0f });
    addAndMakeVisible (statusLabel_);

    pathsLabel_.setJustificationType (juce::Justification::centredLeft);
    pathsLabel_.setTitle ("Plugin scan paths");
    pathsLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    pathsLabel_.setFont (juce::FontOptions { 11.0f });
    addAndMakeVisible (pathsLabel_);

    browserList_.setRowHeight (58);
    browserList_.setTitle ("Browser item list");
    browserList_.setDescription ("List of draggable plugins and project files.");
    browserList_.setColour (juce::ListBox::backgroundColourId, panelColour);
    browserList_.setColour (juce::ListBox::outlineColourId, outlineColour);
    addAndMakeVisible (browserList_);

    emptyLabel_.setText ("No browser items", juce::dontSendNotification);
    emptyLabel_.setTitle ("Browser empty state");
    emptyLabel_.setJustificationType (juce::Justification::centred);
    emptyLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    emptyLabel_.setFont (juce::FontOptions { 12.0f });
    addChildComponent (emptyLabel_);

    addAndMakeVisible (scalePaletteComponent_);

    updateTabButtons();
    refresh();
}

BrowserPanelComponent::~BrowserPanelComponent()
{
    stopTimer();
}

void BrowserPanelComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (panelColour);
    graphics.setColour (outlineColour);
    graphics.drawRect (getLocalBounds());
}

void BrowserPanelComponent::resized()
{
    auto bounds = getLocalBounds().reduced (10);
    auto header = bounds.removeFromTop (26);
    titleLabel_.setBounds (header.removeFromLeft (80));
    header.removeFromLeft (6);
    pluginsTabButton_.setBounds (header.removeFromLeft (80));
    header.removeFromLeft (6);
    devicesTabButton_.setBounds (header.removeFromLeft (80));
    header.removeFromLeft (6);
    scalesTabButton_.setBounds (header.removeFromLeft (66));

    bounds.removeFromTop (8);
    if (activeTab_ == ActiveTab::scales)
    {
        scalePaletteComponent_.setBounds (bounds);
        return;
    }

    auto filterRow = bounds.removeFromTop (30);
    filterSelector_.setBounds (filterRow.removeFromLeft (118));
    filterRow.removeFromLeft (8);
    searchEditor_.setBounds (filterRow);

    bounds.removeFromTop (8);
    auto actionRow = bounds.removeFromTop (30);
    scanButton_.setBounds (actionRow.removeFromLeft (76));
    actionRow.removeFromLeft (8);
    refreshButton_.setBounds (actionRow.removeFromLeft (82));

    bounds.removeFromTop (6);
    statusLabel_.setBounds (bounds.removeFromTop (20));
    pathsLabel_.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (8);
    browserList_.setBounds (bounds);
    emptyLabel_.setBounds (browserList_.getBounds());
}

void BrowserPanelComponent::refresh (bool forceProjectFileScan)
{
    core::diagnostics::ScopedPerformanceTimer timer { "BrowserPanelComponent::refresh" };

    refreshPluginCache();
    firstPartyDevices_ = core::devices::firstPartyDeviceDefinitions();
    rebuildProjectFiles (forceProjectFileScan);
    applyFilters();
    scalePaletteComponent_.refresh();
    refreshStatus();
}

int BrowserPanelComponent::getNumRows()
{
    return static_cast<int> (rows_.size());
}

void BrowserPanelComponent::paintListBoxItem (int, juce::Graphics&, int, int, bool)
{
}

juce::Component* BrowserPanelComponent::refreshComponentForRow (int rowNumber,
                                                                bool isRowSelected,
                                                                juce::Component* existingComponentToUpdate)
{
    auto* row = dynamic_cast<RowComponent*> (existingComponentToUpdate);
    if (row == nullptr)
        row = new RowComponent (*this);

    if (rowNumber >= 0 && rowNumber < static_cast<int> (rows_.size()))
        row->update (rows_[static_cast<std::size_t> (rowNumber)], rowNumber, isRowSelected);

    return row;
}

void BrowserPanelComponent::selectedRowsChanged (int)
{
    browserList_.repaint();
}

void BrowserPanelComponent::timerCallback()
{
    if (appServices_.pluginScanService().isScanning())
    {
        refreshStatus();
    }
    else
    {
        stopTimer();
        refreshPluginCache();
        applyFilters();
        refreshStatus();
    }
}

void BrowserPanelComponent::setActiveTab (ActiveTab tab)
{
    activeTab_ = tab;
    rowsDirty_ = true;
    applyFilters();
    updateTabButtons();
    resized();
    repaint();
}

void BrowserPanelComponent::updateTabButtons()
{
    const auto showingPlugins = activeTab_ == ActiveTab::plugins;
    const auto showingDevices = activeTab_ == ActiveTab::devices;
    const auto showingBrowser = showingPlugins || showingDevices;

    styleButton (pluginsTabButton_, showingPlugins);
    styleButton (devicesTabButton_, showingDevices);
    styleButton (scalesTabButton_, activeTab_ == ActiveTab::scales);

    filterSelector_.setVisible (showingPlugins);
    searchEditor_.setVisible (showingBrowser);
    scanButton_.setVisible (showingPlugins);
    refreshButton_.setVisible (showingBrowser);
    statusLabel_.setVisible (showingBrowser);
    pathsLabel_.setVisible (showingPlugins);
    browserList_.setVisible (showingBrowser);
    emptyLabel_.setVisible (showingBrowser && rows_.empty());
    scalePaletteComponent_.setVisible (! showingBrowser);
}

void BrowserPanelComponent::startScan()
{
    if (appServices_.pluginScanService().startVst3Scan())
    {
        appServices_.clearUserMessage();
        startTimerHz (4);
    }
    else
    {
        appServices_.reportWarning ("Plugin scan is already running");
    }

    refreshStatus();
}

void BrowserPanelComponent::refreshPluginCache()
{
    core::diagnostics::ScopedPerformanceTimer timer { "BrowserPanelComponent::refreshPluginCache" };
    const auto revision = appServices_.pluginRegistry().revision();
    if (pluginCacheValid_ && pluginRegistryRevision_ == revision)
        return;

    allPlugins_ = appServices_.pluginRegistry().plugins();
    pluginRegistryRevision_ = revision;
    pluginCacheValid_ = true;
    rowsDirty_ = true;
}

void BrowserPanelComponent::applyFilters()
{
    core::diagnostics::ScopedPerformanceTimer timer { "BrowserPanelComponent::applyFilters" };

    const auto previousRow = browserList_.getSelectedRow();
    const auto search = lowercase (searchEditor_.getText().toStdString());
    const auto filterId = filterSelector_.getSelectedId() == 0 ? filterAll : filterSelector_.getSelectedId();
    if (! rowsDirty_ && filteredFilterId_ == filterId && filteredSearch_ == search)
        return;

    filteredPluginIndices_.clear();
    rows_.clear();
    filteredPluginIndices_.reserve (allPlugins_.size());
    rows_.reserve (allPlugins_.size() + projectFiles_.size() + 2);

    if (activeTab_ == ActiveTab::devices)
    {
        auto firstDeviceRow = true;
        for (int index = 0; index < static_cast<int> (firstPartyDevices_.size()); ++index)
        {
            const auto& device = firstPartyDevices_[static_cast<std::size_t> (index)];
            if (! firstPartyDeviceMatchesSearch (device, search))
                continue;

            if (firstDeviceRow)
            {
                rows_.push_back (RowItem { RowKind::section, "Devices", {}, -1, -1, -1 });
                firstDeviceRow = false;
            }

            const auto detail = device.manufacturer + " - Native "
                + (device.kind == core::sequencing::PluginKind::instrument ? "Instrument" : "Device")
                + " - " + device.shortDescription;
            rows_.push_back (RowItem { RowKind::firstPartyDevice, device.name, detail, -1, index, -1 });
        }

        if (rows_.empty())
        {
            rows_.push_back (RowItem {
                RowKind::message,
                search.empty() ? "No first-party devices" : "No matching devices",
                search.empty() ? "Native TheorySequencer devices will appear here." : "Try a broader search.",
                -1,
                -1,
                -1
            });
        }
    }

    if (activeTab_ == ActiveTab::plugins && filterId != filterProjectFiles)
    {
        for (std::size_t index = 0; index < allPlugins_.size(); ++index)
        {
            const auto& plugin = allPlugins_[index];
            if (pluginMatchesFilter (plugin, filterId) && pluginMatchesSearch (plugin, search))
                filteredPluginIndices_.push_back (index);
        }

        if (! filteredPluginIndices_.empty())
            rows_.push_back (RowItem { RowKind::section, "Plugins", {}, -1, -1, -1 });

        for (const auto pluginIndex : filteredPluginIndices_)
        {
            if (pluginIndex >= allPlugins_.size())
                continue;

            const auto& plugin = allPlugins_[pluginIndex];
            const auto title = plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name;
            const auto detail = (plugin.manufacturer.empty() ? std::string { "Unknown maker" } : plugin.manufacturer)
                + " - " + pluginCapabilityText (plugin);
            rows_.push_back (RowItem { RowKind::plugin, title, detail, static_cast<int> (pluginIndex), -1, -1 });
        }
    }

    if (activeTab_ == ActiveTab::plugins && (filterId == filterAll || filterId == filterProjectFiles))
    {
        const auto firstProjectFileRow = static_cast<int> (rows_.size());
        for (int index = 0; index < static_cast<int> (projectFiles_.size()); ++index)
        {
            const auto& file = projectFiles_[static_cast<std::size_t> (index)];
            if (! projectFileMatchesSearch (file, search))
                continue;

            if (static_cast<int> (rows_.size()) == firstProjectFileRow)
                rows_.push_back (RowItem { RowKind::section, "Project Files", {}, -1, -1, -1 });

            const auto detail = file.detail.empty()
                ? file.kind + " - " + file.relativePath
                : file.detail + " - " + file.relativePath;
            rows_.push_back (RowItem { RowKind::projectFile, file.displayName, detail, -1, -1, index });
        }
    }

    if (activeTab_ == ActiveTab::plugins && rows_.empty())
    {
        const auto [title, detail] = emptyBrowserMessage (filterId,
                                                          ! search.empty(),
                                                          ! allPlugins_.empty(),
                                                          ! projectFiles_.empty(),
                                                          projectFileStatus_);
        rows_.push_back (RowItem { RowKind::message, title, detail, -1, -1, -1 });
    }

    browserList_.updateContent();
    if (! rows_.empty())
        browserList_.selectRow (std::clamp (previousRow, 0, static_cast<int> (rows_.size()) - 1), juce::dontSendNotification);
    else
        browserList_.deselectAllRows();

    emptyLabel_.setVisible (rows_.empty());
    updateTabButtons();
    browserList_.repaint();
    refreshStatus();
    filteredFilterId_ = filterId;
    filteredSearch_ = search;
    rowsDirty_ = false;
}

std::size_t BrowserPanelComponent::projectFileSourceFingerprint() const
{
    auto fingerprint = std::size_t {};

    for (const auto& track : appServices_.project().tracks())
    {
        hashCombine (fingerprint, std::hash<std::string> {} (track.id()));

        for (const auto& clip : track.audioClips())
        {
            const auto& source = clip.source();
            hashCombine (fingerprint, std::hash<std::string> {} (clip.id()));
            hashCombine (fingerprint, std::hash<std::string> {} (source.sourceId));
            hashCombine (fingerprint, std::hash<std::string> {} (source.filePath));
            hashCombine (fingerprint, std::hash<std::string> {} (source.displayName));
            hashCombine (fingerprint, source.embeddedInProject ? 1u : 0u);
        }
    }

    return fingerprint;
}

void BrowserPanelComponent::rebuildProjectFiles (bool force)
{
    core::diagnostics::ScopedPerformanceTimer timer { "BrowserPanelComponent::rebuildProjectFiles" };

    const auto currentPackagePath = appServices_.currentProjectPackagePath();
    const auto sourceFingerprint = projectFileSourceFingerprint();
    if (! force
        && projectFilesCacheValid_
        && projectFilesPackagePath_ == currentPackagePath
        && projectFilesSourceFingerprint_ == sourceFingerprint)
    {
        return;
    }

    projectFilesCacheValid_ = false;
    projectFiles_.clear();
    std::set<std::string> seenFiles;

    auto addProjectFile = [&] (ProjectFileEntry entry) {
        if (entry.kind.empty() || entry.absolutePath.empty())
            return;

        const auto key = projectFileKey (entry.kind, std::filesystem::path { entry.absolutePath });
        if (! seenFiles.insert (key).second)
            return;

        projectFiles_.push_back (std::move (entry));
    };

    const auto packagePath = currentPackagePath.value_or (std::filesystem::path {});
    auto packageScanAvailable = false;
    std::string packageStatus;

    if (! currentPackagePath.has_value())
    {
        packageStatus = "No project package";
    }
    else
    {
        std::error_code existsError;
        if (! std::filesystem::exists (packagePath, existsError))
        {
            packageStatus = "Project package not found";
        }
        else
        {
            packageScanAvailable = true;
        }
    }

    std::error_code error;
    if (packageScanAvailable)
    {
        auto iterator = std::filesystem::recursive_directory_iterator {
            packagePath,
            std::filesystem::directory_options::skip_permission_denied,
            error
        };
        const auto end = std::filesystem::recursive_directory_iterator {};

        while (! error && iterator != end && static_cast<int> (projectFiles_.size()) < maxProjectFiles)
        {
            const auto entry = *iterator;
            const auto path = entry.path();
            const auto isDirectory = entry.is_directory (error);
            const auto kind = projectFileKindFor (path, isDirectory);

            if (! kind.empty())
            {
                std::error_code relativeError;
                const auto relative = std::filesystem::relative (path, packagePath, relativeError);
                if (! relativeError)
                {
                    addProjectFile (ProjectFileEntry {
                        kind,
                        relative.generic_string(),
                        path.string(),
                        path.filename().string(),
                        projectFileDetailFor (kind, path, isDirectory)
                    });
                }

                if (isDirectory && extensionLower (path) == ".vst3")
                    iterator.disable_recursion_pending();
            }

            iterator.increment (error);
        }
    }

    for (const auto& track : appServices_.project().tracks())
    {
        for (const auto& clip : track.audioClips())
        {
            const auto& source = clip.source();
            if (! source.isValid())
                continue;

            const auto resolvedPath = resolvedAudioSourcePath (packagePath, source);
            const auto displayName = source.displayName.empty()
                ? std::filesystem::path { source.filePath }.filename().string()
                : source.displayName;
            const auto relativePath = source.embeddedInProject
                ? source.filePath
                : "Referenced Audio/" + (displayName.empty() ? source.sourceId : displayName);

            std::error_code assetError;
            const auto exists = std::filesystem::is_regular_file (resolvedPath, assetError);
            addProjectFile (ProjectFileEntry {
                "Audio",
                relativePath,
                resolvedPath.string(),
                displayName.empty() ? source.sourceId : displayName,
                exists ? projectFileDetailFor ("Audio", resolvedPath, false)
                       : "Missing audio reference"
            });
        }
    }

    std::sort (projectFiles_.begin(), projectFiles_.end(), [] (const auto& lhs, const auto& rhs) {
        if (lhs.kind != rhs.kind)
            return lhs.kind < rhs.kind;
        return lhs.relativePath < rhs.relativePath;
    });

    if (error)
    {
        projectFileStatus_ = "Project file scan stopped";
    }
    else if (static_cast<int> (projectFiles_.size()) >= maxProjectFiles)
    {
        projectFileStatus_ = (juce::String { "Project files: " } + juce::String { maxProjectFiles } + "+").toStdString();
    }
    else
    {
        projectFileStatus_ = "Project files: " + std::to_string (projectFiles_.size());
    }

    if (! packageStatus.empty())
        projectFileStatus_ = packageStatus + " - " + projectFileStatus_;

    projectFilesPackagePath_ = currentPackagePath;
    projectFilesSourceFingerprint_ = sourceFingerprint;
    projectFilesCacheValid_ = true;
    rowsDirty_ = true;
}

void BrowserPanelComponent::refreshStatus()
{
    core::diagnostics::ScopedPerformanceTimer timer { "BrowserPanelComponent::refreshStatus" };

    if (activeTab_ == ActiveTab::devices)
    {
        statusLabel_.setText (juce::String { static_cast<int> (firstPartyDevices_.size()) } + " native devices",
                              juce::dontSendNotification);
        pathsLabel_.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto scanStatus = appServices_.pluginScanService().status();
    scanButton_.setEnabled (! scanStatus.running);

    auto status = formatScanStatus (scanStatus, appServices_.pluginRegistry().pluginCount());
    if (! projectFileStatus_.empty())
        status += " - " + toJuceString (projectFileStatus_);
    if (! appServices_.lastUserMessage().empty())
        status += " - " + toJuceString (std::string { appServices_.lastUserMessage() });

    statusLabel_.setText (status, juce::dontSendNotification);
    pathsLabel_.setText ("Paths: " + toJuceString (scanStatus.searchPaths), juce::dontSendNotification);
}

void BrowserPanelComponent::selectBrowserRow (int rowNumber)
{
    if (rowNumber >= 0 && rowNumber < static_cast<int> (rows_.size()))
        browserList_.selectRow (rowNumber);
}

juce::var BrowserPanelComponent::dragPayloadForRow (int rowNumber) const
{
    core::diagnostics::ScopedPerformanceTimer timer { "BrowserPanelComponent::dragPayloadForRow" };

    if (rowNumber < 0 || rowNumber >= static_cast<int> (rows_.size()))
        return {};

    const auto& row = rows_[static_cast<std::size_t> (rowNumber)];
    if (row.kind == RowKind::plugin
        && row.pluginIndex >= 0
        && row.pluginIndex < static_cast<int> (allPlugins_.size()))
    {
        return makePluginDragPayload (allPlugins_[static_cast<std::size_t> (row.pluginIndex)]);
    }

    if (row.kind == RowKind::firstPartyDevice
        && row.firstPartyDeviceIndex >= 0
        && row.firstPartyDeviceIndex < static_cast<int> (firstPartyDevices_.size()))
    {
        return makeFirstPartyDeviceDragPayload (firstPartyDevices_[static_cast<std::size_t> (row.firstPartyDeviceIndex)]);
    }

    if (row.kind == RowKind::projectFile
        && row.projectFileIndex >= 0
        && row.projectFileIndex < static_cast<int> (projectFiles_.size()))
    {
        const auto& file = projectFiles_[static_cast<std::size_t> (row.projectFileIndex)];
        return makeProjectFileDragPayload (BrowserProjectFileDragPayload {
            file.kind,
            file.relativePath,
            file.absolutePath,
            file.displayName
        });
    }

    return {};
}
}
