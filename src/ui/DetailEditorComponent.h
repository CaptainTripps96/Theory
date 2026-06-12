#pragma once

#include "ui/DeviceChainComponent.h"
#include "ui/PianoRollComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class DetailEditorComponent final : public juce::Component
{
public:
    explicit DetailEditorComponent (app::AppServices& appServices);

    void openClip (std::string trackId, std::string clipId);
    void clearClip();
    void setPlayheadTick (core::time::TickPosition playheadTick);
    void refresh();
    bool toggleMode();
    bool showPianoRoll();
    bool showDeviceChain();
    bool hasOpenClip() const;
    bool hasSelectedNotes() const;
    bool selectAllNotes();
    bool copySelectedNotes();
    bool pasteCopiedNotes();
    std::optional<std::pair<std::string, std::string>> openClipIds() const;
    std::vector<std::string> selectedNoteIdsForClip (const std::string& trackId, const std::string& clipId) const;
    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    enum class Mode
    {
        pianoRoll,
        deviceChain
    };

    void setMode (Mode mode);
    void updateModeButtons();

    Mode mode_ = Mode::pianoRoll;
    juce::TextButton pianoRollButton_;
    juce::TextButton deviceChainButton_;
    PianoRollComponent pianoRollComponent_;
    DeviceChainComponent deviceChainComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailEditorComponent)
};
}
