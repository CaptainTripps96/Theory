#pragma once

#include "core/sequencing/HarmonicContext.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/ExpressionDestinationRegistry.h"
#include "core/time/Tick.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::core::sequencing
{
class MidiClip;
}

namespace tsq::ui
{
class PianoRollComponent final : public juce::Component
{
public:
    explicit PianoRollComponent (app::AppServices& appServices);
    ~PianoRollComponent() override;

    void openClip (std::string trackId, std::string clipId);
    void clearClip();
    void setPlayheadTick (core::time::TickPosition playheadTick);
    bool hasOpenClip() const;
    bool hasSelectedNotes() const;
    bool selectAllNotes();
    bool copySelectedNotes();
    bool pasteCopiedNotes();
    void setExpressionModeEnabled (bool enabled);
    bool expressionModeEnabled() const noexcept;
    void setExpressionReleaseModeEnabled (bool enabled);
    bool expressionReleaseModeEnabled() const noexcept;
    std::vector<std::string> expressionLaneIds() const;
    std::optional<std::string> selectedExpressionLaneId() const;
    bool debugCreateExpressionLane();
    bool debugRenameSelectedExpressionLane (std::string name);
    bool debugEmulateMarqueeSelectFirstReleaseGhost();
    bool debugExpressionKeyPress (char editKey, int arrowKeyCode, bool shiftDown = false);
    bool debugSelectFirstPhraseEnvelope();
    bool debugSelectExpressionLane (std::string laneId);
    bool debugSelectPitchExpressionLane();
    std::size_t debugPitchSlurCount() const;
    std::optional<std::int64_t> debugSelectedPitchSlurTimeTicks() const;
    std::vector<juce::Point<float>> debugFirstPitchSlurTrajectoryPoints() const;
    std::size_t debugVibratoExpressionCount() const;
    bool debugApplyFirstPitchSlurVoiceOverride();
    bool debugApplyFirstVibratoVoiceOverride();
    bool debugPhraseEnvelopeControlsVisible() const;
    bool debugPhraseEnvelopeDecayControlsVisible() const;
    bool debugPhraseEnvelopeReleaseControlsVisible() const;
    bool debugCyclicExpressionControlsVisible() const;
    std::size_t debugCyclicExpressionWaveformCount() const;
    bool debugShowPhraseEnvelopeControls();
    bool debugShowCyclicExpressionControls();
    bool debugSetPhraseEnvelopeAttackStartGesture (std::vector<double> values);
    bool debugAddFirstAvailableExpressionRoute();
    bool debugAddExpressionRouteByStableId (std::string stableId);
    bool debugAddSimpleOscExpressionRoute (std::string parameterId);
    bool debugSetFirstExpressionRouteRange (double outputMin, double outputMax);
    bool debugToggleFirstExpressionRouteEnabled();
    bool debugRemoveFirstExpressionRoute();
    std::vector<std::string> debugExpressionRouteSupportLabels() const;
    bool debugExpressionRouteControlsVisible() const;
    std::size_t debugExpressionRouteCount() const;
    std::size_t debugCyclicExpressionCount() const;
    bool debugSelectNoteIds (std::vector<std::string> noteIds);
    bool debugEmulateSingleClickAtFirstEditableCell();
    bool debugEmulateDoubleClickAtFirstEditableCell();
    bool debugEmulateMarqueeSelectAllVisibleNotes();
    std::optional<juce::Point<float>> debugFirstEditableCellGlobalPoint() const;
    std::optional<std::pair<std::string, std::string>> openClipIds() const;
    std::vector<std::string> selectedNoteIdsForClip (const std::string& trackId, const std::string& clipId) const;
    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    struct ClipSelection
    {
        std::string trackId;
        std::string clipId;
    };

    class RollContentComponent;
    class ExpressionLaneListComponent;
    class PhraseEnvelopeControlPanel;
    class CyclicExpressionControlPanel;
    class ExpressionRoutingPanel;

