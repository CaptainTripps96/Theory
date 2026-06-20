#pragma once

#include "engine/plugins/PluginDescription.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class PluginBrowserComponent final : public juce::Component,
                                     private juce::Timer,
                                     private juce::ListBoxModel
{
public:
    explicit PluginBrowserComponent (app::AppServices& appServices);
    ~PluginBrowserComponent() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

    void refresh();

    std::function<void()> onClose;

private:
    int getNumRows() override;
    void paintListBoxItem (int rowNumber,
                           juce::Graphics& graphics,
                           int width,
                           int height,
                           bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void timerCallback() override;
    void startScan();
    void applyFilters();
    void assignSelectedPluginToTrack();
    void playTestPhrase();
    void stopTestPhrase();
    void openLoadedPluginEditor();
    void refreshStatus();
    void refreshPlaybackControls();
    void refreshTrackSelector();

    app::AppServices& appServices_;
    std::vector<engine::plugins::PluginDescription> allPlugins_;
    std::vector<std::size_t> filteredPluginIndices_;
    std::uint64_t pluginRegistryRevision_ = 0;
    int filteredFilterId_ = 0;
    std::string filteredSearch_;
    bool pluginCacheValid_ = false;
    bool filtersDirty_ = true;

    juce::Label titleLabel_;
    juce::TextButton scanButton_;
    juce::TextButton refreshButton_;
    juce::TextButton closeButton_;
    juce::ComboBox filterSelector_;
    juce::TextEditor searchEditor_;
    juce::ComboBox trackSelector_;
    juce::TextButton loadInstrumentButton_;
    juce::TextButton playPhraseButton_;
    juce::TextButton stopPhraseButton_;
    juce::TextButton openEditorButton_;
    juce::Label statusLabel_;
    juce::Label searchPathsLabel_;
    juce::Label loadedPluginLabel_;
    juce::ListBox pluginList_;
    juce::Label emptyLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowserComponent)
};
}
