#include "ui/PluginBrowserComponent.h"

#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "engine/PlaybackEngine.h"
#include "engine/plugins/PluginRegistry.h"
#include "engine/plugins/PluginScanService.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace tsq::ui
{
namespace
{
const auto titleColour = juce::Colour { 0xfff4f7fb };
const auto subtitleColour = juce::Colour { 0xff9aa4b2 };
const auto accentColour = juce::Colour { 0xff5bbad5 };
const auto panelBackgroundColour = juce::Colour { 0xff171b22 };
const auto panelOutlineColour = juce::Colour { 0xff2b3440 };
const auto fieldBackgroundColour = juce::Colour { 0xff101216 };
const auto rowSelectedColour = juce::Colour { 0xff26313a };

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

enum PluginFilter
{
    all = 1,
    instruments = 2,
    audioEffects = 3
};

std::string lowercase (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char character) {
        return static_cast<char> (std::tolower (character));
    });
    return text;
}

bool pluginMatchesSearch (const engine::plugins::PluginDescription& plugin, const std::string& search)
{
    if (search.empty())
        return true;

    const auto haystack = lowercase (plugin.name + " " + plugin.manufacturer + " "
                                     + plugin.category + " " + plugin.fileOrIdentifier + " "
                                     + engine::plugins::stablePluginIdentifier (plugin));
    return haystack.find (search) != std::string::npos;
}

bool pluginMatchesFilter (const engine::plugins::PluginDescription& plugin, int filterId)
{
    switch (filterId)
    {
        case PluginFilter::instruments: return plugin.isInstrument;
        case PluginFilter::audioEffects: return plugin.isAudioEffect;
        case PluginFilter::all:
        default: return true;
    }
}

juce::String capabilityText (const engine::plugins::PluginDescription& plugin)
{
    return toJuceString (engine::plugins::pluginCapabilityDisplayName (engine::plugins::classifyPlugin (plugin)));
}

juce::String formatStatus (const engine::plugins::PluginScanStatus& status, int registryCount)
{
    if (status.running)
    {
        const auto percent = juce::roundToInt (status.progress * 100.0f);
        auto text = juce::String { "Scanning VST3 - " } + juce::String { percent } + "%";

        if (! status.currentItem.empty())
            text += " - " + toJuceString (status.currentItem);

        return text;
    }

    if (! status.message.empty())
        return toJuceString (status.message) + " - " + juce::String { registryCount } + " cached";

    return juce::String { registryCount } + " cached";
}
}