    app::AppServices& appServices_;
    std::optional<ClipSelection> selectedClip_;
    core::time::TickPosition playheadTick_ {};
    juce::ToggleButton showChromaticButton_;
    juce::TextButton expressionModeButton_;
    juce::Label clipLengthLabel_;
    juce::TextEditor clipLengthBarsEditor_;
    juce::Label clipLengthSeparatorLabel_;
    juce::TextEditor clipLengthBeatsEditor_;
    juce::ToggleButton clipLoopButton_;
    juce::ComboBox gridDivisionCombo_;
    juce::TextButton tupletSettingsButton_;
    juce::TextButton chromaticTransposeButton_;
    juce::TextButton scaleDegreeTransposeButton_;
    juce::Label expressionLaneHeaderLabel_;
    juce::ToggleButton expressionReleaseModeButton_;
    juce::TextButton addExpressionLaneButton_;
    juce::TextEditor expressionLaneNameEditor_;
    juce::ToggleButton expressionLaneEnabledButton_;
    juce::ComboBox expressionLanePolarityCombo_;
    juce::Viewport viewport_;
    RollContentComponent* content_ = nullptr;
    std::unique_ptr<ExpressionLaneListComponent> expressionLaneList_;
    std::unique_ptr<PhraseEnvelopeControlPanel> phraseEnvelopePanel_;
    std::unique_ptr<CyclicExpressionControlPanel> cyclicExpressionPanel_;
    std::unique_ptr<ExpressionRoutingPanel> expressionRoutingPanel_;
    bool chromaticReveal_ = false;
    bool expressionModeEnabled_ = false;
    bool expressionReleaseModeEnabled_ = false;
    bool refreshingClipControls_ = false;
    bool refreshingExpressionLaneControls_ = false;
    std::optional<core::sequencing::ExpressionLaneId> selectedExpressionLaneId_;
    std::optional<core::sequencing::ExpressionClipId> selectedPhraseEnvelopeId_;
    std::optional<core::sequencing::ExpressionState> phraseEnvelopeGestureOriginal_;
    bool expressionControlPrefersCyclic_ = false;
    std::function<void()> toggleChromaticRevealCallback_;
    std::function<void()> expressionSelectionChangedCallback_;

    void toggleExpressionMode();
    void toggleExpressionReleaseMode();
    void setSelectedExpressionLane (core::sequencing::ExpressionLaneId laneId);
    void ensureSelectedExpressionLane();
    void refreshExpressionLaneControls (bool forceTextRefresh = false);
    void createExpressionLane();
    void commitExpressionLaneName();
    void commitExpressionLaneEnabled();
    void commitExpressionLanePolarity();
    const core::sequencing::ExpressionLane* selectedExpressionLane() const;
    const core::sequencing::PhraseEnvelopeClip* selectedPhraseEnvelope() const;
    const core::sequencing::CyclicExpressionClip* selectedCyclicExpression() const;
    const core::sequencing::VibratoExpression* selectedVibratoExpression() const;
    bool expressionControlSwitchAvailable() const;
    bool shouldShowPhraseEnvelopeControls() const;
    bool shouldShowCyclicExpressionControls() const;
    void showPhraseEnvelopeControlView();
    void showCyclicExpressionControlView();
    void setSelectedPhraseEnvelope (std::optional<core::sequencing::ExpressionClipId> envelopeId);
    bool mutateSelectedPhraseEnvelope (const std::function<bool (core::sequencing::PhraseEnvelopeClip&,
                                                                 core::sequencing::ExpressionLanePolarity)>& mutate,
                                       bool commitToUndoStack);
    bool mutateSelectedCyclicExpression (const std::function<bool (core::sequencing::CyclicExpressionClip&)>& mutate,
                                         bool commitToUndoStack);
    bool mutateSelectedVibratoExpression (const std::function<bool (core::sequencing::VibratoExpression&)>& mutate,
                                          bool commitToUndoStack);
    std::vector<core::sequencing::ExpressionDestinationMetadata> destinationMetadataForSelectedTrack() const;
    bool addExpressionRouteToSelectedLane (const core::sequencing::ExpressionDestinationMetadata& metadata);
    bool mutateExpressionRouteOnSelectedLane (const core::sequencing::ExpressionRouteId& routeId,
                                              const std::function<bool (core::sequencing::ExpressionRoute&)>& mutate);
    bool removeExpressionRouteFromSelectedLane (const core::sequencing::ExpressionRouteId& routeId);
    void beginPhraseEnvelopeGesture();
    bool commitPhraseEnvelopeGesture();
    void toggleChromaticReveal();
    const core::sequencing::MidiClip* selectedMidiClip() const;
    void commitClipLengthControls();
    void commitClipLoopToggle();
    void refreshClipControls (bool forceTextRefresh = false);
    void chromaticTransposeToCurrentContext();
    void scaleDegreeTransposeToCurrentContext();
    void refreshGridDivisionCombo();
    void setCurrentGridFromCombo();
    void showTupletSettingsMenu();
    std::optional<core::sequencing::HarmonicContext> targetContextForCurrentClip() const;
    void refreshContentBounds();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
}
