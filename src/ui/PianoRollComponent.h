#pragma once

#include "core/sequencing/HarmonicContext.h"
#include "core/time/Tick.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
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

    void openClip (std::string trackId, std::string clipId);
    void clearClip();
    void setPlayheadTick (core::time::TickPosition playheadTick);
    bool hasOpenClip() const;
    bool hasSelectedNotes() const;
    bool selectAllNotes();
    bool copySelectedNotes();
    bool pasteCopiedNotes();
    bool debugEmulateSingleClickAtFirstEditableCell();
    bool debugEmulateDoubleClickAtFirstEditableCell();
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

    app::AppServices& appServices_;
    std::optional<ClipSelection> selectedClip_;
    core::time::TickPosition playheadTick_ {};
    juce::ToggleButton showChromaticButton_;
    juce::Label clipLengthLabel_;
    juce::TextEditor clipLengthBarsEditor_;
    juce::Label clipLengthSeparatorLabel_;
    juce::TextEditor clipLengthBeatsEditor_;
    juce::ToggleButton clipLoopButton_;
    juce::ComboBox gridDivisionCombo_;
    juce::TextButton tupletSettingsButton_;
    juce::TextButton chromaticTransposeButton_;
    juce::TextButton scaleDegreeTransposeButton_;
    juce::Viewport viewport_;
    RollContentComponent* content_ = nullptr;
    bool chromaticReveal_ = false;
    bool refreshingClipControls_ = false;
    std::function<void()> toggleChromaticRevealCallback_;

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