PluginBrowserComponent::PluginBrowserComponent (app::AppServices& appServices)
    : appServices_ (appServices),
      pluginList_ ("Plugin Browser", this)
{
    setTitle ("Plugin Browser");
    setDescription ("Overlay plugin browser for scanning VST3 plugins, assigning instruments, playing test phrases, and opening plugin editors.");

    titleLabel_.setText ("Plugin Browser", juce::dontSendNotification);
    titleLabel_.setTitle ("Plugin Browser");
    titleLabel_.setJustificationType (juce::Justification::centredLeft);
    titleLabel_.setColour (juce::Label::textColourId, titleColour);
    titleLabel_.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
    addAndMakeVisible (titleLabel_);

    scanButton_.setButtonText ("Scan VST3");
    scanButton_.setTooltip ("Scan VST3 plugins");
    scanButton_.setTitle ("Scan VST3 Plugins");
    scanButton_.setColour (juce::TextButton::buttonColourId, accentColour.withAlpha (0.26f));
    scanButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    scanButton_.onClick = [this] { startScan(); };
    addAndMakeVisible (scanButton_);

    refreshButton_.setButtonText ("Refresh");
    refreshButton_.setTooltip ("Refresh plugin list");
    refreshButton_.setTitle ("Refresh Plugin List");
    refreshButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    refreshButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    refreshButton_.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshButton_);

    closeButton_.setButtonText ("Close");
    closeButton_.setTooltip ("Close plugin browser");
    closeButton_.setTitle ("Close Plugin Browser");
    closeButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    closeButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    closeButton_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton_);

    filterSelector_.setColour (juce::ComboBox::backgroundColourId, fieldBackgroundColour);
    filterSelector_.setTooltip ("Plugin filter");
    filterSelector_.setTitle ("Plugin Filter");
    filterSelector_.setColour (juce::ComboBox::outlineColourId, panelOutlineColour);
    filterSelector_.setColour (juce::ComboBox::textColourId, titleColour);
    filterSelector_.addItem ("All", PluginFilter::all);
    filterSelector_.addItem ("Instruments", PluginFilter::instruments);
    filterSelector_.addItem ("Audio Effects", PluginFilter::audioEffects);
    filterSelector_.setSelectedId (PluginFilter::all, juce::dontSendNotification);
    filterSelector_.onChange = [this] { applyFilters(); };
    addAndMakeVisible (filterSelector_);

    searchEditor_.setColour (juce::TextEditor::backgroundColourId, fieldBackgroundColour);
    searchEditor_.setTitle ("Plugin Search");
    searchEditor_.setDescription ("Searches cached plugins by name, manufacturer, format, category, and identifier.");
    searchEditor_.setColour (juce::TextEditor::outlineColourId, panelOutlineColour);
    searchEditor_.setColour (juce::TextEditor::textColourId, titleColour);
    searchEditor_.setColour (juce::TextEditor::highlightColourId, accentColour.withAlpha (0.35f));
    searchEditor_.setTextToShowWhenEmpty ("Search plugins", subtitleColour);
    searchEditor_.onTextChange = [this] { applyFilters(); };
    addAndMakeVisible (searchEditor_);

    trackSelector_.setColour (juce::ComboBox::backgroundColourId, fieldBackgroundColour);
    trackSelector_.setTooltip ("Target track");
    trackSelector_.setTitle ("Target Track");
    trackSelector_.setColour (juce::ComboBox::outlineColourId, panelOutlineColour);
    trackSelector_.setColour (juce::ComboBox::textColourId, titleColour);
    addAndMakeVisible (trackSelector_);

    loadInstrumentButton_.setButtonText ("Assign to Track");
    loadInstrumentButton_.setTooltip ("Assign selected plugin to track");
    loadInstrumentButton_.setTitle ("Assign Selected Plugin To Track");
    loadInstrumentButton_.setColour (juce::TextButton::buttonColourId, accentColour.withAlpha (0.26f));
    loadInstrumentButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    loadInstrumentButton_.onClick = [this] { assignSelectedPluginToTrack(); };
    addAndMakeVisible (loadInstrumentButton_);

    playPhraseButton_.setButtonText ("Play Test Phrase");
    playPhraseButton_.setTooltip ("Play a test phrase through the selected plugin");
    playPhraseButton_.setTitle ("Play Test Phrase");
    playPhraseButton_.setColour (juce::TextButton::buttonColourId, accentColour.withAlpha (0.20f));
    playPhraseButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    playPhraseButton_.onClick = [this] { playTestPhrase(); };
    addAndMakeVisible (playPhraseButton_);

    stopPhraseButton_.setButtonText ("Stop");
    stopPhraseButton_.setTooltip ("Stop test phrase");
    stopPhraseButton_.setTitle ("Stop Test Phrase");
    stopPhraseButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    stopPhraseButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    stopPhraseButton_.onClick = [this] { stopTestPhrase(); };
    addAndMakeVisible (stopPhraseButton_);

    openEditorButton_.setButtonText ("Open Editor");
    openEditorButton_.setTooltip ("Open loaded plugin editor");
    openEditorButton_.setTitle ("Open Plugin Editor");
    openEditorButton_.setColour (juce::TextButton::buttonColourId, panelOutlineColour);
    openEditorButton_.setColour (juce::TextButton::textColourOffId, titleColour);
    openEditorButton_.onClick = [this] { openLoadedPluginEditor(); };
    addAndMakeVisible (openEditorButton_);

    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    statusLabel_.setTitle ("Plugin Browser Status");
    statusLabel_.setColour (juce::Label::textColourId, accentColour);
    statusLabel_.setFont (juce::FontOptions { 13.0f });
    addAndMakeVisible (statusLabel_);

    searchPathsLabel_.setJustificationType (juce::Justification::centredLeft);
    searchPathsLabel_.setTitle ("Plugin Search Paths");
    searchPathsLabel_.setColour (juce::Label::textColourId, subtitleColour);
    searchPathsLabel_.setFont (juce::FontOptions { 12.0f });
    addAndMakeVisible (searchPathsLabel_);

    loadedPluginLabel_.setJustificationType (juce::Justification::centredLeft);
    loadedPluginLabel_.setTitle ("Loaded Plugin");
    loadedPluginLabel_.setColour (juce::Label::textColourId, subtitleColour);
    loadedPluginLabel_.setFont (juce::FontOptions { 12.5f });
    addAndMakeVisible (loadedPluginLabel_);

    pluginList_.setRowHeight (58);
    pluginList_.setTitle ("Plugin List");
    pluginList_.setDescription ("List of cached VST3 plugins.");
    pluginList_.setColour (juce::ListBox::backgroundColourId, fieldBackgroundColour);
    pluginList_.setColour (juce::ListBox::outlineColourId, panelOutlineColour);
    addAndMakeVisible (pluginList_);

    emptyLabel_.setText ("No VST3 plug-ins cached", juce::dontSendNotification);
    emptyLabel_.setTitle ("No Cached Plugins");
    emptyLabel_.setJustificationType (juce::Justification::centred);
    emptyLabel_.setColour (juce::Label::textColourId, subtitleColour);
    emptyLabel_.setFont (juce::FontOptions { 13.0f });
    addChildComponent (emptyLabel_);

    refresh();
}

