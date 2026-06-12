#pragma once

#include "core/time/Tick.h"
#include "core/commands/Command.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/BorrowedChordAnalysis.h"
#include "core/sequencing/ChordRegion.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/KeyCenterRegion.h"
#include "core/sequencing/AudioClip.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/ScaleModeRegion.h"
#include "ui/WaveformCache.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class TimelineComponent final : public juce::Component,
                                public juce::DragAndDropTarget,
                                public juce::FileDragAndDropTarget
{
public:
    explicit TimelineComponent (app::AppServices& appServices);

    std::function<void (core::time::TickPosition)> onPlayheadMoved;
    std::function<void (std::string, std::string)> onClipOpened;
    std::function<std::vector<std::string> (const std::string&, const std::string&)> selectedNoteIdsForClip;

    void setPlayheadTick (core::time::TickPosition playheadTick);
    bool copySelectionToClipboard();
    bool pasteSelectionFromClipboard();
    bool selectAllInFocusedField();
    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum class ClipKind
    {
        midi,
        audio
    };

    struct ClipSelection
    {
        std::string trackId;
        std::string clipId;
        ClipKind kind = ClipKind::midi;
    };

    enum class DragMode
    {
        none,
        move,
        resizeLeft,
        resizeRight
    };

    enum class StructureLane
    {
        none,
        tempo,
        timeSignature,
        keyCenter,
        scaleMode,
        chordProgression
    };

    enum class StructureRegionKind
    {
        keyCenter,
        scaleMode,
        chordProgression
    };

    struct DragState
    {
        ClipSelection selection;
        DragMode mode = DragMode::none;
        int mouseStartX = 0;
        core::time::TickPosition originalStart {};
        core::time::TickDuration originalLength {};
        core::time::TickPosition previewStart {};
        core::time::TickDuration previewLength {};
        bool previewOverlaps = false;
    };

    struct StructureRegionSelection
    {
        StructureRegionKind kind = StructureRegionKind::keyCenter;
        int index = -1;
    };

    struct StructureDragState
    {
        StructureRegionSelection selection;
        DragMode mode = DragMode::none;
        int mouseStartX = 0;
        core::time::TickPosition originalStart {};
        core::time::TickPosition originalEnd {};
        core::time::TickPosition previewStart {};
        core::time::TickPosition previewEnd {};
    };

    enum class FocusedField
    {
        clips,
        structure,
        automation
    };

    struct MarqueeState
    {
        juce::Point<int> start;
        juce::Point<int> current;
        FocusedField field = FocusedField::clips;
    };

    struct CopiedClip
    {
        std::string sourceTrackId;
        ClipKind kind = ClipKind::midi;
        std::variant<core::sequencing::MidiClip, core::sequencing::AudioClip> clip;
    };

    struct CopiedStructureRegion
    {
        StructureRegionKind kind = StructureRegionKind::keyCenter;
        core::time::TickPosition start {};
        core::time::TickPosition end {};
        core::music_theory::PitchClass keyCenter { core::music_theory::PitchClass::c() };
        std::string scaleDefinitionName;
        std::optional<core::sequencing::ChordRegion> chordRegion;
    };

    struct AutomationLaneHit
    {
        int trackIndex = -1;
        int laneIndex = -1;
        core::sequencing::AutomationTarget target;
    };

    struct AutomationPointSelection
    {
        std::string trackId;
        core::sequencing::AutomationTarget target;
        core::time::TickPosition position {};
    };

    struct AutomationDragState
    {
        AutomationPointSelection selection;
        core::time::TickPosition previewPosition {};
        double previewValue = 0.0;
    };

    app::AppServices& appServices_;
    juce::TextButton addTrackButton_;
    juce::TextButton globalizeChordButton_;
    WaveformCache waveformCache_;
    core::time::TickPosition playheadTick_ {};
    std::optional<ClipSelection> selectedClip_;
    std::vector<ClipSelection> selectedClips_;
    std::optional<StructureRegionSelection> selectedStructureRegion_;
    std::vector<StructureRegionSelection> selectedStructureRegions_;
    std::optional<AutomationPointSelection> selectedAutomationPoint_;
    std::optional<DragState> dragState_;
    std::optional<StructureDragState> structureDragState_;
    std::optional<AutomationDragState> automationDragState_;
    std::optional<MarqueeState> marqueeState_;
    std::optional<core::time::TickPosition> scaleDropPreviewTick_;
    std::vector<CopiedClip> clipClipboard_;
    std::vector<CopiedStructureRegion> structureRegionClipboard_;
    std::string feedbackText_;
    bool draggingPlayhead_ = false;
    bool trackCreationDropPreview_ = false;
    FocusedField focusedField_ = FocusedField::clips;
    int visibleTimelineBars_ = 58;

    void addTrack();
    void showInsertTrackMenu();
    bool isEmptyTrackCreationArea (juce::Point<int> position) const;
    bool createTrackFromPayload (const juce::var& payload);
    void addClipAt (const std::string& trackId, core::time::TickPosition startInProject);
    void deleteSelectedClip();
    void deleteSelectedStructureRegion();
    void deleteSelectedAutomationPoint();
    void copySelection();
    void pasteSelection();
    void copySelectedClips();
    void pasteCopiedClips();
    void copySelectedStructureRegions();
    void pasteCopiedStructureRegions();
    void globalizeSelectedClipChordProgression();
    void handleStructureLaneDoubleClick (StructureLane lane,
                                         core::time::TickPosition position,
                                         std::optional<StructureRegionSelection> selection);
    void addOrEditTempoNode (core::time::TickPosition position);
    void addOrEditTimeSignatureMarker (core::time::TickPosition position);
    void addOrEditKeyCenterRegion (core::time::TickPosition position, std::optional<int> regionIndex);
    void addOrEditScaleModeRegion (core::time::TickPosition position, std::optional<int> regionIndex);
    void addOrReplaceScaleModeRegionFromPalette (const std::string& scaleName, core::time::TickPosition position);
    void showChordRegionPopup (int chordRegionIndex);
    void applyCompatibleScaleSuggestion (int chordRegionIndex, core::sequencing::CompatibleScaleSuggestion suggestion);
    void commitStructureDrag (const StructureDragState& drag);
    void commitAutomationPointDrag (const AutomationDragState& drag);
    void showTrackAutomationMenu (const std::string& trackId);
    void showAutomationLaneForTarget (const std::string& trackId, core::sequencing::AutomationTarget target);
    void hideAutomationLaneForTarget (const std::string& trackId, core::sequencing::AutomationTarget target);
    void setAutomationLaneForTrack (const std::string& trackId, core::sequencing::AutomationLane lane);
    core::sequencing::ChordRegion chordRegionWithBounds (const core::sequencing::ChordRegion& region,
                                                         core::time::TickPosition start,
                                                         core::time::TickPosition end) const;
    void runCommand (std::unique_ptr<core::commands::Command> command);
    void refreshAfterEdit();
    void zoomHorizontally (float wheelDeltaY, int anchorX);

    std::string nextClipId() const;
    juce::Rectangle<int> structureLaneBounds (StructureLane lane) const;
    StructureLane structureLaneAt (juce::Point<float> point) const;
    std::optional<StructureRegionSelection> structureRegionAt (juce::Point<float> point) const;
    std::optional<CopiedStructureRegion> copiedStructureRegionForSelection (StructureRegionSelection selection) const;
    bool isStructureRegionSelected (StructureRegionSelection selection) const;
    bool isClipSelected (const ClipSelection& selection) const;
    std::optional<DragMode> resizeModeAtStructureRegionEdge (juce::Point<float> point) const;
    std::optional<DragMode> resizeModeAtClipEdge (juce::Point<float> point) const;
    juce::Rectangle<int> trackRowForIndex (int index) const;
    juce::Rectangle<int> trackHeaderBoundsForIndex (int index) const;
    juce::Rectangle<int> automationLaneBoundsForHit (AutomationLaneHit hit) const;
    int trackIndexAt (juce::Point<float> point) const;
    std::optional<AutomationLaneHit> automationLaneAt (juce::Point<float> point) const;
    std::optional<AutomationPointSelection> automationPointAt (juce::Point<float> point) const;
    std::optional<ClipSelection> clipAt (juce::Point<float> point) const;
    const core::sequencing::MidiClip* findClip (const ClipSelection& selection) const noexcept;
    const core::sequencing::AudioClip* findAudioClip (const ClipSelection& selection) const noexcept;
    core::time::TickPosition clipStart (const CopiedClip& copied) const noexcept;
    core::time::TickPosition clipEnd (const CopiedClip& copied) const noexcept;
    core::time::TickDuration clipLength (const CopiedClip& copied) const noexcept;
    juce::Rectangle<int> marqueeBounds() const;
    void selectClipsInMarquee (juce::Rectangle<int> bounds);
    void selectStructureRegionsInMarquee (juce::Rectangle<int> bounds);
    bool wouldOverlap (const std::string& trackId,
                       const std::string& ignoredClipId,
                       core::time::TickPosition startInProject,
                       core::time::TickDuration length) const;
    int tickToX (core::time::TickPosition position) const noexcept;
    int durationToWidth (core::time::TickDuration duration) const noexcept;
    core::time::TickPosition xToSnappedTick (int x) const noexcept;
    core::time::TickPosition snappedPosition (std::int64_t ticks) const noexcept;
    core::time::TickDuration snappedDuration (std::int64_t ticks) const noexcept;
    core::time::TickPosition timelineEndTick() const;
    double pixelsPerQuarter() const noexcept;
    void ensureTimelineFits (core::time::TickPosition endPosition);
    void ensureTimelineFitsAllClips();
    core::music_theory::ScaleLibrary scaleLibraryForProject() const;
    juce::String formatBarBeat (core::time::TickPosition position) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineComponent)
};
}
