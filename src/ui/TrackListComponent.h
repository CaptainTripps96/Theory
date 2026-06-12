#pragma once

#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/Routing.h"
#include "engine/EngineTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class TrackListComponent final : public juce::Component,
                                 public juce::DragAndDropTarget
{
public:
    explicit TrackListComponent (app::AppServices& appServices);
    ~TrackListComponent() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void refresh();
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override;

private:
    class TrackHeaderComponent;
    friend class TrackHeaderComponent;

    app::AppServices& appServices_;
    std::vector<std::unique_ptr<TrackHeaderComponent>> rowComponents_;
    std::vector<std::string> rowTrackIds_;
    engine::MeterSnapshot meterSnapshot_;
    std::size_t rowLayoutFingerprint_ = 0;
    int pluginDropPreviewTrackIndex_ = -1;

    bool rebuildRowsIfNeeded();
    void layoutRows();
    void showInsertTrackMenu();
    bool isEmptyTrackCreationArea (juce::Point<int> position) const;
    bool createTrackFromPayload (const juce::var& payload);
    int trackIndexAt (juce::Point<int> position) const;
    juce::Rectangle<int> trackRowForIndex (int index) const;
    void commitMixerStrip (const std::string& trackId, core::sequencing::MixerStrip mixerStrip);
    void commitRouting (const std::string& trackId, core::sequencing::TrackRouting routing);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackListComponent)
};
}