PluginBrowserComponent::~PluginBrowserComponent()
{
    stopTimer();
}

void PluginBrowserComponent::paint (juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    graphics.setColour (panelBackgroundColour);
    graphics.fillRoundedRectangle (bounds, 8.0f);
    graphics.setColour (panelOutlineColour);
    graphics.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void PluginBrowserComponent::resized()
{
    auto bounds = getLocalBounds().reduced (16, 14);
    auto header = bounds.removeFromTop (32);

    closeButton_.setBounds (header.removeFromRight (76));
    header.removeFromRight (8);
    refreshButton_.setBounds (header.removeFromRight (84));
    header.removeFromRight (8);
    scanButton_.setBounds (header.removeFromRight (104));
    titleLabel_.setBounds (header);

    bounds.removeFromTop (8);
    statusLabel_.setBounds (bounds.removeFromTop (22));
    searchPathsLabel_.setBounds (bounds.removeFromTop (22));
    bounds.removeFromTop (8);
    auto filterRow = bounds.removeFromTop (32);
    filterSelector_.setBounds (filterRow.removeFromLeft (150));
    filterRow.removeFromLeft (8);
    searchEditor_.setBounds (filterRow);

    bounds.removeFromTop (8);

    if (bounds.getWidth() < 560)
    {
        auto controlRow = bounds.removeFromTop (32);
        trackSelector_.setBounds (controlRow);

        bounds.removeFromTop (6);
        controlRow = bounds.removeFromTop (32);
        loadInstrumentButton_.setBounds (controlRow);

        bounds.removeFromTop (6);
        controlRow = bounds.removeFromTop (32);
        auto leftButton = controlRow.removeFromLeft ((controlRow.getWidth() - 8) / 2);
        controlRow.removeFromLeft (8);
        playPhraseButton_.setBounds (leftButton);
        stopPhraseButton_.setBounds (controlRow);

        bounds.removeFromTop (6);
        openEditorButton_.setBounds (bounds.removeFromTop (32));
    }
    else
    {
        auto controlRow = bounds.removeFromTop (32);
        openEditorButton_.setBounds (controlRow.removeFromRight (108));
        controlRow.removeFromRight (8);
        stopPhraseButton_.setBounds (controlRow.removeFromRight (76));
        controlRow.removeFromRight (8);
        playPhraseButton_.setBounds (controlRow.removeFromRight (132));
        controlRow.removeFromRight (8);
        loadInstrumentButton_.setBounds (controlRow.removeFromRight (132));
        controlRow.removeFromRight (8);
        trackSelector_.setBounds (controlRow.removeFromLeft (180));
    }

    bounds.removeFromTop (6);
    loadedPluginLabel_.setBounds (bounds.removeFromTop (22));
    bounds.removeFromTop (8);
    pluginList_.setBounds (bounds);
    emptyLabel_.setBounds (pluginList_.getBounds());
}

void PluginBrowserComponent::refresh()
{
    core::diagnostics::ScopedPerformanceTimer timer { "PluginBrowserComponent::refresh" };

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "PluginBrowserComponent::refresh plugin-cache" };
        const auto revision = appServices_.pluginRegistry().revision();
        if (! pluginCacheValid_ || pluginRegistryRevision_ != revision)
        {
            allPlugins_ = appServices_.pluginRegistry().plugins();
            pluginRegistryRevision_ = revision;
            pluginCacheValid_ = true;
            filtersDirty_ = true;
        }
    }

    applyFilters();

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "PluginBrowserComponent::refresh track-selector" };
        refreshTrackSelector();
    }

    refreshStatus();
}

int PluginBrowserComponent::getNumRows()
{
    return static_cast<int> (filteredPluginIndices_.size());
}

void PluginBrowserComponent::paintListBoxItem (int rowNumber,
                                               juce::Graphics& graphics,
                                               int width,
                                               int height,
                                               bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int> (filteredPluginIndices_.size()))
        return;

    const auto pluginIndex = filteredPluginIndices_[static_cast<size_t> (rowNumber)];
    if (pluginIndex >= allPlugins_.size())
        return;

    const auto& plugin = allPlugins_[pluginIndex];
    const auto rowBounds = juce::Rectangle<int> { 0, 0, width, height };
    const auto contentBounds = rowBounds.reduced (12, 7);

    graphics.setColour (rowIsSelected ? rowSelectedColour : panelBackgroundColour);
    graphics.fillRect (rowBounds);
    graphics.setColour (panelOutlineColour);
    graphics.drawHorizontalLine (height - 1, 12.0f, static_cast<float> (std::max (12, width - 12)));

    auto textBounds = contentBounds;
    auto titleBounds = textBounds.removeFromTop (20);

    graphics.setColour (titleColour);
    graphics.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
    graphics.drawFittedText (toJuceString (plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name),
                             titleBounds,
                             juce::Justification::centredLeft,
                             1);

    std::ostringstream details;
    details << (plugin.manufacturer.empty() ? "Unknown maker" : plugin.manufacturer)
            << " - " << (plugin.format.empty() ? "VST3" : plugin.format)
            << " - " << capabilityText (plugin).toStdString();

    if (! plugin.parameters.empty())
        details << " - " << plugin.parameters.size() << " params";

    graphics.setColour (subtitleColour);
    graphics.setFont (juce::FontOptions { 12.0f });
    graphics.drawFittedText (toJuceString (details.str()),
                             textBounds.removeFromTop (17),
                             juce::Justification::centredLeft,
                             1);

    graphics.drawFittedText (toJuceString (plugin.fileOrIdentifier),
                             textBounds,
                             juce::Justification::centredLeft,
                             1);
}

void PluginBrowserComponent::selectedRowsChanged (int)
{
    refreshStatus();
}

void PluginBrowserComponent::timerCallback()
{
    if (appServices_.pluginScanService().isScanning())
    {
        refreshStatus();
    }
    else
    {
        stopTimer();
        refresh();
    }
}

void PluginBrowserComponent::startScan()
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

void PluginBrowserComponent::applyFilters()
{
    core::diagnostics::ScopedPerformanceTimer timer { "PluginBrowserComponent::applyFilters" };

    const auto previousRow = pluginList_.getSelectedRow();
    const auto search = lowercase (searchEditor_.getText().toStdString());
    const auto filterId = filterSelector_.getSelectedId() == 0 ? PluginFilter::all : filterSelector_.getSelectedId();
    if (! filtersDirty_ && filteredFilterId_ == filterId && filteredSearch_ == search)
        return;

    filteredPluginIndices_.clear();
    filteredPluginIndices_.reserve (allPlugins_.size());
    for (std::size_t index = 0; index < allPlugins_.size(); ++index)
    {
        const auto& plugin = allPlugins_[index];
        if (pluginMatchesFilter (plugin, filterId) && pluginMatchesSearch (plugin, search))
            filteredPluginIndices_.push_back (index);
    }

    pluginList_.updateContent();
    if (! filteredPluginIndices_.empty())
        pluginList_.selectRow (std::clamp (previousRow, 0, static_cast<int> (filteredPluginIndices_.size()) - 1), juce::dontSendNotification);
    else
        pluginList_.deselectAllRows();

    pluginList_.repaint();
    emptyLabel_.setVisible (filteredPluginIndices_.empty());
    refreshStatus();
    filteredFilterId_ = filterId;
    filteredSearch_ = search;
    filtersDirty_ = false;
}

void PluginBrowserComponent::assignSelectedPluginToTrack()
{
    const auto selectedRow = pluginList_.getSelectedRow();

    if (selectedRow < 0 || selectedRow >= static_cast<int> (filteredPluginIndices_.size()))
    {
        statusLabel_.setText ("Select a VST3 instrument first", juce::dontSendNotification);
        refreshPlaybackControls();
        return;
    }

    const auto pluginIndex = filteredPluginIndices_[static_cast<size_t> (selectedRow)];
    if (pluginIndex >= allPlugins_.size())
    {
        statusLabel_.setText ("Selected plugin is no longer available", juce::dontSendNotification);
        refreshPlaybackControls();
        return;
    }

    const auto& plugin = allPlugins_[pluginIndex];
    const auto selectedTrackIndex = trackSelector_.getSelectedItemIndex();
    const auto& tracks = appServices_.project().tracks();

    if (selectedTrackIndex < 0 || selectedTrackIndex >= static_cast<int> (tracks.size()))
    {
        statusLabel_.setText ("Select a track first", juce::dontSendNotification);
        refreshPlaybackControls();
        return;
    }

    const auto& trackId = tracks[static_cast<std::size_t> (selectedTrackIndex)].id();
    auto assigned = false;
    if (plugin.isInstrument)
        assigned = appServices_.assignInstrumentToTrack (trackId, plugin);
    else if (plugin.isAudioEffect)
        assigned = appServices_.addPluginDeviceToTrack (trackId, plugin, core::sequencing::PluginKind::audioEffect);

    if (! assigned)
    {
        const auto testStatus = appServices_.playbackEngine().getTestInstrumentStatus();
        const auto fallback = testStatus.message.empty() ? std::string { "Plugin assignment failed" } : testStatus.message;
        statusLabel_.setText (toJuceString (appServices_.lastUserMessage().empty() ? fallback : std::string { appServices_.lastUserMessage() }),
                              juce::dontSendNotification);
    }

    refreshStatus();
}

void PluginBrowserComponent::playTestPhrase()
{
    if (appServices_.playbackEngine().playTestPhrase())
    {
        appServices_.logger().info ("Test phrase playback started");
    }
    else
    {
        const auto testStatus = appServices_.playbackEngine().getTestInstrumentStatus();
        statusLabel_.setText (toJuceString (testStatus.message.empty() ? "Test phrase playback failed" : testStatus.message),
                              juce::dontSendNotification);
    }

    refreshStatus();
}

void PluginBrowserComponent::stopTestPhrase()
{
    appServices_.playbackEngine().stopTestPhrase();
    appServices_.logger().info ("Test phrase playback stopped");
    refreshStatus();
}

void PluginBrowserComponent::openLoadedPluginEditor()
{
    if (appServices_.playbackEngine().openLoadedPluginEditor())
    {
        appServices_.logger().info ("Plugin editor open requested");
    }
    else
    {
        const auto testStatus = appServices_.playbackEngine().getTestInstrumentStatus();
        statusLabel_.setText (toJuceString (testStatus.message.empty() ? "Plugin editor is not available" : testStatus.message),
                              juce::dontSendNotification);
    }

    refreshStatus();
}

void PluginBrowserComponent::refreshStatus()
{
    core::diagnostics::ScopedPerformanceTimer timer { "PluginBrowserComponent::refreshStatus" };

    const auto status = appServices_.pluginScanService().status();
    const auto registryCount = appServices_.pluginRegistry().pluginCount();
    scanButton_.setEnabled (! status.running);

    auto statusText = formatStatus (status, registryCount);
    const auto testStatus = appServices_.playbackEngine().getTestInstrumentStatus();

    if (! status.running && ! testStatus.message.empty())
        statusText += " - " + toJuceString (testStatus.message);

    if (! appServices_.lastUserMessage().empty())
        statusText += " - " + toJuceString (std::string { appServices_.lastUserMessage() });

    statusLabel_.setText (statusText, juce::dontSendNotification);
    searchPathsLabel_.setText ("Paths: " + toJuceString (status.searchPaths), juce::dontSendNotification);
    refreshPlaybackControls();
}

void PluginBrowserComponent::refreshPlaybackControls()
{
    const auto scanStatus = appServices_.pluginScanService().status();
    const auto testStatus = appServices_.playbackEngine().getTestInstrumentStatus();
    const auto selectedRow = pluginList_.getSelectedRow();
    const auto hasSelection = selectedRow >= 0 && selectedRow < static_cast<int> (filteredPluginIndices_.size())
        && filteredPluginIndices_[static_cast<std::size_t> (selectedRow)] < allPlugins_.size();
    const auto hasTrackSelection = trackSelector_.getSelectedItemIndex() >= 0;
    const auto* selectedPlugin = hasSelection
        ? &allPlugins_[filteredPluginIndices_[static_cast<std::size_t> (selectedRow)]]
        : nullptr;
    const auto selectedPluginCanAssign = selectedPlugin != nullptr
        && (selectedPlugin->isInstrument || selectedPlugin->isAudioEffect);

    trackSelector_.setEnabled (! appServices_.project().tracks().empty());
    loadInstrumentButton_.setEnabled (! scanStatus.running && selectedPluginCanAssign && hasTrackSelection);
    if (selectedPlugin != nullptr && selectedPlugin->isAudioEffect && ! selectedPlugin->isInstrument)
        loadInstrumentButton_.setButtonText ("Add Effect");
    else
        loadInstrumentButton_.setButtonText ("Assign to Track");
    playPhraseButton_.setEnabled (testStatus.pluginLoaded && testStatus.phraseReady);
    stopPhraseButton_.setEnabled (testStatus.pluginLoaded || appServices_.playbackEngine().isPlaying());
    openEditorButton_.setEnabled (testStatus.pluginLoaded && testStatus.pluginEditorSupported);

    if (testStatus.pluginLoaded)
    {
        loadedPluginLabel_.setText ("Loaded: " + toJuceString (testStatus.pluginName), juce::dontSendNotification);
        return;
    }

    const auto& settings = appServices_.appSettings();

    if (settings.hasSelectedTestInstrument())
        loadedPluginLabel_.setText ("Loaded: None - Last selected: " + toJuceString (settings.selectedTestInstrumentName),
                                    juce::dontSendNotification);
    else
        loadedPluginLabel_.setText ("Loaded: None", juce::dontSendNotification);
}

void PluginBrowserComponent::refreshTrackSelector()
{
    const auto previousTrackId = trackSelector_.getSelectedId();
    trackSelector_.clear (juce::dontSendNotification);

    const auto& tracks = appServices_.project().tracks();
    for (int index = 0; index < static_cast<int> (tracks.size()); ++index)
        trackSelector_.addItem (toJuceString (tracks[static_cast<std::size_t> (index)].name()), index + 1);

    if (tracks.empty())
        return;

    if (previousTrackId > 0 && previousTrackId <= static_cast<int> (tracks.size()))
        trackSelector_.setSelectedId (previousTrackId, juce::dontSendNotification);
    else
        trackSelector_.setSelectedId (1, juce::dontSendNotification);
}
}
