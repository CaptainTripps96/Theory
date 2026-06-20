#include "ui/PianoRollComponent.h"

#include "app/AppServices.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/ArpeggiateSelectionCommand.h"
#include "core/commands/ChordStackingCommand.h"
#include "core/commands/DeleteNoteCommand.h"
#include "core/commands/MoveNoteCommand.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/commands/ResizeNoteCommand.h"
#include "core/commands/SeparateNotesToClipCommand.h"
#include "core/commands/SetProjectRhythmSettingsCommand.h"
#include "core/commands/TransposeClipCommand.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/HarmonicContextResolver.h"
#include "core/sequencing/HarmonicOverlay.h"
#include "core/sequencing/ExpressionDestinationRegistry.h"
#include "core/sequencing/ExpressionEvaluation.h"
#include "core/sequencing/ExpressionPhraseEditing.h"
#include "core/sequencing/ExpressionReleaseGhosts.h"
#include "core/sequencing/PitchExpressionEvaluation.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"
#include "core/sequencing/Arpeggiator.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/time/GridDivision.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff11161d };
const auto headerColour = juce::Colour { 0xff171e28 };
const auto pitchHeaderColour = juce::Colour { 0xff1b2230 };
const auto naturalLaneColour = juce::Colour { 0xff202a36 };
const auto accidentalLaneColour = juce::Colour { 0xff19212b };
const auto greyedLaneColour = juce::Colour { 0xff161b22 };
const auto usedAccidentalLaneColour = juce::Colour { 0xff2f2736 };
const auto rootOverlayColour = juce::Colour { 0xfff4c95d };
const auto chordToneOverlayColour = juce::Colour { 0xff55c6a9 };
const auto nonChordScaleToneOverlayColour = juce::Colour { 0xffd8e3ee };
const auto accidentalOverlayColour = juce::Colour { 0xffd88cff };
const auto gridColour = juce::Colour { 0xff303945 };
const auto beatGridColour = juce::Colour { 0xff27313d };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff9aa7b7 };
const auto noteColour = juce::Colour { 0xff70d3e5 };
const auto selectedNoteColour = juce::Colour { 0xffffd36e };
const auto playheadCursorColour = juce::Colour { 0xffff7a59 };
const auto expressionCurveColour = juce::Colour { 0xfff4c95d };
const auto expressionGuideColour = juce::Colour { 0xffd8e3ee };
const auto expressionPhraseColour = juce::Colour { 0xff55c6a9 };
const auto expressionCyclicColour = juce::Colour { 0xffd88cff };
const auto expressionWarningColour = juce::Colour { 0xffff7a59 };
const auto noteOutlineColour = juce::Colour { 0xff0f2830 };
const auto selectedOutlineColour = juce::Colour { 0xfff8fafc };
const auto buttonColour = juce::Colour { 0xff24303d };
const auto buttonOnColour = juce::Colour { 0xff3f6f7f };

constexpr int pitchHeaderWidth = 68;
constexpr int expressionLanePanelWidth = 220;
constexpr int expressionStripHeight = 20;
constexpr int harmonicChangeHeaderWidth = 48;
constexpr int rulerHeight = 24;
constexpr int defaultRowHeight = 18;
constexpr int minimumRowHeight = 10;
constexpr int maximumRowHeight = 36;
constexpr int defaultPixelsPerQuarter = 48;
constexpr int minimumPixelsPerQuarter = 24;
constexpr int maximumPixelsPerQuarter = 180;
constexpr float zoomStep = 1.04f;
constexpr int beatsPerBar = 4;
constexpr int noteEdgeHandleWidth = 6;
constexpr int defaultVelocity = 100;
constexpr int headerControlsReservedWidth = 900;
constexpr double defaultCyclicExpressionAmplitude = 0.25;
constexpr auto ticksPerBeat = core::time::ticksPerQuarterNote;
constexpr auto ticksPerBar = ticksPerBeat * beatsPerBar;

bool isAccidentalPitchClass (int midiPitch)
{
    switch (midiPitch % 12)
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

bool hasSelectionModifier (const juce::ModifierKeys& modifiers)
{
    return modifiers.isShiftDown() || modifiers.isCommandDown() || modifiers.isCtrlDown();
}

std::int64_t roundToSnap (std::int64_t ticks, std::int64_t gridTicks)
{
    if (ticks < 0)
        return -roundToSnap (-ticks, gridTicks);

    const auto safeGridTicks = std::max<std::int64_t> (1, gridTicks);
    return ((ticks + (safeGridTicks / 2)) / safeGridTicks) * safeGridTicks;
}

std::int64_t clampTicks (std::int64_t value, std::int64_t minimum, std::int64_t maximum)
{
    return std::clamp (value, minimum, std::max (minimum, maximum));
}

juce::String barBeatText (core::time::TickPosition position)
{
    const auto ticks = std::max<std::int64_t> (0, position.ticks());
    const auto bar = (ticks / ticksPerBar) + 1;
    const auto beat = ((ticks % ticksPerBar) / ticksPerBeat) + 1;
    return juce::String (static_cast<int> (bar)) + "." + juce::String (static_cast<int> (beat));
}

juce::String keyNameFor (core::music_theory::PitchClass pitchClass)
{
    return core::music_theory::spellPitchClass (pitchClass).toString();
}

bool keyMatchesCharacter (const juce::KeyPress& key, char lowerCaseCharacter)
{
    const auto upperCaseCharacter = static_cast<char> (lowerCaseCharacter - 'a' + 'A');
    const auto textCharacter = key.getTextCharacter();
    return textCharacter == lowerCaseCharacter
        || textCharacter == upperCaseCharacter
        || key.getKeyCode() == lowerCaseCharacter
        || key.getKeyCode() == upperCaseCharacter;
}

juce::MouseEvent debugMouseEventFor (juce::Component& component,
                                     juce::Point<float> point,
                                     int numberOfClicks)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent {
        juce::Desktop::getInstance().getMainMouseSource(),
        point,
        juce::ModifierKeys {},
        juce::MouseInputSource::defaultPressure,
        juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation,
        juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY,
        &component,
        &component,
        now,
        point,
        now,
        numberOfClicks,
        false
    };
}
}

class PianoRollComponent::RollContentComponent final : public juce::Component,
                                                       public juce::SettableTooltipClient
{
public:
    RollContentComponent (app::AppServices& appServices,
                          std::optional<ClipSelection>& selectedClip,
                          core::time::TickPosition& playheadTick,
                          bool& chromaticReveal,
                          bool& expressionModeEnabled,
                          bool& expressionReleaseModeEnabled,
                          std::optional<core::sequencing::ExpressionLaneId>& selectedExpressionLaneId,
                          std::optional<core::sequencing::ExpressionClipId>& selectedPhraseEnvelopeId,
                          std::function<void()>& expressionSelectionChanged,
                          std::function<void()>& toggleChromaticReveal)
        : appServices_ (appServices),
          selectedClip_ (selectedClip),
          playheadTick_ (playheadTick),
          chromaticReveal_ (chromaticReveal),
          expressionModeEnabled_ (expressionModeEnabled),
          expressionReleaseModeEnabled_ (expressionReleaseModeEnabled),
          selectedExpressionLaneId_ (selectedExpressionLaneId),
          selectedPhraseEnvelopeId_ (selectedPhraseEnvelopeId),
          expressionSelectionChanged_ (expressionSelectionChanged),
          toggleChromaticReveal_ (toggleChromaticReveal)
    {
        setWantsKeyboardFocus (true);
    }

    bool debugEmulateSingleClickAtFirstEditableCell()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto point = firstEditableGridPoint();
        const auto event = debugMouseEventFor (*this, point, 1);
        mouseDown (event);
        mouseUp (event);
        return true;
    }

    bool debugEmulateDoubleClickAtFirstEditableCell()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto beforeNoteCount = clip->notes().size();
        const auto point = firstEditableGridPoint();
        const auto downEvent = debugMouseEventFor (*this, point, 1);
        const auto doubleClickEvent = debugMouseEventFor (*this, point, 2);
        mouseDown (downEvent);
        mouseDoubleClick (doubleClickEvent);
        mouseUp (doubleClickEvent);

        const auto* updatedClip = selectedMidiClip();
        return updatedClip != nullptr && updatedClip->notes().size() > beforeNoteCount;
    }

    std::optional<juce::Point<float>> debugFirstEditableCellGlobalPoint() const
    {
        if (selectedMidiClip() == nullptr)
            return std::nullopt;

        return firstEditableGridPoint();
    }

    bool debugEmulateMarqueeSelectAllVisibleNotes()
    {
        if (selectedMidiClip() == nullptr)
            return false;

        selectedNoteIds_.clear();
        selectNotesInMarquee (getLocalBounds().withTrimmedTop (rulerHeight), false);
        repaint();
        return ! selectedNoteIds_.empty();
    }

    bool debugSelectNoteIds (std::vector<std::string> noteIds)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        for (const auto& noteId : noteIds)
            if (clip->findNoteById (noteId) == nullptr)
                return false;

        selectedNoteIds_ = std::move (noteIds);
        updateExpressionObjectSelectionForCurrentNotes (*clip, false);

        repaint();
        return true;
    }

    bool debugEmulateMarqueeSelectFirstReleaseGhost()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! expressionModeEnabled_ || ! expressionReleaseModeEnabled_)
            return false;

        const auto ghosts = releaseGhostsForClip (*clip);
        if (ghosts.empty())
            return false;

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        const auto* note = clip->findNoteById (ghosts.front().noteId);
        if (note == nullptr)
            return false;

        const auto bounds = boundsForGhost (*note, ghosts.front(), segments, layouts);
        if (bounds.empty())
            return false;

        selectedNoteIds_.clear();
        selectNotesInMarquee (bounds.front().bounds.expanded (2), false);
        repaint();
        return isSelected (ghosts.front().noteId);
    }

    bool debugExpressionKeyPress (char editKey, int arrowKeyCode, bool shiftDown = false)
    {
        if (! expressionModeEnabled_)
            return false;

        auto handled = false;
        if (editKey != '\0')
        {
            const auto key = juce::KeyPress { static_cast<int> (std::tolower (static_cast<unsigned char> (editKey))),
                                              juce::ModifierKeys {},
                                              static_cast<juce::juce_wchar> (std::tolower (static_cast<unsigned char> (editKey))) };
            handled = expressionModeKeyPressed (key, false, shiftDown) || handled;
        }

        if (arrowKeyCode != 0)
        {
            const auto key = juce::KeyPress { arrowKeyCode, juce::ModifierKeys {}, 0 };
            handled = expressionModeKeyPressed (key, false, shiftDown) || handled;
        }

        return handled;
    }

    bool debugSelectFirstPhraseEnvelope()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = selectedExpressionLane (*clip);
        if (lane == nullptr || lane->phraseEnvelopeClips().empty())
            return false;

        selectedPhraseEnvelopeId_ = lane->phraseEnvelopeClips().front().id();
        expressionOverlayCache_ = {};
        repaint();
        return true;
    }

    const core::sequencing::CyclicExpressionClip* selectedCyclicExpression() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedCyclicClipId_.has_value())
            return nullptr;

        const auto* lane = selectedExpressionLane (*clip);
        if (lane == nullptr)
            return nullptr;

        const auto match = std::find_if (lane->cyclicClips().begin(), lane->cyclicClips().end(), [this] (const auto& cyclic) {
            return cyclic.id() == *selectedCyclicClipId_;
        });
        return match == lane->cyclicClips().end() ? nullptr : &*match;
    }

    const core::sequencing::VibratoExpression* selectedVibratoExpression() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedVibratoId_.has_value())
            return nullptr;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        return lane == nullptr ? nullptr : lane->findVibratoExpression (*selectedVibratoId_);
    }

    std::size_t debugCyclicWaveformCount() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return 0;

        const auto* lane = selectedExpressionLane (*clip);
        if (lane == nullptr)
            return 0;

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        const auto& overlay = cachedExpressionOverlay (*lane, segments, layouts, getLocalBounds().withTrimmedTop (rulerHeight));
        return overlay.cyclicWaveforms.size();
    }

    std::size_t debugPitchSlurCount() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return 0;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        return lane == nullptr ? 0 : lane->pitchSlurs().size();
    }

    std::optional<std::int64_t> debugSelectedPitchSlurTimeTicks() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedPitchSlurId_.has_value())
            return std::nullopt;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr)
            return std::nullopt;

        const auto* slur = lane->findPitchSlur (*selectedPitchSlurId_);
        if (slur == nullptr)
            return std::nullopt;

        return slur->slurTime().ticks();
    }

    std::vector<juce::Point<float>> debugFirstPitchSlurTrajectoryPoints() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return {};

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->pitchSlurs().empty())
            return {};

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        return pitchSlurTrajectoryPoints (*clip,
                                          lane->pitchSlurs().front(),
                                          segments,
                                          layouts,
                                          getLocalBounds().withTrimmedTop (rulerHeight));
    }

    bool debugApplyFirstPitchSlurVoiceOverride()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value())
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->pitchSlurs().empty())
            return false;

        const auto slurId = lane->pitchSlurs().front().id();
        const auto* slur = lane->findPitchSlur (slurId);
        if (slur == nullptr)
            return false;

        core::sequencing::PitchSlurBlockSettings settings {
            slur->blockId(),
            slur->slurTime() + core::time::TickDuration::fromTicks (snapTicks()),
            slur->curveShape(),
            slur->legatoNoRetrigger()
        };
        core::sequencing::applySlurVoiceOverride (*lane, slurId, settings);

        const auto* replacement = lane->findPitchSlur (slurId);
        if (replacement == nullptr)
            return false;

        if (! appServices_.replacePitchSlurs (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::ExpressionState::defaultPitchLaneId(),
                { *replacement }))
        {
            return false;
        }

        selectedPitchSlurId_ = slurId;
        selectedPitchSlurSingleVoiceEdit_ = true;
        pitchSlurEditPrefix_ = true;
        expressionEditPrefix_.reset();
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        repaint();
        return true;
    }

    std::size_t debugVibratoExpressionCount() const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return 0;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        return lane == nullptr ? 0 : lane->vibratoExpressions().size();
    }

    bool debugApplyFirstVibratoVoiceOverride()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value())
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->vibratoExpressions().empty())
            return false;

        auto vibrato = lane->vibratoExpressions().front();
        if (vibrato.sourceNoteIds().empty())
            return false;

        auto overrides = vibrato.voiceOverrides();
        overrides.push_back (core::sequencing::VibratoVoiceOverride {
            vibrato.sourceNoteIds().front(),
            vibrato.amplitudeSemitones() + 0.05,
            vibrato.attackTime(),
            vibrato.releaseTime(),
            vibrato.frequencyDivisionId(),
            vibrato.waveShape(),
            0.25
        });
        vibrato.setVoiceOverrides (std::move (overrides));

        if (! appServices_.replaceVibratoExpression (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::ExpressionState::defaultPitchLaneId(),
                vibrato))
        {
            return false;
        }

        selectedVibratoId_ = vibrato.id();
        expressionOverlayCache_ = {};
        repaint();
        return true;
    }

    void clearNoteSelection()
    {
        selectedNoteIds_.clear();
        dragState_.reset();
        if (expressionModeEnabled_)
            clearSelectedExpressionObjectsForNoteSelection();
        repaint();
    }

    void clearSelectedExpressionObjectsForLaneChange()
    {
        selectedCyclicClipId_.reset();
        selectedPitchSlurId_.reset();
        selectedVibratoId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = false;
        pitchSlurEditPrefix_ = false;
        expressionEditPrefix_.reset();
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        repaint();
    }

    void resolveExpressionObjectsForCurrentLane()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || selectedNoteIds_.empty())
        {
            clearSelectedExpressionObjectsForLaneChange();
            return;
        }

        updateExpressionObjectSelectionForCurrentNotes (*clip, false);
        repaint();
    }

    std::vector<std::string> selectedNoteIds() const
    {
        return selectedNoteIds_;
    }

    bool hasOpenClip() const
    {
        return selectedMidiClip() != nullptr;
    }

    bool hasSelectedNotes() const
    {
        return ! selectedNoteIds_.empty();
    }

    void paint (juce::Graphics& graphics) override
    {
        core::diagnostics::ScopedPerformanceTimer timer { "PianoRollContent::paint" };

        graphics.fillAll (surfaceColour);

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
        {
            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
            graphics.drawText ("No clip selected", getLocalBounds(), juce::Justification::centred);
            return;
        }

        std::vector<PianoRollSegment> segments;
        std::vector<SegmentLayout> layouts;
        auto clipLength = core::time::TickDuration {};
        auto contentWidth = getWidth();

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint model" };
            segments = visiblePitchSegments (*clip);
            layouts = segmentLayouts (segments);
            clipLength = editableClipLength (*clip);
            contentWidth = std::max (getWidth(), (layouts.empty() ? pitchHeaderWidth : layouts.back().gridEndX) + 80);
        }

        graphics.setColour (headerColour);
        graphics.fillRect (0, 0, contentWidth, rulerHeight);
        const auto paintClipBounds = visiblePaintBounds (graphics);
        const auto paintClipTop = paintClipBounds.getY();

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint lanes" };
            paintLaneBackground (graphics, segments, layouts, contentWidth, paintClipBounds);
        }

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint grid" };

            const auto gridTicks = snapTicks();
            if (gridTicks < ticksPerBeat)
            {
                graphics.setColour (beatGridColour.withAlpha (0.48f));
                for (const auto& layout : layouts)
                {
                    const auto& segment = segments[layout.segmentIndex];
                    const auto firstGridTick = ((segment.localStart.ticks() + gridTicks - 1) / gridTicks) * gridTicks;
                    for (auto tick = firstGridTick; tick < segment.localEnd.ticks(); tick += gridTicks)
                    {
                    if ((tick % ticksPerBeat) == 0)
                        continue;

                    const auto x = xForLocalTick (layout, segment, core::time::TickPosition::fromTicks (tick));
                    if (x < paintClipBounds.getX() || x > paintClipBounds.getRight())
                        continue;

                    graphics.fillRect (x, paintClipTop, 1, paintClipBounds.getHeight());
                }
            }
            }

            for (const auto& layout : layouts)
            {
                const auto& segment = segments[layout.segmentIndex];
                const auto firstBeatTick = ((segment.localStart.ticks() + ticksPerBeat - 1) / ticksPerBeat) * ticksPerBeat;
                for (auto tick = firstBeatTick; tick <= segment.localEnd.ticks(); tick += ticksPerBeat)
                {
                    const auto x = xForLocalTick (layout, segment, core::time::TickPosition::fromTicks (tick));
                    const auto isBar = ((tick / ticksPerBeat) % beatsPerBar) == 0;
                    if (x < paintClipBounds.getX() || x > paintClipBounds.getRight())
                        continue;

                    graphics.setColour (isBar ? gridColour : beatGridColour);
                    graphics.fillRect (x, paintClipTop, 1, paintClipBounds.getHeight());

                    if (isBar)
                    {
                        graphics.setColour (mutedTextColour);
                        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
                        graphics.drawText (barBeatText (core::time::TickPosition::fromTicks (tick)),
                                           x + 5,
                                           0,
                                           48,
                                           rulerHeight,
                                           juce::Justification::centredLeft);
                    }
                }
            }
        }

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint harmonic-overlay" };
            paintHarmonicOverlay (graphics, *clip, segments, layouts, clipLength);
        }

        if (expressionModeEnabled_)
        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint expression-overlay" };
            paintExpressionOverlay (graphics, *clip, segments, layouts, clipLength, paintClipBounds);
        }

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint notes" };
            const auto useVisibleTickFilter = ! dragState_.has_value();
            const auto visibleTickRanges = useVisibleTickFilter
                ? visibleTickRangesForPaint (segments, layouts, paintClipBounds)
                : std::vector<LocalTickRange> {};

            for (const auto& note : clip->notes())
            {
                if (useVisibleTickFilter && ! noteIntersectsTickRanges (note, visibleTickRanges))
                    continue;

                const auto visualState = dragState_.has_value()
                    ? visualStateFor (note, *dragState_, clipLength, segments)
                    : NoteVisualState { note.pitch().value(), note.spelling() };

                const auto selected = isSelected (note.id());
                const auto drawNoteBounds = [&] (const NoteRenderBounds& bounds)
                {
                    if (! bounds.bounds.intersects (paintClipBounds))
                        return;

                    const auto& segment = segments[bounds.segmentIndex];
                    const auto* lane = laneForNoteState (visualState.midiPitch, visualState.spelling, segment.lanes);
                    graphics.setColour ((selected ? selectedNoteColour : noteColourForLane (lane)).withAlpha (expressionModeEnabled_ ? 0.42f : 1.0f));
                    graphics.fillRoundedRectangle (bounds.bounds.toFloat(), 3.0f);
                    graphics.setColour ((selected ? selectedOutlineColour : noteOutlineColour).withAlpha (expressionModeEnabled_ ? 0.58f : 1.0f));
                    graphics.drawRoundedRectangle (bounds.bounds.toFloat().reduced (0.5f), 3.0f, selected ? 1.5f : 1.0f);

                    if (bounds.bounds.getWidth() > 24)
                    {
                        graphics.setColour (juce::Colours::black.withAlpha (0.72f));
                        graphics.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
                        const auto label = noteLabelForState (visualState.midiPitch, visualState.spelling, segment.lanes);
                        graphics.drawText (label, bounds.bounds.reduced (5, 0), juce::Justification::centredLeft);
                    }
                };

                if (dragState_.has_value())
                {
                    for (const auto& bounds : previewBoundsFor (note, *dragState_, clipLength, segments, layouts))
                        drawNoteBounds (bounds);
                }
                else
                {
                    forEachBoundsForNote (note, segments, layouts, paintClipBounds, drawNoteBounds);
                }
            }

            if (expressionModeEnabled_ && expressionReleaseModeEnabled_)
                paintReleaseGhosts (graphics, *clip, segments, layouts, paintClipBounds);

            if (expressionModeEnabled_)
                paintPitchSlurs (graphics, *clip, segments, layouts, paintClipBounds);

            if (expressionModeEnabled_)
                paintVibratoPaths (graphics, *clip, segments, layouts, paintClipBounds);
        }

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint adornments" };

            if (marqueeState_.has_value())
            {
                const auto bounds = marqueeBounds();
                graphics.setColour (selectedNoteColour.withAlpha (0.16f));
                graphics.fillRect (bounds);
                graphics.setColour (selectedNoteColour.withAlpha (0.82f));
                graphics.drawRect (bounds, 1);
            }

            paintPasteCursor (graphics, *clip, segments, layouts);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        appServices_.tracePluginState ("piano-roll mouseDown begin x=" + std::to_string (event.x)
                                       + " y=" + std::to_string (event.y));
        grabKeyboardFocus();

        auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return;

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        draggingPasteCursor_ = false;

        if (event.y < rulerHeight)
        {
            dragState_.reset();
            marqueeState_.reset();
            draggingPasteCursor_ = setPasteCursorFromPoint (event.position, *clip, segments, layouts);
            repaint();
            return;
        }

        setPasteCursorFromPoint (event.position, *clip, segments, layouts);

        if (expressionModeEnabled_)
        {
            if (const auto envelopeId = phraseEnvelopeAt (event.position, *clip, segments, layouts))
            {
                selectedPhraseEnvelopeId_ = *envelopeId;
                selectedCyclicClipId_.reset();
                expressionEditPrefixTargetsCyclic_ = false;
                expressionSelectionChanged_();
                repaint();
                return;
            }

            if (const auto cyclicId = cyclicExpressionAt (event.position, *clip, segments, layouts))
            {
                selectedCyclicClipId_ = *cyclicId;
                selectedPhraseEnvelopeId_.reset();
                expressionEditPrefixTargetsCyclic_ = true;
                expressionSelectionChanged_();
                repaint();
                return;
            }
        }

        if (auto hit = noteAt (event.position, segments, layouts))
        {
            activeArpeggioSourceNotes_.clear();
            if (hasSelectionModifier (event.mods))
            {
                toggleNoteSelection (hit->noteId);
                if (! isSelected (hit->noteId))
                {
                    repaint();
                    return;
                }
            }
            else if (! isSelected (hit->noteId))
            {
                selectedNoteIds_ = { hit->noteId };
            }

            updateExpressionObjectSelectionForCurrentNotes (*clip, hasSelectionModifier (event.mods));

            if (! expressionModeEnabled_)
            {
                dragState_ = DragState {
                    dragModeForHit (*hit, event.position),
                    event.x,
                    event.y,
                    event.x,
                    event.y,
                    originalStatesForSelection (*clip)
                };
            }
            repaint();
            return;
        }

        if (event.x < pitchHeaderWidth || ! segmentIndexForPoint (event.position, segments, layouts).has_value())
        {
            activeArpeggioSourceNotes_.clear();
            if (! hasSelectionModifier (event.mods))
            {
                selectedNoteIds_.clear();
                if (expressionModeEnabled_)
                    clearSelectedExpressionObjectsForNoteSelection();
            }
            repaint();
            return;
        }

        marqueeState_ = MarqueeState {
            event.getMouseDownPosition(),
            event.getMouseDownPosition(),
            event.mods.isShiftDown()
        };

        activeArpeggioSourceNotes_.clear();
        if (! hasSelectionModifier (event.mods))
            selectedNoteIds_.clear();

        repaint();
        appServices_.tracePluginState ("piano-roll mouseDown end");
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (draggingPasteCursor_)
        {
            if (const auto* clip = selectedMidiClip())
            {
                const auto segments = visiblePitchSegments (*clip);
                const auto layouts = segmentLayouts (segments);
                setPasteCursorFromPoint (event.position, *clip, segments, layouts);
            }

            repaint();
            return;
        }

        if (marqueeState_.has_value())
        {
            marqueeState_->current = event.position.roundToInt();
            repaint();
            return;
        }

        if (! dragState_.has_value())
            return;

        dragState_->currentX = event.x;
        dragState_->currentY = event.y;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (draggingPasteCursor_)
        {
            if (const auto* clip = selectedMidiClip())
            {
                const auto segments = visiblePitchSegments (*clip);
                const auto layouts = segmentLayouts (segments);
                setPasteCursorFromPoint (event.position, *clip, segments, layouts);
            }

            draggingPasteCursor_ = false;
            repaint();
            return;
        }

        if (marqueeState_.has_value())
        {
            marqueeState_->current = event.position.roundToInt();
            selectNotesInMarquee (marqueeBounds(), marqueeState_->additive);
            marqueeState_.reset();
            repaint();
            return;
        }

        if (! dragState_.has_value())
            return;

        dragState_->currentX = event.x;
        dragState_->currentY = event.y;
        commitDrag (*dragState_);
        dragState_.reset();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        appServices_.tracePluginState ("piano-roll mouseDoubleClick begin x=" + std::to_string (event.x)
                                       + " y=" + std::to_string (event.y));
        grabKeyboardFocus();

        auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return;

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        if (auto hit = noteAt (event.position, segments, layouts))
        {
            selectedNoteIds_ = { hit->noteId };
            if (! expressionModeEnabled_)
                deleteSelectedNotes();
            return;
        }

        if (! expressionModeEnabled_ && event.x >= pitchHeaderWidth && event.y >= rulerHeight)
            createNoteAt (event.position, *clip, segments, layouts);

        appServices_.tracePluginState ("piano-roll mouseDoubleClick end");
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            setTooltip ({});
            return;
        }

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        if (const auto tooltip = tooltipForHeaderAt (event.position, segments, layouts))
            setTooltip (*tooltip);
        else
            setTooltip ({});

        if (event.y < rulerHeight && xToSnappedTickInSegments (event.position.x, segments, layouts).has_value())
        {
            setTooltip ("Click or drag to place the paste playhead");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return;
        }

        if (const auto hit = noteAt (event.position, segments, layouts))
        {
            const auto mode = dragModeForHit (*hit, event.position);
            setMouseCursor (mode == DragMode::resizeStart ? juce::MouseCursor::LeftEdgeResizeCursor
                                                          : mode == DragMode::resizeEnd ? juce::MouseCursor::RightEdgeResizeCursor
                                                                                        : juce::MouseCursor::DraggingHandCursor);
            return;
        }

        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (event.mods.isCommandDown() || event.mods.isCtrlDown())
        {
            zoomHorizontally (wheel.deltaY);
            return;
        }

        if (event.mods.isAltDown())
        {
            zoomVertically (wheel.deltaY);
            return;
        }

        juce::Component::mouseWheelMove (event, wheel);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto modifiers = key.getModifiers();
        const auto altDown = modifiers.isAltDown();
        const auto commandDown = modifiers.isCommandDown() || modifiers.isCtrlDown();
        const auto shiftDown = modifiers.isShiftDown();

        if (expressionModeEnabled_)
            return expressionModeKeyPressed (key, commandDown, shiftDown);

        if (key == juce::KeyPress::deleteKey
            || key == juce::KeyPress::backspaceKey
            || key == juce::KeyPress::numberPadDelete)
        {
            deleteSelectedNotes();
            return true;
        }

        if (commandDown && keyMatchesCharacter (key, 'a'))
        {
            selectAllNotes();
            return true;
        }

        if (commandDown && keyMatchesCharacter (key, 'c'))
        {
            copySelectedNotes();
            return true;
        }

        if (commandDown && keyMatchesCharacter (key, 'v'))
        {
            pasteCopiedNotes();
            return true;
        }

        if (commandDown && keyMatchesCharacter (key, 'p'))
        {
            separateSelectedNotesToClip();
            return true;
        }

        if (altDown && key.getKeyCode() == juce::KeyPress::rightKey)
        {
            stepArpeggioSubdivision (true);
            return true;
        }

        if (altDown && key.getKeyCode() == juce::KeyPress::leftKey)
        {
            stepArpeggioSubdivision (false);
            return true;
        }

        if (altDown && key.getKeyCode() == juce::KeyPress::upKey)
        {
            cycleArpeggioPattern (false);
            return true;
        }

        if (altDown && key.getKeyCode() == juce::KeyPress::downKey)
        {
            cycleArpeggioPattern (true);
            return true;
        }

        if (commandDown && key.getKeyCode() == juce::KeyPress::upKey)
        {
            stackDiatonicThird();
            return true;
        }

        if (commandDown && key.getKeyCode() == juce::KeyPress::downKey)
        {
            removeHighestChordTone();
            return true;
        }

        if (! commandDown && shiftDown && key.getKeyCode() == juce::KeyPress::leftKey)
        {
            resizeSelectedNoteEnds (false);
            return true;
        }

        if (! commandDown && shiftDown && key.getKeyCode() == juce::KeyPress::rightKey)
        {
            resizeSelectedNoteEnds (true);
            return true;
        }

        if (! commandDown && shiftDown && key.getKeyCode() == juce::KeyPress::upKey)
        {
            if (selectionIsSingleNoteMelody())
                transposeSelectedNotesByOctaves (1);
            else
                invertSelectedNotes (core::commands::InvertChordCommand::Direction::upward);
            return true;
        }

        if (! commandDown && shiftDown && key.getKeyCode() == juce::KeyPress::downKey)
        {
            if (selectionIsSingleNoteMelody())
                transposeSelectedNotesByOctaves (-1);
            else
                invertSelectedNotes (core::commands::InvertChordCommand::Direction::downward);
            return true;
        }

        if (! commandDown && shiftDown && keyMatchesCharacter (key, 's'))
        {
            fillScalarRunBetweenSelectedNotes();
            return true;
        }

        if (! commandDown && ! shiftDown && ! altDown && key.getKeyCode() == juce::KeyPress::leftKey)
        {
            moveSelectedNotesHorizontally (false);
            return true;
        }

        if (! commandDown && ! shiftDown && ! altDown && key.getKeyCode() == juce::KeyPress::rightKey)
        {
            moveSelectedNotesHorizontally (true);
            return true;
        }

        if (! commandDown && ! shiftDown && ! altDown && key.getKeyCode() == juce::KeyPress::upKey)
        {
            moveSelectedNotesVertically (true);
            return true;
        }

        if (! commandDown && ! shiftDown && ! altDown && key.getKeyCode() == juce::KeyPress::downKey)
        {
            moveSelectedNotesVertically (false);
            return true;
        }

        if (commandDown && keyMatchesCharacter (key, 'd'))
        {
            duplicateSelectedNotes();
            return true;
        }

        if (! commandDown && keyMatchesCharacter (key, 'c'))
        {
            if (toggleChromaticReveal_)
                toggleChromaticReveal_();

            return true;
        }

        return false;
    }

    int preferredWidth() const
    {
        if (const auto* clip = selectedMidiClip())
        {
            const auto layouts = segmentLayouts (visiblePitchSegments (*clip));
            return (layouts.empty() ? pitchHeaderWidth + durationToWidth (editableClipLength (*clip)) : layouts.back().gridEndX) + 120;
        }

        return 900;
    }

    int preferredHeight() const
    {
        if (const auto* clip = selectedMidiClip())
            return rulerHeight + (maximumLaneCount (visiblePitchSegments (*clip)) * rowHeight_);

        return rulerHeight + (75 * rowHeight_);
    }

    bool selectAllNotesInClip()
    {
        selectAllNotes();
        return selectedMidiClip() != nullptr;
    }

    void frameNotesInViewport (bool defaultToC3 = false)
    {
        auto* viewport = findParentComponentOfClass<juce::Viewport>();
        const auto* clip = selectedMidiClip();
        if (viewport == nullptr || clip == nullptr)
            return;

        const auto segments = visiblePitchSegments (*clip);
        if (segments.empty() || maximumLaneCount (segments) == 0)
            return;

        auto topLane = std::optional<int> {};
        auto bottomLane = std::optional<int> {};
        for (const auto& note : clip->notes())
        {
            const auto segmentIndex = segmentIndexForLocalTick (segments, note.startInClip());
            if (! segmentIndex.has_value())
                continue;

            const auto lane = laneIndexForNote (note, segments[*segmentIndex].lanes);
            if (! lane.has_value())
                continue;

            topLane = topLane.has_value() ? std::min (*topLane, *lane) : *lane;
            bottomLane = bottomLane.has_value() ? std::max (*bottomLane, *lane) : *lane;
        }

        if (! topLane.has_value())
        {
            if (! defaultToC3)
                return;

            const auto c3Lane = laneIndexForPitch (48, segments.front().lanes);
            if (! c3Lane.has_value())
                return;

            topLane = *c3Lane;
            bottomLane = *c3Lane;
        }

        const auto padding = rowHeight_ * 3;
        const auto topY = laneToY (*topLane);
        const auto bottomY = laneToY (*bottomLane) + rowHeight_;
        const auto visibleHeight = viewport->getViewArea().getHeight();
        auto targetY = topY - padding;

        if ((bottomY - topY) + (padding * 2) < visibleHeight)
            targetY = ((topY + bottomY) / 2) - (visibleHeight / 2);

        targetY = std::clamp (targetY, 0, std::max (0, getHeight() - visibleHeight));
        viewport->setViewPosition (viewport->getViewPositionX(), targetY);
    }

    void repaintPlayheadTransition (core::time::TickPosition previousTick,
                                    core::time::TickPosition currentTick)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || getWidth() <= 0 || getHeight() <= 0)
            return;

        const auto& playheadLayout = cachedPlayheadLayoutFor (*clip);
        auto dirty = juce::Rectangle<int> {};

        if (const auto previousBounds = playheadCursorBoundsForProjectTick (previousTick,
                                                                            *clip,
                                                                            playheadLayout.segments,
                                                                            playheadLayout.layouts))
            dirty = dirty.isEmpty() ? *previousBounds : dirty.getUnion (*previousBounds);

        if (const auto currentBounds = playheadCursorBoundsForProjectTick (currentTick,
                                                                           *clip,
                                                                           playheadLayout.segments,
                                                                           playheadLayout.layouts))
            dirty = dirty.isEmpty() ? *currentBounds : dirty.getUnion (*currentBounds);

        if (! dirty.isEmpty())
            repaint (dirty.expanded (8, 2).getIntersection (getLocalBounds()));
    }

private:
    static std::optional<core::sequencing::PhraseEnvelopeEditKey> expressionEditKeyForCharacter (const juce::KeyPress& key)
    {
        if (keyMatchesCharacter (key, 'a'))
            return core::sequencing::PhraseEnvelopeEditKey::attack;
        if (keyMatchesCharacter (key, 'd'))
            return core::sequencing::PhraseEnvelopeEditKey::decay;
        if (keyMatchesCharacter (key, 'r'))
            return core::sequencing::PhraseEnvelopeEditKey::release;
        if (keyMatchesCharacter (key, 'f'))
            return core::sequencing::PhraseEnvelopeEditKey::force;
        if (keyMatchesCharacter (key, 'c'))
            return core::sequencing::PhraseEnvelopeEditKey::curve;

        return std::nullopt;
    }

    static std::optional<core::sequencing::PhraseEnvelopeEditDirection> expressionEditDirectionForKey (const juce::KeyPress& key)
    {
        if (key.getKeyCode() == juce::KeyPress::leftKey)
            return core::sequencing::PhraseEnvelopeEditDirection::left;
        if (key.getKeyCode() == juce::KeyPress::rightKey)
            return core::sequencing::PhraseEnvelopeEditDirection::right;
        if (key.getKeyCode() == juce::KeyPress::upKey)
            return core::sequencing::PhraseEnvelopeEditDirection::up;
        if (key.getKeyCode() == juce::KeyPress::downKey)
            return core::sequencing::PhraseEnvelopeEditDirection::down;

        return std::nullopt;
    }

    bool expressionModeKeyPressed (const juce::KeyPress& key, bool commandDown, bool shiftDown)
    {
        if (commandDown && keyMatchesCharacter (key, 'a'))
        {
            selectAllNotes();
            return true;
        }

        if (commandDown && keyMatchesCharacter (key, 'c'))
        {
            copySelectedNotes();
            return true;
        }

        if (! commandDown && shiftDown)
        {
            if (const auto editKey = expressionEditKeyForCharacter (key))
            {
                expressionEditPrefix_ = *editKey;
                expressionEditPrefixTargetsCyclic_ = true;
                pitchSlurEditPrefix_ = false;
                return true;
            }

            if (const auto direction = expressionEditDirectionForKey (key))
            {
                if (expressionEditPrefix_.has_value())
                {
                    if (pitchExpressionLaneSelected())
                        editSelectedVibratoExpression (*expressionEditPrefix_, *direction);
                    else
                        editSelectedCyclicExpression (*expressionEditPrefix_, *direction);
                }
                return true;
            }
        }

        if (! commandDown && ! shiftDown)
        {
            if (keyMatchesCharacter (key, 's'))
            {
                if (pitchExpressionLaneSelected() && selectedPitchSlurId_.has_value() && pitchSlurEditPrefix_)
                {
                    expressionEditPrefix_.reset();
                    expressionEditPrefixTargetsCyclic_ = false;
                    return true;
                }

                if (selectedNoteIds_.size() >= 2 && createPitchSlursFromSelection())
                    return true;

                if (pitchExpressionLaneSelected() && selectedPitchSlurId_.has_value())
                {
                    pitchSlurEditPrefix_ = true;
                    expressionEditPrefix_.reset();
                    expressionEditPrefixTargetsCyclic_ = false;
                    return true;
                }

                return false;
            }

            if (const auto editKey = expressionEditKeyForCharacter (key))
            {
                expressionEditPrefix_ = *editKey;
                expressionEditPrefixTargetsCyclic_ = false;
                pitchSlurEditPrefix_ = false;
                return true;
            }

            if (const auto direction = expressionEditDirectionForKey (key))
            {
                if (pitchExpressionLaneSelected()
                    && (pitchSlurEditPrefix_
                     || (selectedPitchSlurId_.has_value() && ! expressionEditPrefix_.has_value()))
                    && (direction == core::sequencing::PhraseEnvelopeEditDirection::left
                        || direction == core::sequencing::PhraseEnvelopeEditDirection::right))
                {
                    if (editSelectedPitchSlurTime (*direction))
                    {
                        pitchSlurEditPrefix_ = true;
                        expressionEditPrefix_.reset();
                        expressionEditPrefixTargetsCyclic_ = false;
                    }
                }
                else if (pitchExpressionLaneSelected()
                         && selectedPitchSlurId_.has_value()
                         && expressionEditPrefix_.has_value()
                         && *expressionEditPrefix_ == core::sequencing::PhraseEnvelopeEditKey::curve
                         && (*direction == core::sequencing::PhraseEnvelopeEditDirection::up
                             || *direction == core::sequencing::PhraseEnvelopeEditDirection::down))
                {
                    editSelectedPitchSlurCurve (*direction);
                }
                else if (selectedVibratoId_.has_value()
                         && pitchExpressionLaneSelected()
                         && expressionEditPrefix_.has_value()
                         && *expressionEditPrefix_ == core::sequencing::PhraseEnvelopeEditKey::curve)
                {
                    editSelectedVibratoExpression (*expressionEditPrefix_, *direction);
                }
                else if (expressionEditPrefix_.has_value())
                {
                    if (expressionEditPrefixTargetsCyclic_)
                    {
                        if (pitchExpressionLaneSelected())
                            editSelectedVibratoExpression (*expressionEditPrefix_, *direction);
                        else
                            editSelectedCyclicExpression (*expressionEditPrefix_, *direction);
                    }
                    else if (selectedCyclicClipId_.has_value() && *expressionEditPrefix_ == core::sequencing::PhraseEnvelopeEditKey::curve)
                        editSelectedCyclicExpression (*expressionEditPrefix_, *direction);
                    else
                        editSelectedPhraseEnvelope (*expressionEditPrefix_, *direction);
                }
                return true;
            }
        }

        if (key == juce::KeyPress::deleteKey
            || key == juce::KeyPress::backspaceKey
            || key == juce::KeyPress::numberPadDelete)
        {
            auto deletedExpressionObject = false;
            if (pitchExpressionLaneSelected())
                deletedExpressionObject = deleteSelectedVibratoExpression() || deleteSelectedPitchSlur();

            if (! deletedExpressionObject)
                deletedExpressionObject = deleteSelectedCyclicExpression();

            if (! deletedExpressionObject)
                deleteSelectedPhraseEnvelope();
            return true;
        }

        if (key == juce::KeyPress::deleteKey
            || key == juce::KeyPress::backspaceKey
            || key == juce::KeyPress::numberPadDelete
            || key.getKeyCode() == juce::KeyPress::leftKey
            || key.getKeyCode() == juce::KeyPress::rightKey
            || key.getKeyCode() == juce::KeyPress::upKey
            || key.getKeyCode() == juce::KeyPress::downKey
            || (commandDown && (keyMatchesCharacter (key, 'v') || keyMatchesCharacter (key, 'd')))
            || (! commandDown && (keyMatchesCharacter (key, 'c') || keyMatchesCharacter (key, 's'))))
        {
            return true;
        }

        return false;
    }

    std::optional<core::sequencing::ExpressionClipId> nextPhraseEnvelopeId (const core::sequencing::ExpressionLane& lane) const
    {
        for (auto index = lane.phraseEnvelopeClips().size() + 1; index < lane.phraseEnvelopeClips().size() + 2048; ++index)
        {
            auto id = core::sequencing::ExpressionClipId { "env-" + std::to_string (index) };
            if (lane.findPhraseEnvelopeClip (id) == nullptr)
                return id;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::PhraseEnvelopeClip> activePhraseEnvelope (const core::sequencing::ExpressionLane& lane) const
    {
        if (selectedPhraseEnvelopeId_.has_value())
        {
            if (const auto* envelope = lane.findPhraseEnvelopeClip (*selectedPhraseEnvelopeId_))
                return *envelope;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::ExpressionClipId> nextCyclicClipId (const core::sequencing::ExpressionLane& lane) const
    {
        for (auto index = lane.cyclicClips().size() + 1; index < lane.cyclicClips().size() + 2048; ++index)
        {
            auto id = core::sequencing::ExpressionClipId { "cyclic-" + std::to_string (index) };
            const auto exists = std::any_of (lane.cyclicClips().begin(), lane.cyclicClips().end(), [&] (const auto& clip) {
                return clip.id() == id;
            });
            if (! exists)
                return id;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::CyclicExpressionClip> activeCyclicExpression (const core::sequencing::ExpressionLane& lane) const
    {
        if (selectedCyclicClipId_.has_value())
        {
            const auto match = std::find_if (lane.cyclicClips().begin(), lane.cyclicClips().end(), [&] (const auto& clip) {
                return clip.id() == *selectedCyclicClipId_;
            });
            if (match != lane.cyclicClips().end())
                return *match;
        }

        return std::nullopt;
    }

    static bool sourceNoteSetMatchesSelection (const std::vector<std::string>& sourceNoteIds,
                                               const std::vector<std::string>& selectedNoteIds)
    {
        if (sourceNoteIds.size() != selectedNoteIds.size())
            return false;

        auto source = sourceNoteIds;
        auto selected = selectedNoteIds;
        std::sort (source.begin(), source.end());
        std::sort (selected.begin(), selected.end());
        return source == selected;
    }

    void selectExpressionClipsForCurrentNoteSelection (const core::sequencing::MidiClip& clip)
    {
        selectedPhraseEnvelopeId_.reset();
        selectedCyclicClipId_.reset();
        selectedPitchSlurId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = false;
        selectedVibratoId_.reset();
        pitchSlurEditPrefix_ = false;
        expressionEditPrefixTargetsCyclic_ = false;

        const auto* lane = selectedExpressionLane (clip);
        if (lane != nullptr && ! selectedNoteIds_.empty())
        {
            const auto envelope = std::find_if (lane->phraseEnvelopeClips().begin(),
                                                lane->phraseEnvelopeClips().end(),
                                                [this] (const auto& candidate)
                                                {
                                                    return sourceNoteSetMatchesSelection (candidate.sourceNoteIds(), selectedNoteIds_);
                                                });
            if (envelope != lane->phraseEnvelopeClips().end())
                selectedPhraseEnvelopeId_ = envelope->id();

            const auto cyclic = std::find_if (lane->cyclicClips().begin(),
                                              lane->cyclicClips().end(),
                                              [this] (const auto& candidate)
                                              {
                                                  return sourceNoteSetMatchesSelection (candidate.sourceNoteIds(), selectedNoteIds_);
                                              });
            if (cyclic != lane->cyclicClips().end())
                selectedCyclicClipId_ = cyclic->id();

            expressionEditPrefixTargetsCyclic_ = selectedCyclicClipId_.has_value()
                && ! selectedPhraseEnvelopeId_.has_value();
        }

        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
    }

    void selectPitchExpressionObjectsForCurrentNoteSelection (const core::sequencing::MidiClip& clip, bool additive)
    {
        const auto* pitchLane = clip.expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (pitchLane == nullptr)
            return;

        if (const auto slur = existingPitchSlurIdForSelectedNotes (clip, *pitchLane))
        {
            selectPitchSlurForEditing (core::sequencing::ExpressionState::defaultPitchLaneId(),
                                       slur->slurId,
                                       slur->singleVoiceEdit);
            return;
        }

        if (! selectedNoteIds_.empty())
        {
            const auto vibrato = std::find_if (pitchLane->vibratoExpressions().begin(),
                                               pitchLane->vibratoExpressions().end(),
                                               [this] (const auto& candidate)
                                               {
                                                   return sourceNoteSetMatchesSelection (candidate.sourceNoteIds(), selectedNoteIds_);
                                               });
            if (vibrato != pitchLane->vibratoExpressions().end())
            {
                selectedExpressionLaneId_ = core::sequencing::ExpressionState::defaultPitchLaneId();
                selectedVibratoId_ = vibrato->id();
                selectedPitchSlurId_.reset();
                selectedPitchSlurSingleVoiceEdit_ = false;
                selectedPhraseEnvelopeId_.reset();
                selectedCyclicClipId_.reset();
                pitchSlurEditPrefix_ = false;
                expressionEditPrefixTargetsCyclic_ = true;
                expressionOverlayCache_ = {};
                expressionSelectionChanged_();
                repaint();
                return;
            }
        }

        if (! additive)
        {
            selectedPitchSlurId_.reset();
            selectedPitchSlurSingleVoiceEdit_ = false;
            selectedVibratoId_.reset();
            pitchSlurEditPrefix_ = false;
            expressionOverlayCache_ = {};
            expressionSelectionChanged_();
        }
    }

    void updateExpressionObjectSelectionForCurrentNotes (const core::sequencing::MidiClip& clip, bool additive)
    {
        if (! expressionModeEnabled_)
            return;

        if (pitchExpressionLaneSelected())
            selectPitchExpressionObjectsForCurrentNoteSelection (clip, additive);
        else
            selectExpressionClipsForCurrentNoteSelection (clip);
    }

    void addDefaultPhraseEnvelopeStages (core::sequencing::PhraseEnvelopeClip& envelope,
                                         core::time::TickDuration grid,
                                         core::sequencing::ExpressionLanePolarity polarity) const
    {
        auto segment = core::sequencing::PhraseEnvelopeActiveSegment::decay;
        (void) core::sequencing::editPhraseEnvelope (envelope,
                                                     core::sequencing::PhraseEnvelopeEditKey::decay,
                                                     core::sequencing::PhraseEnvelopeEditDirection::right,
                                                     grid,
                                                     polarity,
                                                     segment);

        segment = core::sequencing::PhraseEnvelopeActiveSegment::release;
        (void) core::sequencing::editPhraseEnvelope (envelope,
                                                     core::sequencing::PhraseEnvelopeEditKey::release,
                                                     core::sequencing::PhraseEnvelopeEditDirection::left,
                                                     grid,
                                                     polarity,
                                                     segment);
    }

    void addDefaultCyclicExpressionShape (core::sequencing::CyclicExpressionClip& cyclic,
                                          core::time::TickDuration grid)
    {
        const auto gridTicks = std::max<std::int64_t> (1, grid.ticks());
        const auto phraseTicks = cyclic.phraseRegion().duration().ticks();
        const auto releaseTicks = std::min (gridTicks, std::max<std::int64_t> (0, phraseTicks - gridTicks));

        cyclic.setFrequencyDivisionId (currentArpeggioSubdivisionId());
        cyclic.setMaxAmplitude (defaultCyclicExpressionAmplitude);

        if (releaseTicks > 0)
            cyclic.setReleaseTime (core::time::TickDuration::fromTicks (releaseTicks));
    }

    void addDefaultVibratoExpressionShape (core::sequencing::VibratoExpression& vibrato,
                                           core::time::TickDuration grid)
    {
        const auto gridTicks = std::max<std::int64_t> (1, grid.ticks());
        const auto phraseTicks = vibrato.phraseRegion().duration().ticks();
        const auto releaseTicks = std::min (gridTicks, std::max<std::int64_t> (0, phraseTicks - gridTicks));

        vibrato.setFrequencyDivisionId (currentArpeggioSubdivisionId());
        vibrato.setAmplitudeSemitones (0.05);

        if (releaseTicks > 0)
            vibrato.setReleaseTime (core::time::TickDuration::fromTicks (releaseTicks));
    }

    bool pitchExpressionLaneSelected() const
    {
        return selectedExpressionLaneId_.has_value()
            && *selectedExpressionLaneId_ == core::sequencing::ExpressionState::defaultPitchLaneId();
    }

    void clearSelectedExpressionObjectsForNoteSelection()
    {
        selectedPhraseEnvelopeId_.reset();
        selectedCyclicClipId_.reset();
        selectedVibratoId_.reset();

        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
    }

    std::optional<core::sequencing::ExpressionClipId> nextVibratoId (const core::sequencing::ExpressionLane& lane) const
    {
        for (auto index = lane.vibratoExpressions().size() + 1; index < lane.vibratoExpressions().size() + 4096; ++index)
        {
            auto id = core::sequencing::ExpressionClipId { "vib-" + std::to_string (index) };
            if (lane.findVibratoExpression (id) == nullptr)
                return id;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::ExpressionBlockId> nextVibratoBlockId (const core::sequencing::ExpressionLane& lane) const
    {
        for (auto index = lane.vibratoExpressions().size() + 1; index < lane.vibratoExpressions().size() + 4096; ++index)
        {
            auto id = core::sequencing::ExpressionBlockId { "vib-block-" + std::to_string (index) };
            const auto exists = std::any_of (lane.vibratoExpressions().begin(), lane.vibratoExpressions().end(), [&] (const auto& vibrato)
            {
                return vibrato.blockId().has_value() && *vibrato.blockId() == id;
            });
            if (! exists)
                return id;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::VibratoExpression> activeVibratoExpression (const core::sequencing::ExpressionLane& lane) const
    {
        if (selectedVibratoId_.has_value())
            if (const auto* vibrato = lane.findVibratoExpression (*selectedVibratoId_))
                return *vibrato;

        return std::nullopt;
    }

    std::optional<core::sequencing::ExpressionClipId> nextPitchSlurId (const core::sequencing::ExpressionLane& lane,
                                                                       std::string_view prefix = "slur") const
    {
        for (auto index = lane.pitchSlurs().size() + 1; index < lane.pitchSlurs().size() + 4096; ++index)
        {
            auto id = core::sequencing::ExpressionClipId { std::string { prefix } + "-" + std::to_string (index) };
            if (lane.findPitchSlur (id) == nullptr)
                return id;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::ExpressionBlockId> nextPitchSlurBlockId (const core::sequencing::ExpressionLane& lane) const
    {
        for (auto index = lane.pitchSlurs().size() + 1; index < lane.pitchSlurs().size() + 4096; ++index)
        {
            auto id = core::sequencing::ExpressionBlockId { "slur-block-" + std::to_string (index) };
            const auto exists = std::any_of (lane.pitchSlurs().begin(), lane.pitchSlurs().end(), [&] (const auto& slur)
            {
                return slur.blockId().has_value() && *slur.blockId() == id;
            });
            if (! exists)
                return id;
        }

        return std::nullopt;
    }

    std::string uniquePitchSlurPrefix (const core::sequencing::ExpressionLane& lane) const
    {
        for (auto index = lane.pitchSlurs().size() + 1; index < lane.pitchSlurs().size() + 4096; ++index)
        {
            const auto prefix = "slur-" + std::to_string (index);
            auto collides = false;
            for (auto pairIndex = std::size_t { 1 }; pairIndex <= selectedNoteIds_.size(); ++pairIndex)
            {
                if (lane.findPitchSlur (core::sequencing::ExpressionClipId { prefix + "-" + std::to_string (pairIndex) }) != nullptr)
                {
                    collides = true;
                    break;
                }
            }
            if (! collides)
                return prefix;
        }

        return "slur";
    }

    std::optional<std::vector<std::vector<core::sequencing::PitchSlurNotePair>>> pitchSlurPairsForSelectedNotes (
        const core::sequencing::MidiClip& clip) const
    {
        if (selectedNoteIds_.size() < 2)
            return std::nullopt;

        std::map<std::int64_t, std::vector<std::string>> notesByStart;
        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip.findNoteById (noteId);
            if (note == nullptr)
                return std::nullopt;

            notesByStart[note->startInClip().ticks()].push_back (noteId);
        }

        if (notesByStart.size() < 2)
            return std::nullopt;

        std::vector<std::vector<core::sequencing::PitchSlurNotePair>> result;
        try
        {
            for (auto sourceGroup = notesByStart.begin(), destinationGroup = std::next (sourceGroup);
                 destinationGroup != notesByStart.end();
                 ++sourceGroup, ++destinationGroup)
            {
                const auto& sourceNoteIds = sourceGroup->second;
                const auto& destinationNoteIds = destinationGroup->second;
                if (sourceNoteIds.size() != destinationNoteIds.size())
                    return std::nullopt;

                if (sourceNoteIds.size() == 1)
                {
                    result.push_back ({ core::sequencing::PitchSlurNotePair {
                        sourceNoteIds.front(),
                        destinationNoteIds.front()
                    } });
                }
                else
                {
                    result.push_back (core::sequencing::pairNotesByRegister (clip, sourceNoteIds, destinationNoteIds));
                }
            }
        }
        catch (...)
        {
            return std::nullopt;
        }

        return result.empty() ? std::nullopt : std::optional { std::move (result) };
    }

    static const core::sequencing::PitchSlur* findPitchSlurForPair (
        const core::sequencing::ExpressionLane& lane,
        const core::sequencing::PitchSlurNotePair& pair) noexcept
    {
        const auto match = std::find_if (lane.pitchSlurs().begin(),
                                         lane.pitchSlurs().end(),
                                         [&] (const auto& slur)
                                         {
                                             return slur.sourceNoteId() == pair.sourceNoteId
                                                 && slur.destinationNoteId() == pair.destinationNoteId;
                                         });
        return match == lane.pitchSlurs().end() ? nullptr : &*match;
    }

    struct PitchSlurSelection
    {
        core::sequencing::ExpressionClipId slurId;
        bool singleVoiceEdit = false;
    };

    std::optional<PitchSlurSelection> existingPitchSlurIdForSelectedNotes (
        const core::sequencing::MidiClip& clip,
        const core::sequencing::ExpressionLane& lane) const
    {
        const auto groups = pitchSlurPairsForSelectedNotes (clip);
        if (! groups.has_value())
            return std::nullopt;

        auto selectedSlur = std::optional<PitchSlurSelection> {};
        auto pairCount = std::size_t {};
        auto selectedSlurHasBlock = false;
        for (const auto& group : *groups)
            for (const auto& pair : group)
            {
                const auto* slur = findPitchSlurForPair (lane, pair);
                if (slur == nullptr)
                    return std::nullopt;

                selectedSlur = PitchSlurSelection { slur->id(), false };
                selectedSlurHasBlock = slur->blockId().has_value();
                ++pairCount;
            }

        if (selectedSlur.has_value())
            selectedSlur->singleVoiceEdit = pairCount == 1 && selectedSlurHasBlock;

        return selectedSlur;
    }

    bool selectPitchSlurForEditing (const core::sequencing::ExpressionLaneId& laneId,
                                    const core::sequencing::ExpressionClipId& slurId,
                                    bool singleVoiceEdit = false)
    {
        selectedExpressionLaneId_ = laneId;
        selectedPitchSlurId_ = slurId;
        selectedVibratoId_.reset();
        selectedPhraseEnvelopeId_.reset();
        selectedCyclicClipId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = singleVoiceEdit;
        pitchSlurEditPrefix_ = true;
        expressionEditPrefix_.reset();
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    static core::sequencing::ExpressionCurveShape nextExpressionCurveShape (core::sequencing::ExpressionCurveShape shape, bool forward) noexcept
    {
        constexpr core::sequencing::ExpressionCurveShape cycle[] {
            core::sequencing::ExpressionCurveShape::linear,
            core::sequencing::ExpressionCurveShape::logarithmic,
            core::sequencing::ExpressionCurveShape::exponential
        };

        auto index = std::size_t {};
        for (; index < std::size (cycle); ++index)
            if (cycle[index] == shape)
                break;

        if (index >= std::size (cycle))
            index = 0;

        return forward
            ? cycle[(index + 1) % std::size (cycle)]
            : cycle[(index + std::size (cycle) - 1) % std::size (cycle)];
    }

    bool commitCyclicExpressionEdit (const core::sequencing::ExpressionLaneId& laneId,
                                     const std::optional<core::sequencing::ExpressionClipId>& previousCyclicId,
                                     const core::sequencing::CyclicExpressionClip& cyclic)
    {
        if (! selectedClip_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (laneId);
        if (lane == nullptr)
            return false;

        auto replacesExistingCyclic = previousCyclicId.has_value();
        if (previousCyclicId.has_value())
        {
            const auto exists = std::any_of (lane->cyclicClips().begin(), lane->cyclicClips().end(), [&] (const auto& existing) {
                return existing.id() == *previousCyclicId;
            });
            if (exists)
                lane->removeCyclicClip (*previousCyclicId);
        }

        const auto duplicate = std::any_of (lane->cyclicClips().begin(), lane->cyclicClips().end(), [&] (const auto& existing) {
            return existing.id() == cyclic.id();
        });
        if (duplicate)
        {
            replacesExistingCyclic = true;
            lane->removeCyclicClip (cyclic.id());
        }

        try
        {
            lane->addCyclicClip (cyclic);
        }
        catch (...)
        {
            return false;
        }

        const auto committed = replacesExistingCyclic
            ? appServices_.replaceCyclicExpressionClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                laneId,
                previousCyclicId,
                cyclic)
            : appServices_.addCyclicExpressionClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                laneId,
                cyclic);
        if (! committed)
        {
            return false;
        }

        selectedCyclicClipId_ = cyclic.id();
        selectedPhraseEnvelopeId_.reset();
        selectedPitchSlurId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = false;
        selectedVibratoId_.reset();
        pitchSlurEditPrefix_ = false;
        expressionEditPrefixTargetsCyclic_ = true;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    bool ensureDefaultVolumeExpressionRoute (const core::sequencing::ExpressionLaneId& laneId,
                                             const core::sequencing::ExpressionLane& lane)
    {
        if (! selectedClip_.has_value()
            || laneId != core::sequencing::ExpressionState::defaultVolumeLaneId()
            || ! lane.routes().empty())
        {
            return true;
        }

        for (auto index = lane.routes().size() + 1; index < lane.routes().size() + 1024; ++index)
        {
            auto routeId = core::sequencing::ExpressionRouteId {
                index == 1 ? "route-volume" : "route-volume-" + std::to_string (index)
            };
            if (lane.findRoute (routeId) != nullptr)
                continue;

            return appServices_.addExpressionRoute (
                selectedClip_->trackId,
                selectedClip_->clipId,
                laneId,
                core::sequencing::ExpressionRoute {
                    std::move (routeId),
                    core::sequencing::ExpressionDestination::trackVolume (selectedClip_->trackId),
                    0.0,
                    1.0
                });
        }

        return false;
    }

    static core::sequencing::CyclicWaveShape nextCyclicWaveShape (core::sequencing::CyclicWaveShape shape, bool forward) noexcept
    {
        constexpr core::sequencing::CyclicWaveShape cycle[] {
            core::sequencing::CyclicWaveShape::sine,
            core::sequencing::CyclicWaveShape::triangle,
            core::sequencing::CyclicWaveShape::rampUp,
            core::sequencing::CyclicWaveShape::rampDown,
            core::sequencing::CyclicWaveShape::square
        };

        auto index = std::size_t {};
        for (; index < std::size (cycle); ++index)
            if (cycle[index] == shape)
                break;

        if (index >= std::size (cycle))
            index = 0;

        return forward
            ? cycle[(index + 1) % std::size (cycle)]
            : cycle[(index + std::size (cycle) - 1) % std::size (cycle)];
    }

    bool createPitchSlursFromSelection()
    {
        if (! selectedClip_.has_value() || selectedNoteIds_.size() < 2)
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto pitchSlurGroups = pitchSlurPairsForSelectedNotes (*clip);
        if (! pitchSlurGroups.has_value())
            return false;

        auto expression = clip->expressionState();
        const auto pitchLaneId = core::sequencing::ExpressionState::defaultPitchLaneId();
        auto* lane = expression.findLane (pitchLaneId);
        if (lane == nullptr)
            return false;

        if (const auto existingSlur = existingPitchSlurIdForSelectedNotes (*clip, *lane))
            return selectPitchSlurForEditing (pitchLaneId, existingSlur->slurId, existingSlur->singleVoiceEdit);

        std::vector<core::sequencing::PitchSlur> slurs;
        auto selectedSlurId = std::optional<core::sequencing::ExpressionClipId> {};
        try
        {
            for (const auto& group : *pitchSlurGroups)
            {
                std::vector<core::sequencing::PitchSlurNotePair> missingPairs;
                missingPairs.reserve (group.size());
                for (const auto& pair : group)
                    if (findPitchSlurForPair (*lane, pair) == nullptr)
                        missingPairs.push_back (pair);

                if (missingPairs.empty())
                    continue;

                if (missingPairs.size() == group.size() && group.size() > 1)
                {
                    std::vector<std::string> sourceNoteIds;
                    std::vector<std::string> destinationNoteIds;
                    sourceNoteIds.reserve (group.size());
                    destinationNoteIds.reserve (group.size());
                    for (const auto& pair : group)
                    {
                        sourceNoteIds.push_back (pair.sourceNoteId);
                        destinationNoteIds.push_back (pair.destinationNoteId);
                    }

                    const auto blockId = nextPitchSlurBlockId (*lane);
                    if (! blockId.has_value())
                        return false;

                    auto blockSlurs = core::sequencing::createLegatoPitchSlurBlock (*clip,
                                                                                    sourceNoteIds,
                                                                                    destinationNoteIds,
                                                                                    *blockId,
                                                                                    uniquePitchSlurPrefix (*lane));
                    for (const auto& slur : blockSlurs)
                    {
                        lane->addPitchSlur (slur);
                        selectedSlurId = slur.id();
                    }
                    slurs.insert (slurs.end(),
                                  std::make_move_iterator (blockSlurs.begin()),
                                  std::make_move_iterator (blockSlurs.end()));
                }
                else
                {
                    for (const auto& pair : missingPairs)
                    {
                        const auto id = nextPitchSlurId (*lane);
                        if (! id.has_value())
                            return false;

                        core::sequencing::PitchSlur slur { *id, pair.sourceNoteId, pair.destinationNoteId };
                        slur.setSlurTime (core::time::TickDuration {});
                        slur.setLegatoNoRetrigger (true);
                        lane->addPitchSlur (slur);
                        selectedSlurId = slur.id();
                        slurs.push_back (std::move (slur));
                    }
                }
            }

        }
        catch (...)
        {
            return false;
        }

        if (slurs.empty())
            return false;

        if (! selectedSlurId.has_value())
            selectedSlurId = slurs.back().id();

        if (! appServices_.addPitchSlurs (selectedClip_->trackId,
                                          selectedClip_->clipId,
                                          pitchLaneId,
                                          std::move (slurs)))
        {
            return false;
        }

        return selectPitchSlurForEditing (pitchLaneId, *selectedSlurId);
    }

    bool commitVibratoExpressionEdit (const core::sequencing::VibratoExpression& vibrato)
    {
        if (! selectedClip_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr)
            return false;

        const auto pitchLaneId = core::sequencing::ExpressionState::defaultPitchLaneId();
        const auto committed = lane->findVibratoExpression (vibrato.id()) != nullptr
            ? appServices_.replaceVibratoExpression (
                selectedClip_->trackId,
                selectedClip_->clipId,
                pitchLaneId,
                vibrato)
            : appServices_.addVibratoExpression (
                selectedClip_->trackId,
                selectedClip_->clipId,
                pitchLaneId,
                vibrato);
        if (! committed)
        {
            return false;
        }

        selectedExpressionLaneId_ = pitchLaneId;
        selectedVibratoId_ = vibrato.id();
        selectedPitchSlurId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = false;
        selectedPhraseEnvelopeId_.reset();
        selectedCyclicClipId_.reset();
        pitchSlurEditPrefix_ = false;
        expressionEditPrefixTargetsCyclic_ = true;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    bool editSelectedVibratoExpression (core::sequencing::PhraseEnvelopeEditKey editKey,
                                        core::sequencing::PhraseEnvelopeEditDirection direction)
    {
        if (! selectedClip_.has_value() || ! pitchExpressionLaneSelected())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr)
            return false;

        auto vibrato = activeVibratoExpression (*lane);
        auto createdVibrato = false;
        if (! vibrato.has_value())
        {
            const auto creating =
                (editKey == core::sequencing::PhraseEnvelopeEditKey::attack
                 && (direction == core::sequencing::PhraseEnvelopeEditDirection::left
                     || direction == core::sequencing::PhraseEnvelopeEditDirection::right))
                || (editKey == core::sequencing::PhraseEnvelopeEditKey::decay
                    && direction == core::sequencing::PhraseEnvelopeEditDirection::up);
            if (! creating)
                return false;

            const auto id = nextVibratoId (*lane);
            const auto region = core::sequencing::phraseRegionForSelectedNotes (*clip, selectedNoteIds_);
            if (! id.has_value() || ! region.has_value())
                return false;

            vibrato = core::sequencing::VibratoExpression { *id, selectedNoteIds_, *region };
            addDefaultVibratoExpressionShape (*vibrato, core::time::TickDuration::fromTicks (snapTicks()));
            if (selectedNoteIds_.size() > 1)
                if (const auto blockId = nextVibratoBlockId (*lane))
                    vibrato->setBlockId (*blockId);
            createdVibrato = true;
        }

        const auto grid = core::time::TickDuration::fromTicks (snapTicks());
        try
        {
            if (editKey == core::sequencing::PhraseEnvelopeEditKey::decay)
            {
                if (createdVibrato && direction == core::sequencing::PhraseEnvelopeEditDirection::up)
                    return commitVibratoExpressionEdit (*vibrato);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::up)
                    vibrato->setAmplitudeSemitones (vibrato->amplitudeSemitones() + 0.05);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::down)
                    vibrato->setAmplitudeSemitones (std::max (0.0, vibrato->amplitudeSemitones() - 0.05));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::attack)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::right)
                    vibrato->setAttackTime (vibrato->attackTime() + grid);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::left)
                    vibrato->setAttackTime (core::time::TickDuration::fromTicks (std::max<std::int64_t> (0, vibrato->attackTime().ticks() - grid.ticks())));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::release)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::left)
                    vibrato->setReleaseTime (vibrato->releaseTime() + grid);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::right)
                    vibrato->setReleaseTime (core::time::TickDuration::fromTicks (std::max<std::int64_t> (0, vibrato->releaseTime().ticks() - grid.ticks())));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::force)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::left)
                    vibrato->setFrequencyDivisionId (core::sequencing::longerArpeggioSubdivisionId (vibrato->frequencyDivisionId(), appServices_.project().rhythmSettings()));
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::right)
                    vibrato->setFrequencyDivisionId (core::sequencing::shorterArpeggioSubdivisionId (vibrato->frequencyDivisionId(), appServices_.project().rhythmSettings()));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::curve)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::up || direction == core::sequencing::PhraseEnvelopeEditDirection::down)
                    vibrato->setWaveShape (nextCyclicWaveShape (vibrato->waveShape(), direction == core::sequencing::PhraseEnvelopeEditDirection::up));
                else
                    return false;
            }
        }
        catch (...)
        {
            return false;
        }

        return commitVibratoExpressionEdit (*vibrato);
    }

    bool editSelectedPitchSlurTime (core::sequencing::PhraseEnvelopeEditDirection direction)
    {
        if (! selectedClip_.has_value() || ! selectedPitchSlurId_.has_value())
            return false;

        if (direction != core::sequencing::PhraseEnvelopeEditDirection::left
            && direction != core::sequencing::PhraseEnvelopeEditDirection::right)
        {
            return false;
        }

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr)
            return false;

        auto* slur = lane->findPitchSlur (*selectedPitchSlurId_);
        if (slur == nullptr)
            return false;

        const auto grid = core::time::TickDuration::fromTicks (snapTicks());
        const auto newTicks = direction == core::sequencing::PhraseEnvelopeEditDirection::right
            ? slur->slurTime().ticks() + grid.ticks()
            : std::max<std::int64_t> (0, slur->slurTime().ticks() - grid.ticks());

        std::vector<core::sequencing::PitchSlur> replacements;
        try
        {
            core::sequencing::PitchSlurBlockSettings settings {
                slur->blockId(),
                core::time::TickDuration::fromTicks (newTicks),
                slur->curveShape(),
                slur->legatoNoRetrigger()
            };

            const auto editSharedBlock = slur->blockId().has_value()
                && ! slur->hasVoiceOverride()
                && ! selectedPitchSlurSingleVoiceEdit_;
            if (editSharedBlock)
            {
                core::sequencing::applySharedSlurBlockSettings (*lane, settings);
                for (const auto& replacement : lane->pitchSlurs())
                    if (replacement.blockId() == settings.blockId && ! replacement.hasVoiceOverride())
                        replacements.push_back (replacement);
            }
            else
            {
                core::sequencing::applySlurVoiceOverride (*lane, slur->id(), settings);
                if (const auto* replacement = lane->findPitchSlur (*selectedPitchSlurId_))
                    replacements.push_back (*replacement);
            }
        }
        catch (...)
        {
            return false;
        }

        if (replacements.empty())
            return false;

        if (! appServices_.replacePitchSlurs (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::ExpressionState::defaultPitchLaneId(),
                std::move (replacements)))
        {
            return false;
        }

        expressionOverlayCache_ = {};
        repaint();
        return true;
    }

    bool editSelectedPitchSlurCurve (core::sequencing::PhraseEnvelopeEditDirection direction)
    {
        if (! selectedClip_.has_value() || ! selectedPitchSlurId_.has_value())
            return false;

        if (direction != core::sequencing::PhraseEnvelopeEditDirection::up
            && direction != core::sequencing::PhraseEnvelopeEditDirection::down)
        {
            return false;
        }

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr)
            return false;

        auto* slur = lane->findPitchSlur (*selectedPitchSlurId_);
        if (slur == nullptr)
            return false;

        std::vector<core::sequencing::PitchSlur> replacements;
        try
        {
            core::sequencing::PitchSlurBlockSettings settings {
                slur->blockId(),
                slur->slurTime(),
                nextExpressionCurveShape (slur->curveShape(), direction == core::sequencing::PhraseEnvelopeEditDirection::up),
                slur->legatoNoRetrigger()
            };

            const auto editSharedBlock = slur->blockId().has_value()
                && ! slur->hasVoiceOverride()
                && ! selectedPitchSlurSingleVoiceEdit_;
            if (editSharedBlock)
            {
                core::sequencing::applySharedSlurBlockSettings (*lane, settings);
                for (const auto& replacement : lane->pitchSlurs())
                    if (replacement.blockId() == settings.blockId && ! replacement.hasVoiceOverride())
                        replacements.push_back (replacement);
            }
            else
            {
                core::sequencing::applySlurVoiceOverride (*lane, slur->id(), settings);
                if (const auto* replacement = lane->findPitchSlur (*selectedPitchSlurId_))
                    replacements.push_back (*replacement);
            }
        }
        catch (...)
        {
            return false;
        }

        if (replacements.empty())
            return false;

        if (! appServices_.replacePitchSlurs (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::ExpressionState::defaultPitchLaneId(),
                std::move (replacements)))
        {
            return false;
        }

        expressionOverlayCache_ = {};
        repaint();
        return true;
    }

    bool editSelectedCyclicExpression (core::sequencing::PhraseEnvelopeEditKey editKey,
                                       core::sequencing::PhraseEnvelopeEditDirection direction)
    {
        if (! selectedExpressionLaneId_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = selectedExpressionLane (*clip);
        if (lane == nullptr)
            return false;

        const auto laneId = lane->id();
        auto cyclic = activeCyclicExpression (*lane);
        auto previousCyclicId = cyclic.has_value()
            ? std::optional<core::sequencing::ExpressionClipId> { cyclic->id() }
            : std::optional<core::sequencing::ExpressionClipId> {};

        if (! cyclic.has_value())
        {
            const auto creating = (editKey == core::sequencing::PhraseEnvelopeEditKey::attack
                                   && (direction == core::sequencing::PhraseEnvelopeEditDirection::left
                                       || direction == core::sequencing::PhraseEnvelopeEditDirection::right))
                || (editKey == core::sequencing::PhraseEnvelopeEditKey::decay
                    && direction == core::sequencing::PhraseEnvelopeEditDirection::up);
            if (! creating)
                return false;

            const auto id = nextCyclicClipId (*lane);
            const auto region = core::sequencing::phraseRegionForSelectedNotes (*clip, selectedNoteIds_);
            if (! id.has_value() || ! region.has_value())
                return false;

            if (! ensureDefaultVolumeExpressionRoute (laneId, *lane))
                return false;

            clip = selectedMidiClip();
            if (clip == nullptr)
                return false;

            lane = selectedExpressionLane (*clip);
            if (lane == nullptr)
                return false;

            cyclic = core::sequencing::CyclicExpressionClip { *id, selectedNoteIds_, *region };
            addDefaultCyclicExpressionShape (*cyclic, core::time::TickDuration::fromTicks (snapTicks()));
        }

        const auto grid = core::time::TickDuration::fromTicks (snapTicks());
        try
        {
            if (editKey == core::sequencing::PhraseEnvelopeEditKey::attack)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::right)
                    cyclic->setAttackTime (cyclic->attackTime() + grid);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::left)
                    cyclic->setAttackTime (core::time::TickDuration::fromTicks (std::max<std::int64_t> (0, cyclic->attackTime().ticks() - grid.ticks())));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::decay)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::up)
                    cyclic->setMaxAmplitude (cyclic->maxAmplitude() + 0.05);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::down)
                    cyclic->setMaxAmplitude (cyclic->maxAmplitude() - 0.05);
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::release)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::left)
                    cyclic->setReleaseTime (cyclic->releaseTime() + grid);
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::right)
                    cyclic->setReleaseTime (core::time::TickDuration::fromTicks (std::max<std::int64_t> (0, cyclic->releaseTime().ticks() - grid.ticks())));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::force)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::left)
                    cyclic->setFrequencyDivisionId (core::sequencing::longerArpeggioSubdivisionId (cyclic->frequencyDivisionId(), appServices_.project().rhythmSettings()));
                else if (direction == core::sequencing::PhraseEnvelopeEditDirection::right)
                    cyclic->setFrequencyDivisionId (core::sequencing::shorterArpeggioSubdivisionId (cyclic->frequencyDivisionId(), appServices_.project().rhythmSettings()));
                else
                    return false;
            }
            else if (editKey == core::sequencing::PhraseEnvelopeEditKey::curve)
            {
                if (direction == core::sequencing::PhraseEnvelopeEditDirection::up || direction == core::sequencing::PhraseEnvelopeEditDirection::down)
                    cyclic->setWaveShape (nextCyclicWaveShape (cyclic->waveShape(), direction == core::sequencing::PhraseEnvelopeEditDirection::up));
                else
                    return false;
            }
        }
        catch (...)
        {
            return false;
        }

        return commitCyclicExpressionEdit (laneId, previousCyclicId, *cyclic);
    }

    std::vector<core::sequencing::ReleaseGhostNote> selectedReleaseGhostsForEnvelope (const core::sequencing::MidiClip& clip) const
    {
        return expressionReleaseModeEnabled_ ? releaseGhostsForClip (clip) : std::vector<core::sequencing::ReleaseGhostNote> {};
    }

    bool commitPhraseEnvelopeEdit (const core::sequencing::ExpressionLaneId& laneId,
                                   const std::optional<core::sequencing::ExpressionClipId>& previousEnvelopeId,
                                   const core::sequencing::PhraseEnvelopeClip& envelope)
    {
        if (! selectedClip_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        auto expression = clip->expressionState();
        auto* lane = expression.findLane (laneId);
        if (lane == nullptr)
            return false;

        auto replacesExistingEnvelope = previousEnvelopeId.has_value();
        if (previousEnvelopeId.has_value() && lane->findPhraseEnvelopeClip (*previousEnvelopeId) != nullptr)
            lane->removePhraseEnvelopeClip (*previousEnvelopeId);

        if (lane->findPhraseEnvelopeClip (envelope.id()) != nullptr)
        {
            replacesExistingEnvelope = true;
            lane->removePhraseEnvelopeClip (envelope.id());
        }

        lane->addPhraseEnvelopeClip (envelope);
        const auto committed = replacesExistingEnvelope
            ? appServices_.replacePhraseEnvelopeClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                laneId,
                previousEnvelopeId,
                envelope)
            : appServices_.addPhraseEnvelopeClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                laneId,
                envelope);
        if (! committed)
        {
            return false;
        }

        selectedPhraseEnvelopeId_ = envelope.id();
        selectedCyclicClipId_.reset();
        selectedPitchSlurId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = false;
        selectedVibratoId_.reset();
        pitchSlurEditPrefix_ = false;
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    bool editSelectedPhraseEnvelope (core::sequencing::PhraseEnvelopeEditKey editKey,
                                     core::sequencing::PhraseEnvelopeEditDirection direction)
    {
        if (! selectedExpressionLaneId_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = selectedExpressionLane (*clip);
        if (lane == nullptr)
            return false;

        const auto laneId = lane->id();
        auto envelope = activePhraseEnvelope (*lane);
        auto previousEnvelopeId = envelope.has_value()
            ? std::optional<core::sequencing::ExpressionClipId> { envelope->id() }
            : std::optional<core::sequencing::ExpressionClipId> {};

        if (! envelope.has_value())
        {
            const auto createsEnvelope =
                (editKey == core::sequencing::PhraseEnvelopeEditKey::attack
                 && (direction == core::sequencing::PhraseEnvelopeEditDirection::left
                     || direction == core::sequencing::PhraseEnvelopeEditDirection::right))
                || (editKey == core::sequencing::PhraseEnvelopeEditKey::release
                    && (direction == core::sequencing::PhraseEnvelopeEditDirection::left
                        || direction == core::sequencing::PhraseEnvelopeEditDirection::right))
                || (editKey == core::sequencing::PhraseEnvelopeEditKey::decay
                    && (direction == core::sequencing::PhraseEnvelopeEditDirection::left
                        || direction == core::sequencing::PhraseEnvelopeEditDirection::right
                        || direction == core::sequencing::PhraseEnvelopeEditDirection::up
                        || direction == core::sequencing::PhraseEnvelopeEditDirection::down));
            if (! createsEnvelope)
            {
                return false;
            }

            if (! ensureDefaultVolumeExpressionRoute (laneId, *lane))
                return false;

            clip = selectedMidiClip();
            if (clip == nullptr)
                return false;

            lane = selectedExpressionLane (*clip);
            if (lane == nullptr)
                return false;

            const auto id = nextPhraseEnvelopeId (*lane);
            if (! id.has_value())
                return false;

            const auto ghosts = selectedReleaseGhostsForEnvelope (*clip);
            const auto region = core::sequencing::phraseRegionForSelectedNotes (*clip, selectedNoteIds_, ghosts);
            if (! region.has_value())
                return false;

            const auto storedLevel = core::sequencing::evaluateExpressionLaneAt (
                *lane,
                region->start(),
                core::sequencing::ExpressionEvaluationContext { appServices_.project().rhythmSettings(), true });

            envelope = core::sequencing::createPhraseEnvelopeForSelection (*id,
                                                                           *clip,
                                                                           selectedNoteIds_,
                                                                           core::time::TickDuration::fromTicks (snapTicks()),
                                                                           storedLevel,
                                                                           lane->polarity(),
                                                                           ghosts);
            if (! envelope.has_value())
                return false;

            const auto grid = core::time::TickDuration::fromTicks (snapTicks());
            auto activeSegment = core::sequencing::PhraseEnvelopeActiveSegment::attack;
            if (editKey == core::sequencing::PhraseEnvelopeEditKey::attack)
                addDefaultPhraseEnvelopeStages (*envelope, grid, lane->polarity());

            const auto initialKeyIsDefaultAttack =
                editKey == core::sequencing::PhraseEnvelopeEditKey::attack
                && direction == core::sequencing::PhraseEnvelopeEditDirection::right;
            if (! initialKeyIsDefaultAttack)
            {
                if (! core::sequencing::editPhraseEnvelope (*envelope,
                                                            editKey,
                                                            direction,
                                                            grid,
                                                            lane->polarity(),
                                                            activeSegment))
                {
                    return false;
                }
            }

            activePhraseSegment_ = activeSegment;
            return commitPhraseEnvelopeEdit (laneId, previousEnvelopeId, *envelope);
        }

        auto activeSegment = activePhraseSegment_;
        if (! core::sequencing::editPhraseEnvelope (*envelope,
                                                    editKey,
                                                    direction,
                                                    core::time::TickDuration::fromTicks (snapTicks()),
                                                    lane->polarity(),
                                                    activeSegment))
        {
            return false;
        }

        activePhraseSegment_ = activeSegment;
        return commitPhraseEnvelopeEdit (laneId, previousEnvelopeId, *envelope);
    }

    bool deleteSelectedPhraseEnvelope()
    {
        if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value() || ! selectedPhraseEnvelopeId_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = clip->expressionState().findLane (*selectedExpressionLaneId_);
        if (lane == nullptr || lane->findPhraseEnvelopeClip (*selectedPhraseEnvelopeId_) == nullptr)
            return false;

        if (! appServices_.removePhraseEnvelopeClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                *selectedExpressionLaneId_,
                *selectedPhraseEnvelopeId_))
        {
            return false;
        }

        selectedPhraseEnvelopeId_.reset();
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    bool deleteSelectedPitchSlur()
    {
        if (! selectedClip_.has_value() || ! selectedPitchSlurId_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->findPitchSlur (*selectedPitchSlurId_) == nullptr)
            return false;

        if (! appServices_.removePitchSlur (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::ExpressionState::defaultPitchLaneId(),
                *selectedPitchSlurId_))
        {
            return false;
        }

        selectedPitchSlurId_.reset();
        selectedPitchSlurSingleVoiceEdit_ = false;
        pitchSlurEditPrefix_ = false;
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    bool deleteSelectedVibratoExpression()
    {
        if (! selectedClip_.has_value() || ! selectedVibratoId_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->findVibratoExpression (*selectedVibratoId_) == nullptr)
            return false;

        if (! appServices_.removeVibratoExpression (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::ExpressionState::defaultPitchLaneId(),
                *selectedVibratoId_))
        {
            return false;
        }

        selectedVibratoId_.reset();
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    bool deleteSelectedCyclicExpression()
    {
        if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value() || ! selectedCyclicClipId_.has_value())
            return false;

        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return false;

        const auto* lane = selectedExpressionLane (*clip);
        if (lane == nullptr)
            return false;

        const auto exists = std::any_of (lane->cyclicClips().begin(), lane->cyclicClips().end(), [&] (const auto& cyclic) {
            return cyclic.id() == *selectedCyclicClipId_;
        });
        if (! exists)
            return false;

        const auto removed = appServices_.removeCyclicExpressionClip (selectedClip_->trackId,
                                                                      selectedClip_->clipId,
                                                                      *selectedExpressionLaneId_,
                                                                      *selectedCyclicClipId_);
        if (! removed)
            return false;

        selectedCyclicClipId_.reset();
        expressionEditPrefixTargetsCyclic_ = false;
        expressionOverlayCache_ = {};
        expressionSelectionChanged_();
        repaint();
        return true;
    }

    enum class DragMode
    {
        move,
        resizeStart,
        resizeEnd
    };

    struct NoteHit
    {
        std::string noteId;
        juce::Rectangle<int> bounds;
    };

    struct NoteRenderBounds
    {
        juce::Rectangle<int> bounds;
        std::size_t segmentIndex = 0;
    };

    struct NoteVisualState
    {
        int midiPitch = 60;
        std::optional<core::music_theory::NoteName> spelling;
    };

    struct PianoLane
    {
        int midiPitch = 60;
        core::music_theory::NoteName spelling { core::music_theory::LetterName::c };
        bool nativeScale = true;
        bool usedAccidental = false;
        bool greyed = false;
    };

    struct PianoRollSegment
    {
        core::time::TickPosition localStart {};
        core::time::TickPosition localEnd {};
        core::sequencing::HarmonicContext context {
            core::music_theory::PitchClass::c(),
            "Major"
        };
        std::vector<PianoLane> lanes;
        bool insertedHeader = false;
    };

    struct SegmentLayout
    {
        std::size_t segmentIndex = 0;
        int headerX = 0;
        int headerWidth = pitchHeaderWidth;
        int gridStartX = pitchHeaderWidth;
        int gridEndX = pitchHeaderWidth;
    };

    struct LaneBackgroundCache
    {
        juce::Image image;
        std::size_t fingerprint = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    struct ExpressionStrip
    {
        juce::Rectangle<int> bounds;
        juce::Colour colour;
        std::optional<core::sequencing::ExpressionClipId> phraseEnvelopeId;
        std::optional<core::sequencing::ExpressionClipId> cyclicClipId;
        int partitionX = -1;
        int secondPartitionX = -1;
    };

    struct ExpressionOverlayCache
    {
        std::size_t fingerprint = 0;
        juce::Path curvePath;
        struct CyclicWaveform
        {
            core::sequencing::ExpressionClipId clipId;
            juce::Path path;
        };
        std::vector<CyclicWaveform> cyclicWaveforms;
        std::vector<ExpressionStrip> strips;
        std::vector<float> warningXs;
        juce::Rectangle<int> valueArea;
        core::sequencing::ExpressionLanePolarity polarity = core::sequencing::ExpressionLanePolarity::unipolar;
    };

    struct PlayheadLayoutCache
    {
        std::size_t fingerprint = 0;
        std::vector<PianoRollSegment> segments;
        std::vector<SegmentLayout> layouts;
    };

    struct LocalTickRange
    {
        std::int64_t startTicks = 0;
        std::int64_t endTicks = 0;
    };

    struct DragNoteState
    {
        std::string noteId;
        core::time::TickPosition start {};
        core::time::TickDuration duration {};
        core::music_theory::MidiPitch pitch { 60 };
        std::optional<core::music_theory::NoteName> spelling;
    };

    struct DragState
    {
        DragMode mode = DragMode::move;
        int mouseStartX = 0;
        int mouseStartY = 0;
        int currentX = 0;
        int currentY = 0;
        std::vector<DragNoteState> notes;
    };

    struct MarqueeState
    {
        juce::Point<int> start;
        juce::Point<int> current;
        bool additive = false;
    };

    const core::sequencing::MidiClip* selectedMidiClip() const
    {
        if (! selectedClip_.has_value())
            return nullptr;

        const auto* track = appServices_.project().findTrackById (selectedClip_->trackId);
        if (track == nullptr)
            return nullptr;

        return track->findClipById (selectedClip_->clipId);
    }

    core::sequencing::MidiClip* selectedMidiClip()
    {
        if (! selectedClip_.has_value())
            return nullptr;

        auto* track = appServices_.project().findTrackById (selectedClip_->trackId);
        if (track == nullptr)
            return nullptr;

        return track->findClipById (selectedClip_->clipId);
    }

    const core::sequencing::Track* selectedTrack() const
    {
        if (! selectedClip_.has_value())
            return nullptr;

        return appServices_.project().findTrackById (selectedClip_->trackId);
    }

    std::vector<core::sequencing::ReleaseGhostNote> releaseGhostsForClip (const core::sequencing::MidiClip& clip) const
    {
        const auto* track = selectedTrack();
        if (track == nullptr)
            return {};

        return core::sequencing::computeReleaseGhostNotes (appServices_.project(), *track, clip);
    }

    static core::time::TickDuration editableClipLength (const core::sequencing::MidiClip& clip)
    {
        return clip.sourceLength().ticks() > 0 ? clip.sourceLength() : clip.length();
    }

    bool isSelected (const std::string& noteId) const
    {
        return std::find (selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId) != selectedNoteIds_.end();
    }

    void toggleNoteSelection (const std::string& noteId)
    {
        const auto match = std::find (selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId);
        if (match == selectedNoteIds_.end())
            selectedNoteIds_.push_back (noteId);
        else
            selectedNoteIds_.erase (match);
    }

    void selectAllNotes()
    {
        selectedNoteIds_.clear();
        activeArpeggioSourceNotes_.clear();

        if (const auto* clip = selectedMidiClip())
        {
            for (const auto& note : clip->notes())
                selectedNoteIds_.push_back (note.id());

            updateExpressionObjectSelectionForCurrentNotes (*clip, false);
        }

        repaint();
    }

    void zoomHorizontally (float wheelDeltaY)
    {
        const auto multiplier = wheelDeltaY > 0.0f ? zoomStep : 1.0f / zoomStep;
        pixelsPerQuarter_ = std::clamp (static_cast<int> (std::lround (static_cast<float> (pixelsPerQuarter_) * multiplier)),
                                        minimumPixelsPerQuarter,
                                        maximumPixelsPerQuarter);
        resizedParentContent();
    }

    void zoomVertically (float wheelDeltaY)
    {
        const auto delta = wheelDeltaY > 0.0f ? 1 : -1;
        rowHeight_ = std::clamp (rowHeight_ + delta, minimumRowHeight, maximumRowHeight);
        resizedParentContent();
    }

    void resizedParentContent()
    {
        if (auto* parent = getParentComponent())
            parent->resized();

        repaint();
    }

    juce::Rectangle<int> marqueeBounds() const
    {
        if (! marqueeState_.has_value())
            return {};

        return juce::Rectangle<int>::leftTopRightBottom (
            std::min (marqueeState_->start.x, marqueeState_->current.x),
            std::min (marqueeState_->start.y, marqueeState_->current.y),
            std::max (marqueeState_->start.x, marqueeState_->current.x),
            std::max (marqueeState_->start.y, marqueeState_->current.y));
    }

    void selectNotesInMarquee (juce::Rectangle<int> bounds, bool additive)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "PianoRollContent::selectNotesInMarquee" };

        const auto* clip = selectedMidiClip();
        if (clip == nullptr || bounds.getWidth() < 3 || bounds.getHeight() < 3)
            return;

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        const auto ghosts = expressionModeEnabled_ && expressionReleaseModeEnabled_
            ? releaseGhostsForClip (*clip)
            : std::vector<core::sequencing::ReleaseGhostNote> {};
        if (! additive)
            selectedNoteIds_.clear();

        for (const auto& note : clip->notes())
        {
            const auto noteBounds = boundsForNote (note, segments, layouts);
            auto intersectsSelection = std::any_of (noteBounds.begin(), noteBounds.end(), [bounds] (const auto& rendered) {
                    return bounds.intersects (rendered.bounds);
                });

            if (! intersectsSelection)
            {
                const auto ghost = std::find_if (ghosts.begin(), ghosts.end(), [&note] (const auto& candidate) {
                    return candidate.noteId == note.id();
                });
                if (ghost != ghosts.end())
                {
                    const auto ghostBounds = boundsForGhost (note, *ghost, segments, layouts);
                    intersectsSelection = std::any_of (ghostBounds.begin(), ghostBounds.end(), [bounds] (const auto& rendered) {
                        return bounds.intersects (rendered.bounds);
                    });
                }
            }

            if (intersectsSelection)
            {
                if (! isSelected (note.id()))
                    selectedNoteIds_.push_back (note.id());
            }
        }

        if (! expressionModeEnabled_)
            return;

        updateExpressionObjectSelectionForCurrentNotes (*clip, additive);
    }

    std::vector<core::sequencing::MidiNote> selectedNotes() const
    {
        std::vector<core::sequencing::MidiNote> result;
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return result;

        for (const auto& noteId : selectedNoteIds_)
            if (const auto* note = clip->findNoteById (noteId))
                result.push_back (*note);

        std::stable_sort (result.begin(), result.end(), [] (const auto& lhs, const auto& rhs) {
            if (lhs.startInClip() == rhs.startInClip())
                return lhs.pitch().value() < rhs.pitch().value();

            return lhs.startInClip() < rhs.startInClip();
        });
        return result;
    }

public:
    bool copySelectedNotes()
    {
        if (selectedNoteIds_.empty())
            return false;

        noteClipboard_ = selectedNotes();
        activeArpeggioSourceNotes_.clear();
        return ! noteClipboard_.empty();
    }

    bool pasteCopiedNotes()
    {
        auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || noteClipboard_.empty())
            return false;

        const auto minStart = std::min_element (noteClipboard_.begin(), noteClipboard_.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.startInClip() < rhs.startInClip();
        })->startInClip();
        const auto maxEnd = std::max_element (noteClipboard_.begin(), noteClipboard_.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.endInClip() < rhs.endInClip();
        })->endInClip();
        const auto clipLength = editableClipLength (*clip);
        auto pasteStart = localPlayheadInClip (*clip).value_or (maxEnd + core::time::TickDuration::fromTicks (snapTicks()));
        pasteStart = core::time::TickPosition::fromTicks (roundToSnap (pasteStart.ticks(), snapTicks()));

        const auto copiedLength = maxEnd - minStart;
        if (pasteStart + copiedLength > core::time::TickPosition {} + clipLength)
            pasteStart = core::time::TickPosition::fromTicks (std::max<std::int64_t> (0, clipLength.ticks() - copiedLength.ticks()));

        std::vector<core::sequencing::MidiNote> pending;
        selectedNoteIds_.clear();
        for (const auto& copiedNote : noteClipboard_)
        {
            auto noteId = nextNoteId (*clip, pending);
            const auto start = pasteStart + (copiedNote.startInClip() - minStart);
            auto pasted = core::sequencing::MidiNote {
                noteId,
                copiedNote.pitch(),
                start,
                copiedNote.duration(),
                copiedNote.velocity(),
                copiedNote.spelling(),
                copiedNote.harmonicInterpretation()
            };

            if (pasted.endInClip() > core::time::TickPosition {} + clipLength)
                continue;

            if (runCommand (std::make_unique<core::commands::AddNoteCommand> (selectedClip_->trackId, selectedClip_->clipId, pasted)))
            {
                selectedNoteIds_.push_back (noteId);
                pending.push_back (std::move (pasted));
            }
        }

        activeArpeggioSourceNotes_.clear();
        frameNotesInViewport();
        repaint();
        return ! selectedNoteIds_.empty();
    }

private:
    juce::Point<float> firstEditableGridPoint() const
    {
        return {
            static_cast<float> (pitchHeaderWidth + 12),
            static_cast<float> (rulerHeight + std::max (1, rowHeight_ / 2))
        };
    }

    bool moveSelectedNotesHorizontally (bool later)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.empty())
            return false;

        auto movedAny = false;
        const auto gridTicks = snapTicks();
        const auto clipLength = editableClipLength (*clip);

        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip->findNoteById (noteId);
            if (note == nullptr)
                continue;

            const auto proposedTicks = note->startInClip().ticks() + (later ? gridTicks : -gridTicks);
            const auto snappedTicks = roundToSnap (proposedTicks, gridTicks);
            const auto clampedTicks = clampTicks (snappedTicks, 0, clipLength.ticks() - note->duration().ticks());
            const auto newStart = core::time::TickPosition::fromTicks (clampedTicks);
            if (newStart == note->startInClip())
                continue;

            if (runCommand (std::make_unique<core::commands::MoveNoteCommand> (
                    selectedClip_->trackId,
                    selectedClip_->clipId,
                    noteId,
                    newStart,
                    note->pitch(),
                    note->spelling())))
            {
                movedAny = true;
            }
        }

        activeArpeggioSourceNotes_.clear();
        repaint();
        return movedAny;
    }

    bool moveSelectedNotesVertically (bool upward)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.empty())
            return false;

        const auto segments = visiblePitchSegments (*clip);
        if (segments.empty())
            return false;

        auto movedAny = false;
        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip->findNoteById (noteId);
            if (note == nullptr)
                continue;

            const auto segmentIndex = segmentIndexForLocalTick (segments, note->startInClip());
            if (! segmentIndex.has_value())
                continue;

            const auto& lanes = segments[*segmentIndex].lanes;
            const auto laneIndex = laneIndexForNote (*note, lanes);
            if (! laneIndex.has_value())
                continue;

            const auto targetLaneIndex = std::clamp (*laneIndex + (upward ? -1 : 1),
                                                     0,
                                                     static_cast<int> (lanes.size()) - 1);
            const auto& targetLane = lanes[static_cast<std::size_t> (targetLaneIndex)];
            if (targetLane.midiPitch == note->pitch().value())
                continue;

            if (runCommand (std::make_unique<core::commands::MoveNoteCommand> (
                    selectedClip_->trackId,
                    selectedClip_->clipId,
                    noteId,
                    note->startInClip(),
                    core::music_theory::MidiPitch::fromValue (targetLane.midiPitch),
                    targetLane.spelling)))
            {
                movedAny = true;
            }
        }

        activeArpeggioSourceNotes_.clear();
        frameNotesInViewport();
        repaint();
        return movedAny;
    }

    bool fillScalarRunBetweenSelectedNotes()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.size() != 2)
            return false;

        auto notes = selectedNotes();
        if (notes.size() != 2)
            return false;

        const auto segments = visiblePitchSegments (*clip);
        const auto firstSegmentIndex = segmentIndexForLocalTick (segments, notes[0].startInClip());
        if (! firstSegmentIndex.has_value())
            return false;

        const auto& lanes = segments[*firstSegmentIndex].lanes;
        const auto firstLaneIndex = laneIndexForNote (notes[0], lanes);
        const auto secondLaneIndex = laneIndexForNote (notes[1], lanes);
        if (! firstLaneIndex.has_value() || ! secondLaneIndex.has_value() || *firstLaneIndex == *secondLaneIndex)
            return false;

        const auto gridTicks = snapTicks();
        const auto clipLength = editableClipLength (*clip);
        const auto firstStartTicks = roundToSnap (notes[0].startInClip().ticks(), gridTicks);
        const auto laneStep = *secondLaneIndex > *firstLaneIndex ? 1 : -1;
        const auto laneDistance = std::abs (*secondLaneIndex - *firstLaneIndex);
        if (laneDistance <= 0)
            return false;

        const auto targetSecondStartTicks = firstStartTicks + (gridTicks * laneDistance);
        if (targetSecondStartTicks + notes[1].duration().ticks() > clipLength.ticks())
            return false;

        const auto notesToCreate = laneDistance - 1;
        const auto noteAlreadyExists = [clip] (core::time::TickPosition start, int pitch)
        {
            return std::any_of (clip->notes().begin(), clip->notes().end(), [start, pitch] (const auto& note) {
                return note.startInClip() == start && note.pitch().value() == pitch;
            });
        };

        std::vector<core::sequencing::MidiNote> pendingNotes;
        pendingNotes.reserve (static_cast<std::size_t> (notesToCreate));

        for (auto step = 1; step <= notesToCreate; ++step)
        {
            const auto laneIndex = *firstLaneIndex + (laneStep * step);
            if (laneIndex < 0 || laneIndex >= static_cast<int> (lanes.size()))
                break;

            const auto& lane = lanes[static_cast<std::size_t> (laneIndex)];
            const auto start = core::time::TickPosition::fromTicks (firstStartTicks + (gridTicks * step));
            const auto duration = core::time::TickDuration::fromTicks (gridTicks);
            if (start + duration > core::time::TickPosition {} + clipLength)
                break;

            if (noteAlreadyExists (start, lane.midiPitch)
                || std::any_of (pendingNotes.begin(), pendingNotes.end(), [start, &lane] (const auto& note) {
                    return note.startInClip() == start && note.pitch().value() == lane.midiPitch;
                }))
            {
                continue;
            }

            const auto noteId = nextNoteId (*clip, pendingNotes);
            pendingNotes.push_back (core::sequencing::MidiNote {
                noteId,
                core::music_theory::MidiPitch::fromValue (lane.midiPitch),
                start,
                duration,
                notes[0].velocity(),
                lane.spelling
            });
        }

        auto changed = false;
        const auto targetSecondStart = core::time::TickPosition::fromTicks (targetSecondStartTicks);
        if (targetSecondStart != notes[1].startInClip())
        {
            changed = runCommand (std::make_unique<core::commands::MoveNoteCommand> (
                selectedClip_->trackId,
                selectedClip_->clipId,
                notes[1].id(),
                targetSecondStart,
                notes[1].pitch(),
                notes[1].spelling()));

            if (! changed)
                return false;
        }

        selectedNoteIds_.clear();
        selectedNoteIds_.push_back (notes[0].id());
        for (const auto& note : pendingNotes)
        {
            if (runCommand (std::make_unique<core::commands::AddNoteCommand> (selectedClip_->trackId, selectedClip_->clipId, note)))
            {
                selectedNoteIds_.push_back (note.id());
                changed = true;
            }
        }
        selectedNoteIds_.push_back (notes[1].id());

        activeArpeggioSourceNotes_.clear();
        frameNotesInViewport();
        repaint();
        return changed;
    }

    std::optional<core::time::TickPosition> localPlayheadInClip (const core::sequencing::MidiClip& clip) const
    {
        if (playheadTick_ < clip.startInProject() || playheadTick_ > clip.endInProject())
            return std::nullopt;

        return clip.projectToLocal (playheadTick_);
    }

    std::optional<juce::Rectangle<int>> playheadCursorBoundsForProjectTick (
        core::time::TickPosition projectTick,
        const core::sequencing::MidiClip& clip,
        const std::vector<PianoRollSegment>& segments,
        const std::vector<SegmentLayout>& layouts) const
    {
        if (projectTick < clip.startInProject() || projectTick > clip.endInProject())
            return std::nullopt;

        const auto localPlayhead = clip.projectToLocal (projectTick);
        const auto segmentIndex = segmentIndexForLocalTick (segments, localPlayhead);
        if (! segmentIndex.has_value())
            return std::nullopt;

        const auto layout = std::find_if (layouts.begin(), layouts.end(), [segmentIndex] (const auto& candidate) {
            return candidate.segmentIndex == *segmentIndex;
        });
        if (layout == layouts.end())
            return std::nullopt;

        const auto& segment = segments[*segmentIndex];
        const auto x = xForLocalTick (*layout, segment, localPlayhead);
        return juce::Rectangle<int> { x - 6, 0, 13, getHeight() };
    }

    void paintPasteCursor (juce::Graphics& graphics,
                           const core::sequencing::MidiClip& clip,
                           const std::vector<PianoRollSegment>& segments,
                           const std::vector<SegmentLayout>& layouts) const
    {
        const auto localPlayhead = localPlayheadInClip (clip);
        if (! localPlayhead.has_value())
            return;

        const auto segmentIndex = segmentIndexForLocalTick (segments, *localPlayhead);
        if (! segmentIndex.has_value())
            return;

        const auto layout = std::find_if (layouts.begin(), layouts.end(), [segmentIndex] (const auto& candidate) {
            return candidate.segmentIndex == *segmentIndex;
        });
        if (layout == layouts.end())
            return;

        const auto& segment = segments[*segmentIndex];
        const auto x = xForLocalTick (*layout, segment, *localPlayhead);
        graphics.setColour (playheadCursorColour.withAlpha (0.30f));
        graphics.fillRect (x - 1, rulerHeight, 3, std::max (0, getHeight() - rulerHeight));

        graphics.setColour (playheadCursorColour);
        graphics.drawVerticalLine (x, 0.0f, static_cast<float> (getHeight()));

        juce::Path marker;
        marker.startNewSubPath (static_cast<float> (x - 5), 2.0f);
        marker.lineTo (static_cast<float> (x + 5), 2.0f);
        marker.lineTo (static_cast<float> (x), 11.0f);
        marker.closeSubPath();
        graphics.fillPath (marker);
    }

    bool setPasteCursorFromPoint (juce::Point<float> point,
                                  const core::sequencing::MidiClip& clip,
                                  const std::vector<PianoRollSegment>& segments,
                                  const std::vector<SegmentLayout>& layouts)
    {
        const auto snappedLocalTick = xToSnappedTickInSegments (point.x, segments, layouts);
        if (! snappedLocalTick.has_value())
            return false;

        const auto maxLocalTicks = std::min (editableClipLength (clip).ticks(), clip.length().ticks());
        const auto localTick = core::time::TickPosition::fromTicks (clampTicks (snappedLocalTick->ticks(), 0, maxLocalTicks));
        const auto projectTick = clip.localToProject (localTick);

        playheadTick_ = projectTick;
        appServices_.setPlaybackPlayheadPosition (projectTick);
        return true;
    }

    void resizeSelectedNoteEnds (bool later)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        const auto gridTicks = snapTicks();
        const auto clipLength = editableClipLength (*clip);

        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip->findNoteById (noteId);
            if (note == nullptr)
                continue;

            const auto endTicks = note->endInClip().ticks();
            const auto newEndTicks = later
                ? ((endTicks / gridTicks) + 1) * gridTicks
                : ((std::max<std::int64_t> (0, endTicks - 1) / gridTicks) * gridTicks);
            const auto clampedEndTicks = clampTicks (newEndTicks,
                                                     note->startInClip().ticks() + gridTicks,
                                                     clipLength.ticks());
            const auto newDuration = core::time::TickDuration::fromTicks (clampedEndTicks - note->startInClip().ticks());
            if (newDuration != note->duration())
                runCommand (std::make_unique<core::commands::ResizeNoteCommand> (
                    selectedClip_->trackId,
                    selectedClip_->clipId,
                    noteId,
                    newDuration));
        }

        activeArpeggioSourceNotes_.clear();
        repaint();
    }

    bool selectionIsSingleNoteMelody() const
    {
        const auto notes = selectedNotes();
        if (notes.empty())
            return false;

        for (std::size_t lhs = 0; lhs < notes.size(); ++lhs)
            for (std::size_t rhs = lhs + 1; rhs < notes.size(); ++rhs)
                if (notes[lhs].startInClip() == notes[rhs].startInClip())
                    return false;

        return true;
    }

    void transposeSelectedNotesByOctaves (int octaveDelta)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip->findNoteById (noteId);
            if (note == nullptr)
                continue;

            const auto newPitchValue = std::clamp (note->pitch().value() + (octaveDelta * 12), 0, 127);
            if (newPitchValue == note->pitch().value())
                continue;

            runCommand (std::make_unique<core::commands::MoveNoteCommand> (
                selectedClip_->trackId,
                selectedClip_->clipId,
                noteId,
                note->startInClip(),
                core::music_theory::MidiPitch::fromValue (newPitchValue),
                note->spelling()));
        }

        activeArpeggioSourceNotes_.clear();
        frameNotesInViewport();
        repaint();
    }

    std::vector<DragNoteState> originalStatesForSelection (const core::sequencing::MidiClip& clip) const
    {
        std::vector<DragNoteState> result;
        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip.findNoteById (noteId);
            if (note != nullptr)
                result.push_back (DragNoteState { note->id(), note->startInClip(), note->duration(), note->pitch(), note->spelling() });
        }

        return result;
    }

    DragMode dragModeForHit (const NoteHit& hit, juce::Point<float> position) const
    {
        if (std::abs (position.x - static_cast<float> (hit.bounds.getX())) <= noteEdgeHandleWidth)
            return DragMode::resizeStart;

        if (std::abs (position.x - static_cast<float> (hit.bounds.getRight())) <= noteEdgeHandleWidth)
            return DragMode::resizeEnd;

        return DragMode::move;
    }

    std::optional<NoteHit> noteAt (juce::Point<float> point,
                                   const std::vector<PianoRollSegment>& segments,
                                   const std::vector<SegmentLayout>& layouts) const
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return std::nullopt;

        for (auto note = clip->notes().rbegin(); note != clip->notes().rend(); ++note)
        {
            for (const auto& bounds : boundsForNote (*note, segments, layouts))
                if (bounds.bounds.toFloat().contains (point))
                    return NoteHit { note->id(), bounds.bounds };
        }

        return std::nullopt;
    }

    std::vector<NoteRenderBounds> boundsForNote (const core::sequencing::MidiNote& note,
                                                 const std::vector<PianoRollSegment>& segments,
                                                 const std::vector<SegmentLayout>& layouts) const
    {
        return boundsForNoteState (note.startInClip(),
                                   note.duration(),
                                   note.pitch().value(),
                                   note.spelling(),
                                   segments,
                                   layouts,
                                   std::nullopt);
    }

    std::vector<NoteRenderBounds> boundsForGhost (const core::sequencing::MidiNote& note,
                                                  const core::sequencing::ReleaseGhostNote& ghost,
                                                  const std::vector<PianoRollSegment>& segments,
                                                  const std::vector<SegmentLayout>& layouts) const
    {
        return boundsForNoteState (ghost.ghostRegion.start(),
                                   ghost.ghostRegion.duration(),
                                   note.pitch().value(),
                                   note.spelling(),
                                   segments,
                                   layouts,
                                   std::nullopt);
    }

    std::vector<NoteRenderBounds> boundsForNote (const core::sequencing::MidiNote& note,
                                                 const std::vector<PianoRollSegment>& segments,
                                                 const std::vector<SegmentLayout>& layouts,
                                                 juce::Rectangle<int> paintClipBounds) const
    {
        return boundsForNoteState (note.startInClip(),
                                   note.duration(),
                                   note.pitch().value(),
                                   note.spelling(),
                                   segments,
                                   layouts,
                                   std::optional<juce::Rectangle<int>> { paintClipBounds });
    }

    template <typename Callback>
    void forEachBoundsForNote (const core::sequencing::MidiNote& note,
                               const std::vector<PianoRollSegment>& segments,
                               const std::vector<SegmentLayout>& layouts,
                               juce::Rectangle<int> paintClipBounds,
                               Callback&& callback) const
    {
        forEachNoteRenderBoundsState (note.startInClip(),
                                      note.duration(),
                                      note.pitch().value(),
                                      note.spelling(),
                                      segments,
                                      layouts,
                                      std::optional<juce::Rectangle<int>> { paintClipBounds },
                                      std::forward<Callback> (callback));
    }

    template <typename Callback>
    void forEachNoteRenderBoundsState (core::time::TickPosition start,
                                       core::time::TickDuration duration,
                                       int pitch,
                                       std::optional<core::music_theory::NoteName> spelling,
                                       const std::vector<PianoRollSegment>& segments,
                                       const std::vector<SegmentLayout>& layouts,
                                       std::optional<juce::Rectangle<int>> paintClipBounds,
                                       Callback&& callback) const
    {
        const auto end = start + duration;

        for (const auto& layout : layouts)
        {
            const auto& segment = segments[layout.segmentIndex];
            const auto partStartTicks = std::max (start.ticks(), segment.localStart.ticks());
            const auto partEndTicks = std::min (end.ticks(), segment.localEnd.ticks());
            if (partEndTicks <= partStartTicks)
                continue;

            const auto laneIndex = laneIndexForNoteState (pitch, spelling, segment.lanes);
            if (! laneIndex.has_value())
                continue;

            const auto y = laneToY (*laneIndex) + 2;
            const auto height = rowHeight_ - 4;
            if (paintClipBounds.has_value()
                && (y >= paintClipBounds->getBottom() || y + height <= paintClipBounds->getY()))
                continue;

            const auto partStart = core::time::TickPosition::fromTicks (partStartTicks);
            const auto partEnd = core::time::TickPosition::fromTicks (partEndTicks);
            const auto x = xForLocalTick (layout, segment, partStart);
            const auto width = std::max (5, xForLocalTick (layout, segment, partEnd) - x);
            if (paintClipBounds.has_value()
                && (x >= paintClipBounds->getRight() || x + width <= paintClipBounds->getX()))
                continue;

            callback (NoteRenderBounds {
                juce::Rectangle<int> {
                    x,
                    y,
                    width,
                    height
                },
                layout.segmentIndex
            });
        }
    }

    std::vector<NoteRenderBounds> boundsForNoteState (core::time::TickPosition start,
                                                      core::time::TickDuration duration,
                                                      int pitch,
                                                      std::optional<core::music_theory::NoteName> spelling,
                                                      const std::vector<PianoRollSegment>& segments,
                                                      const std::vector<SegmentLayout>& layouts,
                                                      std::optional<juce::Rectangle<int>> paintClipBounds = std::nullopt) const
    {
        std::vector<NoteRenderBounds> result;
        forEachNoteRenderBoundsState (start,
                                      duration,
                                      pitch,
                                      spelling,
                                      segments,
                                      layouts,
                                      paintClipBounds,
                                      [&result] (const auto& bounds)
                                      {
                                          result.push_back (bounds);
                                      });

        return result;
    }

    std::vector<NoteRenderBounds> previewBoundsFor (const core::sequencing::MidiNote& note,
                                                    const DragState& drag,
                                                    core::time::TickDuration clipLength,
                                                    const std::vector<PianoRollSegment>& segments,
                                                    const std::vector<SegmentLayout>& layouts) const
    {
        const auto original = std::find_if (drag.notes.begin(), drag.notes.end(), [&note] (const auto& state) {
            return state.noteId == note.id();
        });

        if (original == drag.notes.end())
            return boundsForNote (note, segments, layouts);

        switch (drag.mode)
        {
            case DragMode::move:
            {
                const auto newStart = moveStartFor (*original, drag, clipLength);
                const auto movedLane = moveLaneFor (*original, drag, segments, newStart);
                if (! movedLane.has_value())
                    return boundsForNote (note, segments, layouts);

                return boundsForNoteState (newStart,
                                           original->duration,
                                           movedLane->midiPitch,
                                           movedLane->spelling,
                                           segments,
                                           layouts);
            }

            case DragMode::resizeStart:
            {
                const auto resized = resizeStartFor (*original, drag);
                return boundsForNoteState (resized.start,
                                           resized.duration,
                                           original->pitch.value(),
                                           original->spelling,
                                           segments,
                                           layouts);
            }

            case DragMode::resizeEnd:
                return boundsForNoteState (original->start,
                                           resizeEndDurationFor (*original, drag, clipLength),
                                           original->pitch.value(),
                                           original->spelling,
                                           segments,
                                           layouts);
        }

        return boundsForNote (note, segments, layouts);
    }

    NoteVisualState visualStateFor (const core::sequencing::MidiNote& note,
                                    const DragState& drag,
                                    core::time::TickDuration clipLength,
                                    const std::vector<PianoRollSegment>& segments) const
    {
        const auto original = std::find_if (drag.notes.begin(), drag.notes.end(), [&note] (const auto& state) {
            return state.noteId == note.id();
        });

        if (original == drag.notes.end() || drag.mode != DragMode::move)
            return NoteVisualState { note.pitch().value(), note.spelling() };

        const auto newStart = moveStartFor (*original, drag, clipLength);
        const auto movedLane = moveLaneFor (*original, drag, segments, newStart);
        if (! movedLane.has_value())
            return NoteVisualState { note.pitch().value(), note.spelling() };

        return NoteVisualState { movedLane->midiPitch, movedLane->spelling };
    }

    void createNoteAt (juce::Point<float> point,
                       const core::sequencing::MidiClip& clip,
                       const std::vector<PianoRollSegment>& segments,
                       const std::vector<SegmentLayout>& layouts)
    {
        const auto clipLength = editableClipLength (clip);
        const auto gridTicks = snapTicks();
        if (clipLength.ticks() < gridTicks)
            return;

        const auto segmentIndex = segmentIndexForPoint (point, segments, layouts);
        if (! segmentIndex.has_value())
            return;

        const auto& segment = segments[*segmentIndex];
        const auto lane = laneAtY (point.y, segment.lanes);
        if (! lane.has_value())
            return;

        const auto duration = core::time::TickDuration::fromTicks (std::min<std::int64_t> (gridTicks, clipLength.ticks()));
        const auto latestStart = std::max<std::int64_t> (0, clipLength.ticks() - duration.ticks());
        const auto snappedTick = xToSnappedTickInSegments (point.x, segments, layouts);
        if (! snappedTick.has_value())
            return;

        const auto start = core::time::TickPosition::fromTicks (clampTicks (snappedTick->ticks(), 0, latestStart));
        const auto pitch = core::music_theory::MidiPitch::fromValue (lane->midiPitch);
        const auto noteId = nextNoteId (clip);

        auto command = std::make_unique<core::commands::AddNoteCommand> (
            selectedClip_->trackId,
            selectedClip_->clipId,
            core::sequencing::MidiNote { noteId, pitch, start, duration, defaultVelocity, lane->spelling });

        if (runCommand (std::move (command)))
        {
            selectedNoteIds_ = { noteId };
            activeArpeggioSourceNotes_.clear();
        }

        repaint();
    }

    void commitDrag (const DragState& drag)
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value())
            return;

        const auto clipLength = editableClipLength (*clip);
        const auto segments = visiblePitchSegments (*clip);

        for (const auto& note : drag.notes)
        {
            switch (drag.mode)
            {
                case DragMode::move:
                {
                    const auto newStart = moveStartFor (note, drag, clipLength);
                    const auto newLane = moveLaneFor (note, drag, segments, newStart);
                    if (! newLane.has_value())
                        break;

                    const auto newPitch = newLane->midiPitch;
                    if (newStart != note.start || newPitch != note.pitch.value())
                    {
                        runCommand (std::make_unique<core::commands::MoveNoteCommand> (
                            selectedClip_->trackId,
                            selectedClip_->clipId,
                            note.noteId,
                            newStart,
                            core::music_theory::MidiPitch::fromValue (newPitch),
                            newLane->spelling));
                    }
                    break;
                }

                case DragMode::resizeStart:
                {
                    const auto resized = resizeStartFor (note, drag);
                    if (resized.start != note.start || resized.duration != note.duration)
                    {
                        runCommand (std::make_unique<core::commands::ResizeNoteCommand> (
                            selectedClip_->trackId,
                            selectedClip_->clipId,
                            note.noteId,
                            resized.start,
                            resized.duration));
                    }
                    break;
                }

                case DragMode::resizeEnd:
                {
                    const auto duration = resizeEndDurationFor (note, drag, clipLength);
                    if (duration != note.duration)
                    {
                        runCommand (std::make_unique<core::commands::ResizeNoteCommand> (
                            selectedClip_->trackId,
                            selectedClip_->clipId,
                            note.noteId,
                            duration));
                    }
                    break;
                }
            }
        }
    }

    void deleteSelectedNotes()
    {
        if (! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        const auto noteIds = selectedNoteIds_;
        selectedNoteIds_.clear();
        activeArpeggioSourceNotes_.clear();
        for (const auto& noteId : noteIds)
        {
            runCommand (std::make_unique<core::commands::DeleteNoteCommand> (
                selectedClip_->trackId,
                selectedClip_->clipId,
                noteId));
        }

        repaint();
    }

    std::string nextClipId() const
    {
        auto index = std::size_t { 1 };
        while (true)
        {
            const auto candidate = "clip-" + std::to_string (index);
            auto exists = false;
            for (const auto& track : appServices_.project().tracks())
            {
                if (track.findClipById (candidate) != nullptr || track.findAudioClipById (candidate) != nullptr)
                {
                    exists = true;
                    break;
                }
            }

            if (! exists)
                return candidate;

            ++index;
        }
    }

    void separateSelectedNotesToClip()
    {
        if (! selectedClip_.has_value() || selectedNoteIds_.empty() || selectedMidiClip() == nullptr)
            return;

        const auto sourceTrackId = selectedClip_->trackId;
        const auto sourceClipId = selectedClip_->clipId;
        const auto separatedClipId = nextClipId();
        const auto separatedNoteIds = selectedNoteIds_;

        if (runCommand (std::make_unique<core::commands::SeparateNotesToClipCommand> (
                sourceTrackId,
                sourceClipId,
                separatedClipId,
                separatedNoteIds)))
        {
            selectedClip_ = ClipSelection { sourceTrackId, separatedClipId };
            selectedNoteIds_ = separatedNoteIds;
            activeArpeggioSourceNotes_.clear();
            frameNotesInViewport();
        }

        refreshAfterSelectionMutation();
    }

    void duplicateSelectedNotes()
    {
        const auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        struct DuplicateNote
        {
            core::music_theory::MidiPitch pitch { 60 };
            core::time::TickPosition start {};
            core::time::TickDuration duration {};
            int velocity = defaultVelocity;
            std::optional<core::music_theory::NoteName> spelling;
            std::optional<core::sequencing::NoteHarmonicInterpretation> harmonicInterpretation;
        };

        std::vector<DuplicateNote> duplicates;
        const auto clipLength = editableClipLength (*clip);

        for (const auto& noteId : selectedNoteIds_)
        {
            const auto* note = clip->findNoteById (noteId);
            if (note == nullptr)
                continue;

            const auto newStartTicks = note->startInClip().ticks() + snapTicks();
            if (newStartTicks + note->duration().ticks() > clipLength.ticks())
                continue;

            duplicates.push_back (DuplicateNote {
                note->pitch(),
                core::time::TickPosition::fromTicks (newStartTicks),
                note->duration(),
                note->velocity(),
                note->spelling(),
                note->harmonicInterpretation()
            });
        }

        selectedNoteIds_.clear();
        activeArpeggioSourceNotes_.clear();
        for (const auto& duplicate : duplicates)
        {
            auto* mutableClip = selectedMidiClip();
            if (mutableClip == nullptr)
                return;

            const auto noteId = nextNoteId (*mutableClip);
            auto command = std::make_unique<core::commands::AddNoteCommand> (
                selectedClip_->trackId,
                selectedClip_->clipId,
                core::sequencing::MidiNote {
                    noteId,
                    duplicate.pitch,
                    duplicate.start,
                    duplicate.duration,
                    duplicate.velocity,
                    duplicate.spelling,
                    duplicate.harmonicInterpretation
                });

            if (runCommand (std::move (command)))
                selectedNoteIds_.push_back (noteId);
        }

        repaint();
    }

    void stackDiatonicThird()
    {
        if (! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        auto command = std::make_unique<core::commands::StackDiatonicThirdCommand> (
            selectedClip_->trackId,
            selectedClip_->clipId,
            selectedNoteIds_);
        auto* commandPtr = command.get();

        if (runCommand (std::move (command)))
        {
            selectedNoteIds_ = commandPtr->resultingSelectionNoteIds();
            activeArpeggioSourceNotes_.clear();
        }

        refreshAfterSelectionMutation();
    }

    void removeHighestChordTone()
    {
        if (! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        auto command = std::make_unique<core::commands::RemoveHighestChordToneCommand> (
            selectedClip_->trackId,
            selectedClip_->clipId,
            selectedNoteIds_);
        auto* commandPtr = command.get();

        if (runCommand (std::move (command)))
        {
            selectedNoteIds_ = commandPtr->resultingSelectionNoteIds();
            activeArpeggioSourceNotes_.clear();
        }

        refreshAfterSelectionMutation();
    }

    void invertSelectedNotes (core::commands::InvertChordCommand::Direction direction)
    {
        if (! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        auto command = std::make_unique<core::commands::InvertChordCommand> (
            selectedClip_->trackId,
            selectedClip_->clipId,
            selectedNoteIds_,
            direction);

        runCommand (std::move (command));
        activeArpeggioSourceNotes_.clear();
        refreshAfterSelectionMutation();
    }

    void stepArpeggioSubdivision (bool shorter)
    {
        const auto& settings = appServices_.project().rhythmSettings();
        arpeggioSubdivisionId_ = shorter
            ? core::sequencing::shorterArpeggioSubdivisionId (currentArpeggioSubdivisionId(), settings)
            : core::sequencing::longerArpeggioSubdivisionId (currentArpeggioSubdivisionId(), settings);

        arpeggiateSelectedNotes();
    }

    void cycleArpeggioPattern (bool forward)
    {
        arpeggioPattern_ = forward
            ? core::sequencing::nextArpeggioPattern (arpeggioPattern_)
            : core::sequencing::previousArpeggioPattern (arpeggioPattern_);

        arpeggiateSelectedNotes();
    }

    void arpeggiateSelectedNotes()
    {
        auto* clip = selectedMidiClip();
        if (clip == nullptr || ! selectedClip_.has_value() || selectedNoteIds_.empty())
            return;

        if (activeArpeggioSourceNotes_.empty())
            activeArpeggioSourceNotes_ = selectedNotes();

        const auto subdivision = core::time::gridDivisionDefinitionFor (currentArpeggioSubdivisionId(), appServices_.project().rhythmSettings());
        const auto replacementNotes = arpeggiatedNotesFromSource (*clip, activeArpeggioSourceNotes_, subdivision.tickDuration, arpeggioPattern_);
        if (replacementNotes.empty())
            return;

        const auto previousSelection = selectedNoteIds_;
        selectedNoteIds_.clear();

        for (const auto& noteId : previousSelection)
            runCommand (std::make_unique<core::commands::DeleteNoteCommand> (selectedClip_->trackId, selectedClip_->clipId, noteId));

        std::vector<core::sequencing::MidiNote> addedNotes;
        for (const auto& note : replacementNotes)
        {
            if (runCommand (std::make_unique<core::commands::AddNoteCommand> (selectedClip_->trackId, selectedClip_->clipId, note)))
            {
                selectedNoteIds_.push_back (note.id());
                addedNotes.push_back (note);
            }
        }

        appServices_.logger().info ("Arpeggiated selection: "
                                    + core::sequencing::arpeggioPatternName (arpeggioPattern_)
                                    + " at "
                                    + currentArpeggioSubdivisionId());
        refreshAfterSelectionMutation();
    }

    struct ArpeggioSourceGroup
    {
        core::time::TickPosition start {};
        core::time::TickPosition end {};
        std::vector<core::sequencing::MidiNote> notes;
    };

    std::vector<ArpeggioSourceGroup> arpeggioSourceGroups (std::vector<core::sequencing::MidiNote> notes) const
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const auto& lhs, const auto& rhs) {
            if (lhs.startInClip() == rhs.startInClip())
                return lhs.pitch().value() < rhs.pitch().value();

            return lhs.startInClip() < rhs.startInClip();
        });

        std::vector<ArpeggioSourceGroup> groups;
        constexpr auto toleranceTicks = std::int64_t { 15 };
        for (const auto& note : notes)
        {
            if (! groups.empty() && note.startInClip().ticks() - groups.back().start.ticks() <= toleranceTicks)
            {
                groups.back().notes.push_back (note);
                if (note.endInClip() > groups.back().end)
                    groups.back().end = note.endInClip();
                continue;
            }

            groups.push_back (ArpeggioSourceGroup { note.startInClip(), note.endInClip(), { note } });
        }

        for (auto& group : groups)
            std::stable_sort (group.notes.begin(), group.notes.end(), [] (const auto& lhs, const auto& rhs) {
                return lhs.pitch().value() < rhs.pitch().value();
            });

        return groups;
    }

    std::vector<std::size_t> arpeggioOrderFor (std::size_t noteCount, core::sequencing::ArpeggioPattern pattern) const
    {
        std::vector<std::size_t> order;
        if (noteCount == 0)
            return order;

        switch (pattern)
        {
            case core::sequencing::ArpeggioPattern::up:
                for (auto index = std::size_t {}; index < noteCount; ++index)
                    order.push_back (index);
                break;

            case core::sequencing::ArpeggioPattern::down:
                for (auto index = noteCount; index > 0; --index)
                    order.push_back (index - 1);
                break;

            case core::sequencing::ArpeggioPattern::upDown:
                for (auto index = std::size_t {}; index < noteCount; ++index)
                    order.push_back (index);
                if (noteCount > 2)
                    for (auto index = noteCount - 1; index > 1; --index)
                        order.push_back (index - 1);
                break;

            case core::sequencing::ArpeggioPattern::downUp:
                for (auto index = noteCount; index > 0; --index)
                    order.push_back (index - 1);
                if (noteCount > 2)
                    for (auto index = std::size_t { 1 }; index + 1 < noteCount; ++index)
                        order.push_back (index);
                break;

            case core::sequencing::ArpeggioPattern::outsideIn:
            {
                auto left = std::size_t {};
                auto right = noteCount - 1;
                while (left <= right)
                {
                    order.push_back (left++);
                    if (right + 1 == 0 || right < left)
                        break;
                    order.push_back (right--);
                }
                break;
            }

            case core::sequencing::ArpeggioPattern::insideOut:
            {
                const auto leftCentre = (noteCount - 1) / 2;
                auto left = static_cast<int> (leftCentre);
                auto right = left + 1;
                while (left >= 0 || right < static_cast<int> (noteCount))
                {
                    if (left >= 0)
                        order.push_back (static_cast<std::size_t> (left--));
                    if (right < static_cast<int> (noteCount))
                        order.push_back (static_cast<std::size_t> (right++));
                }
                break;
            }
        }

        return order;
    }

    std::vector<core::sequencing::MidiNote> arpeggiatedNotesFromSource (const core::sequencing::MidiClip& clip,
                                                                        const std::vector<core::sequencing::MidiNote>& sourceNotes,
                                                                        core::time::TickDuration subdivision,
                                                                        core::sequencing::ArpeggioPattern pattern) const
    {
        std::vector<core::sequencing::MidiNote> result;
        const auto subdivisionTicks = std::max<std::int64_t> (1, subdivision.ticks());

        for (const auto& group : arpeggioSourceGroups (sourceNotes))
        {
            if (group.notes.size() < 2)
                continue;

            const auto order = arpeggioOrderFor (group.notes.size(), pattern);
            auto step = std::size_t {};
            for (auto tick = group.start.ticks(); tick < group.end.ticks(); tick += subdivisionTicks)
            {
                const auto& source = group.notes[order[step % order.size()]];
                const auto noteId = step < group.notes.size() ? group.notes[step].id() : nextNoteId (clip, result);
                const auto durationTicks = std::min<std::int64_t> (subdivisionTicks, group.end.ticks() - tick);
                result.push_back (core::sequencing::MidiNote {
                    noteId,
                    source.pitch(),
                    core::time::TickPosition::fromTicks (tick),
                    core::time::TickDuration::fromTicks (durationTicks),
                    source.velocity(),
                    source.spelling(),
                    source.harmonicInterpretation()
                });
                ++step;
            }
        }

        return result;
    }

    std::string currentArpeggioSubdivisionId()
    {
        const auto& settings = appServices_.project().rhythmSettings();
        const auto subdivisions = core::sequencing::availableArpeggioSubdivisions (settings);
        const auto match = std::find_if (subdivisions.begin(), subdivisions.end(), [this] (const auto& subdivision) {
            return subdivision.id == arpeggioSubdivisionId_;
        });

        if (match != subdivisions.end())
            return arpeggioSubdivisionId_;

        const auto currentGrid = core::time::gridDivisionDefinitionFor (settings.currentGridDivisionId(), settings);
        const auto currentGridMatch = std::find_if (subdivisions.begin(), subdivisions.end(), [&currentGrid] (const auto& subdivision) {
            return subdivision.id == currentGrid.id;
        });

        arpeggioSubdivisionId_ = currentGridMatch == subdivisions.end() && ! subdivisions.empty()
            ? subdivisions.front().id
            : currentGrid.id;
        return arpeggioSubdivisionId_;
    }

    void refreshAfterSelectionMutation()
    {
        repaint();
    }

    bool runCommand (std::unique_ptr<core::commands::Command> command)
    {
        const auto commandName = command == nullptr ? std::string { "unknown command" } : command->name();
        appServices_.tracePluginState ("piano-roll command begin: " + commandName);
        appServices_.observeLivePluginParameterState();
        const auto result = appServices_.commandStack().execute (std::move (command));
        if (result.failed())
        {
            appServices_.reportWarning ("Piano roll edit failed: " + result.error());
            appServices_.tracePluginState ("piano-roll command failed: " + commandName);
            return false;
        }

        resizedParentContent();
        frameNotesInViewport();
        appServices_.restoreObservedPluginParameterStateSoon();
        appServices_.tracePluginState ("piano-roll command end: " + commandName);
        return true;
    }

    struct ResizeStartResult
    {
        core::time::TickPosition start {};
        core::time::TickDuration duration {};
    };

    ResizeStartResult resizeStartFor (const DragNoteState& note, const DragState& drag) const
    {
        const auto endTicks = note.start.ticks() + note.duration.ticks();
        const auto gridTicks = snapTicks();
        const auto startTicks = clampTicks (note.start.ticks() + snappedTickDelta (drag.currentX - drag.mouseStartX),
                                           0,
                                           endTicks - gridTicks);
        return ResizeStartResult {
            core::time::TickPosition::fromTicks (startTicks),
            core::time::TickDuration::fromTicks (endTicks - startTicks)
        };
    }

    core::time::TickDuration resizeEndDurationFor (const DragNoteState& note,
                                                   const DragState& drag,
                                                   core::time::TickDuration clipLength) const
    {
        const auto durationTicks = clampTicks (note.duration.ticks() + snappedTickDelta (drag.currentX - drag.mouseStartX),
                                              snapTicks(),
                                              clipLength.ticks() - note.start.ticks());
        return core::time::TickDuration::fromTicks (durationTicks);
    }

    core::time::TickPosition moveStartFor (const DragNoteState& note,
                                           const DragState& drag,
                                           core::time::TickDuration clipLength) const
    {
        const auto startTicks = clampTicks (note.start.ticks() + snappedTickDelta (drag.currentX - drag.mouseStartX),
                                           0,
                                           clipLength.ticks() - note.duration.ticks());
        return core::time::TickPosition::fromTicks (startTicks);
    }

    std::optional<PianoLane> moveLaneFor (const DragNoteState& note,
                                          const DragState& drag,
                                          const std::vector<PianoRollSegment>& segments,
                                          core::time::TickPosition targetStart) const
    {
        const auto originalSegmentIndex = segmentIndexForLocalTick (segments, note.start);
        const auto targetSegmentIndex = segmentIndexForLocalTick (segments, targetStart);
        if (! originalSegmentIndex.has_value() || ! targetSegmentIndex.has_value())
            return std::nullopt;

        const auto& originalLanes = segments[*originalSegmentIndex].lanes;
        const auto& targetLanes = segments[*targetSegmentIndex].lanes;
        const auto originalLaneIndex = laneIndexForNoteState (note.pitch.value(), note.spelling, originalLanes);
        if (! originalLaneIndex.has_value() || targetLanes.empty())
            return std::nullopt;

        const auto rowDelta = static_cast<int> (std::llround (static_cast<double> (drag.currentY - drag.mouseStartY) / rowHeight_));
        const auto targetLaneIndex = std::clamp (*originalLaneIndex + rowDelta, 0, static_cast<int> (targetLanes.size()) - 1);
        return targetLanes[static_cast<std::size_t> (targetLaneIndex)];
    }

    std::vector<PianoLane> visiblePitchLanesForSegment (const core::sequencing::MidiClip& clip,
                                                        core::time::TickPosition localStart,
                                                        core::time::TickPosition localEnd,
                                                        const core::sequencing::HarmonicContext& context) const
    {
        core::diagnostics::ScopedPerformanceTimer timer { "PianoRollContent::visiblePitchLanesForSegment" };

        struct PitchClassLane
        {
            core::music_theory::PitchClass pitchClass;
            core::music_theory::NoteName spelling;
            bool nativeScale = true;
            bool usedAccidental = false;
            bool greyed = false;
        };

        std::vector<PianoLane> lanes;
        std::vector<PitchClassLane> pitchClassLanes;

        const auto scaleInstance = context.scaleInstance (scaleLibraryForProject());
        const auto pitchClasses = scaleInstance.pitchClasses();
        const auto spellings = scaleInstance.visibleNoteSpellings();
        pitchClassLanes.reserve (chromaticReveal_ ? 12 : pitchClasses.size());

        for (std::size_t index = 0; index < pitchClasses.size(); ++index)
        {
            pitchClassLanes.push_back (PitchClassLane {
                pitchClasses[index],
                spellings[index],
                true,
                false,
                false
            });
        }

        const auto findNativeLane = [&pitchClassLanes] (core::music_theory::PitchClass pitchClass) -> const PitchClassLane*
        {
            const auto match = std::find_if (pitchClassLanes.begin(), pitchClassLanes.end(), [pitchClass] (const auto& lane) {
                return lane.pitchClass == pitchClass && lane.nativeScale;
            });

            return match == pitchClassLanes.end() ? nullptr : &*match;
        };

        const auto accidentalSpellingFor = [&clip, localStart, localEnd, &findNativeLane] (core::music_theory::PitchClass pitchClass) -> std::optional<core::music_theory::NoteName>
        {
            if (findNativeLane (pitchClass) != nullptr)
                return std::nullopt;

            for (const auto& note : clip.notes())
            {
                if (note.startInClip() >= localEnd || note.endInClip() <= localStart)
                    continue;

                if (note.pitch().pitchClass() != pitchClass)
                    continue;

                if (note.spelling().has_value() && note.spelling()->pitchClass() == pitchClass)
                    return note.spelling();

                return core::music_theory::spellPitchClass (pitchClass);
            }

            return std::nullopt;
        };

        for (auto semitone = 0; semitone < 12; ++semitone)
        {
            const auto pitchClass = core::music_theory::PitchClass::fromSemitonesFromC (semitone);
            if (findNativeLane (pitchClass) != nullptr)
                continue;

            const auto accidentalSpelling = accidentalSpellingFor (pitchClass);
            if (accidentalSpelling.has_value())
            {
                pitchClassLanes.push_back (PitchClassLane {
                    pitchClass,
                    *accidentalSpelling,
                    false,
                    true,
                    false
                });
                continue;
            }

            if (chromaticReveal_)
            {
                pitchClassLanes.push_back (PitchClassLane {
                    pitchClass,
                    core::music_theory::spellPitchClass (pitchClass),
                    false,
                    false,
                    true
                });
            }
        }

        for (auto midi = 127; midi >= 0; --midi)
        {
            const auto midiPitch = core::music_theory::MidiPitch::fromValue (midi);

            for (const auto& pitchClassLane : pitchClassLanes)
            {
                if (midiPitch.pitchClass() == pitchClassLane.pitchClass)
                {
                    lanes.push_back (PianoLane {
                        midi,
                        pitchClassLane.spelling,
                        pitchClassLane.nativeScale,
                        pitchClassLane.usedAccidental,
                        pitchClassLane.greyed
                    });
                    break;
                }
            }
        }

        return lanes;
    }

    std::vector<PianoRollSegment> visiblePitchSegments (const core::sequencing::MidiClip& clip) const
    {
        core::diagnostics::ScopedPerformanceTimer timer { "PianoRollContent::visiblePitchSegments" };

        const auto clipLength = editableClipLength (clip);
        const auto clipStart = clip.startInProject();
        const auto clipEnd = clipStart + clipLength;
        const auto& structure = appServices_.project().musicalStructure();
        const core::sequencing::HarmonicContextResolver resolver { structure };

        std::vector<std::int64_t> candidateBoundaries;
        std::vector<std::int64_t> regionStartBoundaries;

        const auto addBoundary = [&clipStart, &clipEnd, &candidateBoundaries] (core::time::TickPosition projectPosition)
        {
            if (projectPosition <= clipStart || projectPosition >= clipEnd)
                return;

            candidateBoundaries.push_back ((projectPosition - clipStart).ticks());
        };

        const auto addRegionStart = [&clipStart, &clipEnd, &regionStartBoundaries] (core::time::TickPosition projectPosition)
        {
            if (projectPosition <= clipStart || projectPosition >= clipEnd)
                return;

            regionStartBoundaries.push_back ((projectPosition - clipStart).ticks());
        };

        for (const auto& region : structure.keyCenterRegions())
        {
            addBoundary (region.start());
            addBoundary (region.end());
            addRegionStart (region.start());
        }

        for (const auto& region : structure.scaleModeRegions())
        {
            addBoundary (region.start());
            addBoundary (region.end());
            addRegionStart (region.start());
        }

        std::sort (candidateBoundaries.begin(), candidateBoundaries.end());
        candidateBoundaries.erase (std::unique (candidateBoundaries.begin(), candidateBoundaries.end()), candidateBoundaries.end());
        std::sort (regionStartBoundaries.begin(), regionStartBoundaries.end());
        regionStartBoundaries.erase (std::unique (regionStartBoundaries.begin(), regionStartBoundaries.end()), regionStartBoundaries.end());

        std::vector<std::int64_t> boundaries { 0 };
        for (const auto localTicks : candidateBoundaries)
        {
            const auto projectPosition = clipStart + core::time::TickDuration::fromTicks (localTicks);
            const auto previousProjectPosition = clipStart + core::time::TickDuration::fromTicks (std::max<std::int64_t> (0, localTicks - 1));
            const auto isRegionStart = std::binary_search (regionStartBoundaries.begin(), regionStartBoundaries.end(), localTicks);
            const auto contextChanges = resolver.resolveAt (previousProjectPosition) != resolver.resolveAt (projectPosition);
            if (isRegionStart || contextChanges)
                boundaries.push_back (localTicks);
        }

        boundaries.push_back (clipLength.ticks());
        std::sort (boundaries.begin(), boundaries.end());
        boundaries.erase (std::unique (boundaries.begin(), boundaries.end()), boundaries.end());

        std::vector<PianoRollSegment> segments;
        segments.reserve (boundaries.size() > 1 ? boundaries.size() - 1 : 1);
        for (std::size_t index = 0; index + 1 < boundaries.size(); ++index)
        {
            const auto localStart = core::time::TickPosition::fromTicks (boundaries[index]);
            const auto localEnd = core::time::TickPosition::fromTicks (boundaries[index + 1]);
            const auto context = resolver.resolveAt (clipStart + (localStart - core::time::TickPosition {}));
            segments.push_back (PianoRollSegment {
                localStart,
                localEnd,
                context,
                visiblePitchLanesForSegment (clip, localStart, localEnd, context),
                index > 0
            });
        }

        if (segments.empty())
        {
            const auto context = resolver.resolveAt (clipStart);
            segments.push_back (PianoRollSegment {
                core::time::TickPosition {},
                core::time::TickPosition {} + clipLength,
                context,
                visiblePitchLanesForSegment (clip, core::time::TickPosition {}, core::time::TickPosition {} + clipLength, context),
                false
            });
        }

        return segments;
    }

    core::sequencing::HarmonicContext harmonicContextForFocus (const core::sequencing::MidiClip& clip) const
    {
        const core::sequencing::HarmonicContextResolver resolver { appServices_.project().musicalStructure() };
        return resolver.resolveAt (focusProjectTick (clip));
    }

    core::time::TickPosition focusProjectTick (const core::sequencing::MidiClip& clip) const
    {
        if (! selectedNoteIds_.empty())
        {
            std::optional<core::time::TickPosition> earliestSelectedNoteStart;
            for (const auto& noteId : selectedNoteIds_)
            {
                const auto* note = clip.findNoteById (noteId);
                if (note == nullptr)
                    continue;

                if (! earliestSelectedNoteStart.has_value() || note->startInClip() < *earliestSelectedNoteStart)
                    earliestSelectedNoteStart = note->startInClip();
            }

            if (earliestSelectedNoteStart.has_value())
                return clip.startInProject() + (*earliestSelectedNoteStart - core::time::TickPosition {});
        }

        if (playheadTick_ >= clip.startInProject() && playheadTick_ < clip.endInProject())
            return playheadTick_;

        return clip.startInProject();
    }

    core::music_theory::ScaleLibrary scaleLibraryForProject() const
    {
        auto library = core::music_theory::ScaleLibrary::createBuiltInLibrary();
        for (const auto& customScale : appServices_.project().customScales())
        {
            try
            {
                library.addDefinition (customScale);
            }
            catch (const std::exception&)
            {
            }
        }

        return library;
    }

    void paintHarmonicOverlay (juce::Graphics& graphics,
                               const core::sequencing::MidiClip& clip,
                               const std::vector<PianoRollSegment>& segments,
                               const std::vector<SegmentLayout>& layouts,
                               core::time::TickDuration clipLength) const
    {
        const auto paintClipBounds = visiblePaintBounds (graphics);
        const auto projectStart = clip.startInProject();
        const auto projectEnd = projectStart + clipLength;
        const auto scaleLibrary = scaleLibraryForProject();
        const auto& structure = appServices_.project().musicalStructure();

        for (const auto& chordRegion : structure.chordRegions())
        {
            const auto startTicks = std::max (projectStart.ticks(), chordRegion.start().ticks());
            const auto endTicks = std::min (projectEnd.ticks(), chordRegion.end().ticks());
            if (endTicks <= startTicks)
                continue;

            graphics.setColour (textColour.withAlpha (0.86f));

            for (const auto& layout : layouts)
            {
                const auto& segment = segments[layout.segmentIndex];
                const auto segmentProjectStart = projectStart + (segment.localStart - core::time::TickPosition {});
                const auto segmentProjectEnd = projectStart + (segment.localEnd - core::time::TickPosition {});
                const auto segmentStartTicks = std::max (startTicks, segmentProjectStart.ticks());
                const auto segmentEndTicks = std::min (endTicks, segmentProjectEnd.ticks());
                if (segmentEndTicks <= segmentStartTicks)
                    continue;

                const auto localStart = core::time::TickPosition::fromTicks (segmentStartTicks - projectStart.ticks());
                const auto localEnd = core::time::TickPosition::fromTicks (segmentEndTicks - projectStart.ticks());
                const auto x = xForLocalTick (layout, segment, localStart);
                const auto width = std::max (1, xForLocalTick (layout, segment, localEnd) - x);
                if (! juce::Rectangle<int> { x, 0, width, getHeight() }.intersects (paintClipBounds))
                    continue;

                graphics.setColour (rootOverlayColour.withAlpha (0.14f));
                graphics.fillRect (x, 0, width, rulerHeight);
                graphics.setColour (textColour.withAlpha (0.86f));
                graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
                graphics.drawText (chordRegion.chordName(),
                                   x + 5,
                                   0,
                                   std::max (0, width - 10),
                                   rulerHeight,
                                   juce::Justification::centredLeft);

                for (std::size_t laneIndex = 0; laneIndex < segment.lanes.size(); ++laneIndex)
                {
                    const auto& lane = segment.lanes[laneIndex];
                    const auto y = laneToY (static_cast<int> (laneIndex));
                    if (y >= paintClipBounds.getBottom() || y + rowHeight_ <= paintClipBounds.getY())
                        continue;

                    const auto pitchClass = core::music_theory::MidiPitch::fromValue (lane.midiPitch).pitchClass();
                    const auto role = core::sequencing::harmonicOverlayRoleAt (
                        structure,
                        scaleLibrary,
                        core::time::TickPosition::fromTicks (segmentStartTicks),
                        pitchClass);

                    const auto colour = overlayColourForRole (role);
                    if (! colour.has_value())
                        continue;

                    graphics.setColour (*colour);
                    graphics.fillRect (x, y, width, rowHeight_);
                }
            }
        }
    }

    const core::sequencing::ExpressionLane* selectedExpressionLane (const core::sequencing::MidiClip& clip) const
    {
        if (! selectedExpressionLaneId_.has_value())
            return nullptr;

        return clip.expressionState().findLane (*selectedExpressionLaneId_);
    }

    juce::Rectangle<int> expressionValueArea (const std::vector<SegmentLayout>& layouts, juce::Rectangle<int> paintClipBounds) const
    {
        if (layouts.empty())
            return {};

        const auto left = std::max (paintClipBounds.getX(), layouts.front().gridStartX);
        const auto right = std::min (paintClipBounds.getRight(), layouts.back().gridEndX);
        const auto top = std::max (paintClipBounds.getY(), rulerHeight + expressionStripHeight);
        const auto bottom = paintClipBounds.getBottom();
        if (right <= left || bottom <= top)
            return {};

        return juce::Rectangle<int>::leftTopRightBottom (left, top, right, bottom);
    }

    float expressionValueToY (double value,
                              core::sequencing::ExpressionLanePolarity polarity,
                              juce::Rectangle<int> area) const noexcept
    {
        const auto minimum = core::sequencing::expressionLaneMinimumValue (polarity);
        const auto maximum = core::sequencing::expressionLaneMaximumValue (polarity);
        const auto normalized = (std::clamp (value, minimum, maximum) - minimum) / std::max (0.000001, maximum - minimum);
        return static_cast<float> (area.getBottom()) - (static_cast<float> (area.getHeight()) * static_cast<float> (normalized));
    }

    std::optional<int> xForExpressionLocalTick (const std::vector<PianoRollSegment>& segments,
                                                const std::vector<SegmentLayout>& layouts,
                                                core::time::TickPosition localPosition) const
    {
        const auto segmentIndex = segmentIndexForLocalTick (segments, localPosition);
        if (! segmentIndex.has_value())
            return std::nullopt;

        const auto layout = std::find_if (layouts.begin(), layouts.end(), [segmentIndex] (const auto& candidate) {
            return candidate.segmentIndex == *segmentIndex;
        });
        if (layout == layouts.end())
            return std::nullopt;

        return xForLocalTick (*layout, segments[*segmentIndex], localPosition);
    }

    void addExpressionClipStrip (std::vector<ExpressionStrip>& strips,
                                 const std::vector<PianoRollSegment>& segments,
                                 const std::vector<SegmentLayout>& layouts,
                                 core::sequencing::Region region,
                                 juce::Rectangle<int> paintClipBounds,
                                 juce::Colour colour,
                                 int y,
                                 std::optional<core::sequencing::ExpressionClipId> phraseEnvelopeId = std::nullopt,
                                 std::optional<core::sequencing::ExpressionClipId> cyclicClipId = std::nullopt,
                                 std::optional<core::time::TickPosition> partition = std::nullopt,
                                 std::optional<core::time::TickPosition> secondPartition = std::nullopt) const
    {
        for (const auto& layout : layouts)
        {
            const auto& segment = segments[layout.segmentIndex];
            const auto startTicks = std::max (region.start().ticks(), segment.localStart.ticks());
            const auto endTicks = std::min (region.end().ticks(), segment.localEnd.ticks());
            if (endTicks <= startTicks)
                continue;

            const auto start = core::time::TickPosition::fromTicks (startTicks);
            const auto end = core::time::TickPosition::fromTicks (endTicks);
            const auto x = xForLocalTick (layout, segment, start);
            const auto width = std::max (2, xForLocalTick (layout, segment, end) - x);
            auto bounds = juce::Rectangle<int> { x, y, width, 8 };
            if (! bounds.intersects (paintClipBounds))
                continue;

            auto strip = ExpressionStrip { bounds, colour, std::nullopt, std::nullopt };
            strip.phraseEnvelopeId = phraseEnvelopeId;
            strip.cyclicClipId = cyclicClipId;
            if (partition.has_value())
            {
                if (const auto partitionX = xForExpressionLocalTick (segments, layouts, *partition);
                    partitionX.has_value() && *partitionX > bounds.getX() && *partitionX < bounds.getRight())
                {
                    strip.partitionX = *partitionX;
                }
            }
            if (secondPartition.has_value())
            {
                if (const auto partitionX = xForExpressionLocalTick (segments, layouts, *secondPartition);
                    partitionX.has_value() && *partitionX > bounds.getX() && *partitionX < bounds.getRight())
                {
                    strip.secondPartitionX = *partitionX;
                }
            }

            strips.push_back (strip);
        }
    }

    void addCyclicWaveformPath (std::vector<ExpressionOverlayCache::CyclicWaveform>& waveforms,
                                const std::vector<PianoRollSegment>& segments,
                                const std::vector<SegmentLayout>& layouts,
                                const core::sequencing::ExpressionLane& lane,
                                const core::sequencing::CyclicExpressionClip& clip,
                                juce::Rectangle<int> paintClipBounds,
                                juce::Rectangle<int> area,
                                const core::sequencing::ExpressionEvaluationContext& context,
                                std::int64_t stepTicks) const
    {
        if (area.isEmpty() || stepTicks <= 0)
            return;

        juce::Path path;
        auto started = false;
        for (const auto& layout : layouts)
        {
            if (layout.segmentIndex >= segments.size())
                continue;

            if (paintClipBounds.getRight() <= layout.gridStartX || paintClipBounds.getX() >= layout.gridEndX)
                continue;

            const auto& segment = segments[layout.segmentIndex];
            const auto visibleStartX = std::clamp (paintClipBounds.getX(), layout.gridStartX, layout.gridEndX);
            const auto visibleEndX = std::clamp (paintClipBounds.getRight(), layout.gridStartX, layout.gridEndX);
            if (visibleEndX <= visibleStartX)
                continue;

            const auto visibleStartTicks = segment.localStart.ticks()
                + static_cast<std::int64_t> (std::floor ((static_cast<double> (visibleStartX - layout.gridStartX) / std::max (1, pixelsPerQuarter_)) * ticksPerBeat));
            const auto visibleEndTicks = segment.localStart.ticks()
                + static_cast<std::int64_t> (std::ceil ((static_cast<double> (visibleEndX - layout.gridStartX) / std::max (1, pixelsPerQuarter_)) * ticksPerBeat));
            const auto startTicks = std::max ({ clip.phraseRegion().start().ticks(), segment.localStart.ticks(), visibleStartTicks });
            const auto endTicks = std::min ({ clip.phraseRegion().end().ticks(), segment.localEnd.ticks(), visibleEndTicks });
            if (endTicks <= startTicks)
                continue;

            started = false;
            for (auto tick = startTicks; tick <= endTicks; tick += stepTicks)
            {
                const auto position = core::time::TickPosition::fromTicks (std::min (tick, endTicks));
                const auto value = core::sequencing::evaluateCyclicExpressionClipAt (clip, position, context);
                if (! value.has_value())
                    continue;

                const auto x = xForLocalTick (layout, segment, position);
                const auto y = expressionValueToY (*value, lane.polarity(), area);
                if (! started)
                {
                    path.startNewSubPath (static_cast<float> (x), y);
                    started = true;
                }
                else
                {
                    path.lineTo (static_cast<float> (x), y);
                }

                if (tick == endTicks)
                    break;
            }
        }

        if (! path.isEmpty())
            waveforms.push_back ({ clip.id(), std::move (path) });
    }

    std::size_t expressionLaneFingerprint (const core::sequencing::ExpressionLane& lane,
                                           const std::vector<SegmentLayout>& layouts,
                                           juce::Rectangle<int> paintClipBounds,
                                           juce::Rectangle<int> area) const
    {
        auto fingerprint = std::size_t {};
        hashCombine (fingerprint, std::hash<std::string> {} (lane.id().value));
        hashCombine (fingerprint, std::hash<std::string> {} (lane.name()));
        hashCombine (fingerprint, static_cast<std::size_t> (lane.polarity()));
        hashCombine (fingerprint, lane.enabled() ? 1u : 0u);
        hashCombine (fingerprint, static_cast<std::size_t> (pixelsPerQuarter_));
        hashCombine (fingerprint, static_cast<std::size_t> (rowHeight_));
        hashCombine (fingerprint, static_cast<std::size_t> (paintClipBounds.getX() / 96));
        hashCombine (fingerprint, static_cast<std::size_t> (paintClipBounds.getRight() / 96));
        hashCombine (fingerprint, static_cast<std::size_t> (area.getY()));
        hashCombine (fingerprint, static_cast<std::size_t> (area.getHeight()));
        hashCombine (fingerprint, layouts.size());

        for (const auto& layout : layouts)
        {
            hashCombine (fingerprint, layout.segmentIndex);
            hashCombine (fingerprint, static_cast<std::size_t> (layout.gridStartX));
            hashCombine (fingerprint, static_cast<std::size_t> (layout.gridEndX));
        }

        for (const auto& clip : lane.phraseEnvelopeClips())
        {
            hashCombine (fingerprint, std::hash<std::string> {} (clip.id().value));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.phraseRegion().start().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.phraseRegion().end().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (std::llround (clip.storedLevel() * 1000000.0)));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.attackStage().duration.ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (std::llround (clip.attackStage().startLevel * 1000000.0)));
            hashCombine (fingerprint, static_cast<std::size_t> (std::llround (clip.attackStage().endLevel * 1000000.0)));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.attackStage().curveShape));
            hashCombine (fingerprint, clip.decayStage().has_value() ? static_cast<std::size_t> (clip.decayStage()->duration.ticks()) : 0u);
            hashCombine (fingerprint, clip.decayStage().has_value() ? static_cast<std::size_t> (std::llround (clip.decayStage()->startLevel * 1000000.0)) : 0u);
            hashCombine (fingerprint, clip.decayStage().has_value() ? static_cast<std::size_t> (std::llround (clip.decayStage()->endLevel * 1000000.0)) : 0u);
            hashCombine (fingerprint, clip.decayStage().has_value() ? static_cast<std::size_t> (clip.decayStage()->curveShape) : 0u);
            hashCombine (fingerprint, clip.releaseStage().has_value() ? static_cast<std::size_t> (clip.releaseStage()->duration.ticks()) : 0u);
            hashCombine (fingerprint, clip.releaseStage().has_value() ? static_cast<std::size_t> (std::llround (clip.releaseStage()->startLevel * 1000000.0)) : 0u);
            hashCombine (fingerprint, clip.releaseStage().has_value() ? static_cast<std::size_t> (std::llround (clip.releaseStage()->endLevel * 1000000.0)) : 0u);
            hashCombine (fingerprint, clip.releaseStage().has_value() ? static_cast<std::size_t> (clip.releaseStage()->curveShape) : 0u);
            hashCombine (fingerprint, clip.peakLevel().has_value() ? static_cast<std::size_t> (std::llround (*clip.peakLevel() * 1000000.0)) : 0u);
            hashCombine (fingerprint, clip.sustainLevel().has_value() ? static_cast<std::size_t> (std::llround (*clip.sustainLevel() * 1000000.0)) : 0u);
        }

        for (const auto& clip : lane.cyclicClips())
        {
            hashCombine (fingerprint, std::hash<std::string> {} (clip.id().value));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.phraseRegion().start().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.phraseRegion().end().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.waveShape()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.blendMode()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.wavePolarityMode()));
            hashCombine (fingerprint, std::hash<std::string> {} (clip.frequencyDivisionId()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.attackTime().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (clip.releaseTime().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (std::llround (clip.maxAmplitude() * 1000000.0)));
            hashCombine (fingerprint, static_cast<std::size_t> (std::llround (clip.phase() * 1000000.0)));
        }

        return fingerprint;
    }

    const ExpressionOverlayCache& cachedExpressionOverlay (const core::sequencing::ExpressionLane& lane,
                                                           const std::vector<PianoRollSegment>& segments,
                                                           const std::vector<SegmentLayout>& layouts,
                                                           juce::Rectangle<int> paintClipBounds) const
    {
        const auto area = expressionValueArea (layouts, paintClipBounds);
        const auto fingerprint = expressionLaneFingerprint (lane, layouts, paintClipBounds, area);
        if (expressionOverlayCache_.fingerprint == fingerprint)
            return expressionOverlayCache_;

        expressionOverlayCache_ = {};
        expressionOverlayCache_.fingerprint = fingerprint;
        expressionOverlayCache_.valueArea = area;
        expressionOverlayCache_.polarity = lane.polarity();
        if (area.isEmpty() || layouts.empty())
            return expressionOverlayCache_;

        core::sequencing::ExpressionEvaluationContext context { appServices_.project().rhythmSettings(), true };
        const auto ticksPerPixel = static_cast<double> (ticksPerBeat) / static_cast<double> (std::max (1, pixelsPerQuarter_));
        const auto stepTicks = std::max<std::int64_t> (24, static_cast<std::int64_t> (std::ceil (ticksPerPixel * 6.0)));
        const auto waveformStepTicks = std::max<std::int64_t> (6, static_cast<std::int64_t> (std::ceil (ticksPerPixel * 2.0)));
        const auto sampleStep = core::time::TickDuration::fromTicks (stepTicks);

        auto pathStarted = false;
        std::vector<core::sequencing::ExpressionSample> samples;
        for (const auto& layout : layouts)
        {
            if (layout.segmentIndex >= segments.size())
                continue;

            if (paintClipBounds.getRight() <= layout.gridStartX || paintClipBounds.getX() >= layout.gridEndX)
                continue;

            const auto& segment = segments[layout.segmentIndex];
            const auto visibleStartX = std::clamp (paintClipBounds.getX(), layout.gridStartX, layout.gridEndX);
            const auto visibleEndX = std::clamp (paintClipBounds.getRight(), layout.gridStartX, layout.gridEndX);
            if (visibleEndX <= visibleStartX)
                continue;

            const auto startTicks = segment.localStart.ticks()
                + static_cast<std::int64_t> (std::floor ((static_cast<double> (visibleStartX - layout.gridStartX) / std::max (1, pixelsPerQuarter_)) * ticksPerBeat));
            const auto endTicks = segment.localStart.ticks()
                + static_cast<std::int64_t> (std::ceil ((static_cast<double> (visibleEndX - layout.gridStartX) / std::max (1, pixelsPerQuarter_)) * ticksPerBeat));
            const auto region = core::sequencing::Region {
                core::time::TickPosition::fromTicks (clampTicks (startTicks, segment.localStart.ticks(), segment.localEnd.ticks())),
                core::time::TickPosition::fromTicks (clampTicks (endTicks, segment.localStart.ticks(), segment.localEnd.ticks()))
            };
            if (region.duration().ticks() <= 0)
                continue;

            core::sequencing::sampleExpressionLane (lane, region, sampleStep, context, samples);
            for (const auto& sample : samples)
            {
                const auto x = xForLocalTick (layout, segment, sample.position);
                const auto y = expressionValueToY (sample.value, lane.polarity(), area);
                if (! pathStarted)
                {
                    expressionOverlayCache_.curvePath.startNewSubPath (static_cast<float> (x), y);
                    pathStarted = true;
                }
                else
                {
                    expressionOverlayCache_.curvePath.lineTo (static_cast<float> (x), y);
                }

                const auto limitProximity = lane.polarity() == core::sequencing::ExpressionLanePolarity::bipolar
                    ? std::abs (sample.value)
                    : std::max (sample.value, 1.0 - sample.value);
                if (limitProximity >= 0.94)
                    expressionOverlayCache_.warningXs.push_back (static_cast<float> (x));
            }
        }

        for (const auto& clip : lane.phraseEnvelopeClips())
        {
            const auto attackEnd = clip.phraseRegion().start() + clip.attackStage().duration;
            auto secondPartition = std::optional<core::time::TickPosition> {};
            if (clip.releaseStage().has_value())
                secondPartition = clip.phraseRegion().end() - clip.releaseStage()->duration;

            addExpressionClipStrip (expressionOverlayCache_.strips,
                                    segments,
                                    layouts,
                                    clip.phraseRegion(),
                                    paintClipBounds,
                                    expressionPhraseColour,
                                    3,
                                    clip.id(),
                                    std::nullopt,
                                    attackEnd,
                                    secondPartition);
        }

        for (const auto& clip : lane.cyclicClips())
        {
            addCyclicWaveformPath (expressionOverlayCache_.cyclicWaveforms,
                                   segments,
                                   layouts,
                                   lane,
                                   clip,
                                   paintClipBounds,
                                   area,
                                   context,
                                   waveformStepTicks);

            addExpressionClipStrip (expressionOverlayCache_.strips,
                                    segments,
                                    layouts,
                                    clip.phraseRegion(),
                                    paintClipBounds,
                                    expressionCyclicColour,
                                    13,
                                    std::nullopt,
                                    clip.id());
        }

        return expressionOverlayCache_;
    }

    void paintExpressionOverlay (juce::Graphics& graphics,
                                 const core::sequencing::MidiClip& clip,
                                 const std::vector<PianoRollSegment>& segments,
                                 const std::vector<SegmentLayout>& layouts,
                                 core::time::TickDuration,
                                 juce::Rectangle<int> paintClipBounds) const
    {
        const auto* lane = selectedExpressionLane (clip);
        if (lane == nullptr)
            return;

        const auto& overlay = cachedExpressionOverlay (*lane, segments, layouts, paintClipBounds);
        const auto area = overlay.valueArea;
        if (area.isEmpty())
            return;

        graphics.saveState();
        graphics.reduceClipRegion (paintClipBounds);

        for (const auto& strip : overlay.strips)
        {
            const auto selectedPhrase = strip.phraseEnvelopeId.has_value()
                && selectedPhraseEnvelopeId_.has_value()
                && *strip.phraseEnvelopeId == *selectedPhraseEnvelopeId_;
            const auto selectedCyclic = strip.cyclicClipId.has_value()
                && selectedCyclicClipId_.has_value()
                && *strip.cyclicClipId == *selectedCyclicClipId_;
            const auto selected = selectedPhrase || selectedCyclic;
            graphics.setColour (strip.colour.withAlpha (selected ? 0.48f : 0.32f));
            graphics.fillRoundedRectangle (strip.bounds.toFloat(), 3.0f);
            graphics.setColour (selected ? selectedOutlineColour.withAlpha (0.92f) : strip.colour.withAlpha (0.78f));
            graphics.drawRoundedRectangle (strip.bounds.toFloat().reduced (0.5f), 3.0f, selected ? 1.7f : 1.0f);

            graphics.setColour (surfaceColour.withAlpha (0.74f));
            if (strip.partitionX >= 0)
                graphics.drawVerticalLine (strip.partitionX, static_cast<float> (strip.bounds.getY()), static_cast<float> (strip.bounds.getBottom()));
            if (strip.secondPartitionX >= 0)
                graphics.drawVerticalLine (strip.secondPartitionX, static_cast<float> (strip.bounds.getY()), static_cast<float> (strip.bounds.getBottom()));
        }

        graphics.setColour (expressionGuideColour.withAlpha (0.20f));
        graphics.drawHorizontalLine (area.getY(), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
        graphics.drawHorizontalLine (area.getBottom() - 1, static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
        if (overlay.polarity == core::sequencing::ExpressionLanePolarity::bipolar)
        {
            const auto centerY = static_cast<int> (std::round (expressionValueToY (0.0, overlay.polarity, area)));
            graphics.setColour (expressionGuideColour.withAlpha (0.30f));
            graphics.drawHorizontalLine (centerY, static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
        }

        graphics.setColour (expressionWarningColour.withAlpha (0.10f));
        for (const auto x : overlay.warningXs)
            graphics.fillRect (juce::Rectangle<float> { x - 1.0f, static_cast<float> (area.getY()), 2.0f, static_cast<float> (area.getHeight()) });

        for (const auto& waveform : overlay.cyclicWaveforms)
        {
            const auto selected = selectedCyclicClipId_.has_value() && waveform.clipId == *selectedCyclicClipId_;
            graphics.setColour ((selected ? selectedNoteColour : expressionCyclicColour).withAlpha (selected ? 0.94f : 0.72f));
            graphics.strokePath (waveform.path, juce::PathStrokeType { selected ? 2.1f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        graphics.setColour (expressionCurveColour.withAlpha (0.88f));
        graphics.strokePath (overlay.curvePath, juce::PathStrokeType { 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        graphics.restoreState();
    }

    std::optional<core::sequencing::ExpressionClipId> phraseEnvelopeAt (juce::Point<float> point,
                                                                        const core::sequencing::MidiClip& clip,
                                                                        const std::vector<PianoRollSegment>& segments,
                                                                        const std::vector<SegmentLayout>& layouts) const
    {
        const auto* lane = selectedExpressionLane (clip);
        if (lane == nullptr)
            return std::nullopt;

        const auto paintBounds = getLocalBounds().withTrimmedTop (rulerHeight);
        const auto& overlay = cachedExpressionOverlay (*lane, segments, layouts, paintBounds);
        for (const auto& strip : overlay.strips)
        {
            if (strip.phraseEnvelopeId.has_value() && strip.bounds.expanded (2, 5).contains (point.roundToInt()))
                return strip.phraseEnvelopeId;
        }

        return std::nullopt;
    }

    std::optional<core::sequencing::ExpressionClipId> cyclicExpressionAt (juce::Point<float> point,
                                                                          const core::sequencing::MidiClip& clip,
                                                                          const std::vector<PianoRollSegment>& segments,
                                                                          const std::vector<SegmentLayout>& layouts) const
    {
        const auto* lane = selectedExpressionLane (clip);
        if (lane == nullptr)
            return std::nullopt;

        const auto paintBounds = getLocalBounds().withTrimmedTop (rulerHeight);
        const auto& overlay = cachedExpressionOverlay (*lane, segments, layouts, paintBounds);
        for (const auto& strip : overlay.strips)
        {
            if (strip.cyclicClipId.has_value() && strip.bounds.expanded (2, 5).contains (point.roundToInt()))
                return strip.cyclicClipId;
        }

        return std::nullopt;
    }

    std::optional<juce::Point<float>> visibleNoteCenter (const core::sequencing::MidiClip& clip,
                                                        const std::string& noteId,
                                                        const std::vector<PianoRollSegment>& segments,
                                                        const std::vector<SegmentLayout>& layouts,
                                                        juce::Rectangle<int> paintClipBounds) const
    {
        const auto* note = clip.findNoteById (noteId);
        if (note == nullptr)
            return std::nullopt;

        for (const auto& bounds : boundsForNote (*note, segments, layouts, paintClipBounds))
            if (bounds.bounds.intersects (paintClipBounds))
                return bounds.bounds.toFloat().getCentre();

        return std::nullopt;
    }

    static float pitchSlurCurveProgress (core::sequencing::ExpressionCurveShape shape, float alpha) noexcept
    {
        alpha = std::clamp (alpha, 0.0f, 1.0f);
        switch (shape)
        {
            case core::sequencing::ExpressionCurveShape::linear:
                return alpha;

            case core::sequencing::ExpressionCurveShape::logarithmic:
                return 1.0f - ((1.0f - alpha) * (1.0f - alpha));

            case core::sequencing::ExpressionCurveShape::exponential:
                return alpha * alpha;
        }

        return alpha;
    }

    std::vector<juce::Point<float>> pitchSlurTrajectoryPoints (const core::sequencing::MidiClip& clip,
                                                               const core::sequencing::PitchSlur& slur,
                                                               const std::vector<PianoRollSegment>& segments,
                                                               const std::vector<SegmentLayout>& layouts,
                                                               juce::Rectangle<int> paintClipBounds) const
    {
        const auto* destinationNote = clip.findNoteById (slur.destinationNoteId());
        if (destinationNote == nullptr)
            return {};

        const auto source = visibleNoteCenter (clip, slur.sourceNoteId(), segments, layouts, paintClipBounds);
        const auto destination = visibleNoteCenter (clip, slur.destinationNoteId(), segments, layouts, paintClipBounds);
        const auto transitionX = xForExpressionLocalTick (segments, layouts, destinationNote->startInClip());
        if (! source.has_value() || ! destination.has_value() || ! transitionX.has_value())
            return {};

        const auto minCenterX = std::min (source->x, destination->x);
        const auto maxCenterX = std::max (source->x, destination->x);
        const auto destinationStartX = std::clamp (static_cast<float> (*transitionX), minCenterX, maxCenterX);

        std::vector<juce::Point<float>> points;
        points.reserve (24);
        points.push_back (*source);
        points.push_back ({ destinationStartX, source->y });

        if (slur.slurTime().ticks() <= 0)
        {
            points.push_back ({ destinationStartX, destination->y });
            points.push_back (*destination);
            return points;
        }

        const auto clipEnd = core::time::TickPosition::fromTicks (editableClipLength (clip).ticks());
        const auto glideEndLocal = core::time::TickPosition::fromTicks (
            std::min ((destinationNote->startInClip() + slur.slurTime()).ticks(), clipEnd.ticks()));
        const auto glideEndTickX = xForExpressionLocalTick (segments, layouts, glideEndLocal);
        auto glideEndX = glideEndTickX.has_value()
            ? static_cast<float> (*glideEndTickX)
            : destinationStartX + static_cast<float> (durationToWidth (slur.slurTime()));
        glideEndX = std::max (destinationStartX + 1.0f, glideEndX);

        const auto samples = std::clamp (durationToWidth (slur.slurTime()) / 8, 4, 24);
        for (auto index = 1; index <= samples; ++index)
        {
            const auto alpha = static_cast<float> (index) / static_cast<float> (samples);
            const auto shaped = pitchSlurCurveProgress (slur.curveShape(), alpha);
            points.push_back ({ destinationStartX + ((glideEndX - destinationStartX) * alpha),
                                source->y + ((destination->y - source->y) * shaped) });
        }

        if (destination->x > glideEndX + 0.5f)
            points.push_back (*destination);

        return points;
    }

    void paintPitchSlurs (juce::Graphics& graphics,
                          const core::sequencing::MidiClip& clip,
                          const std::vector<PianoRollSegment>& segments,
                          const std::vector<SegmentLayout>& layouts,
                          juce::Rectangle<int> paintClipBounds) const
    {
        const auto* lane = clip.expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->pitchSlurs().empty())
            return;

        graphics.saveState();
        graphics.reduceClipRegion (paintClipBounds);

        for (const auto& slur : lane->pitchSlurs())
        {
            const auto points = pitchSlurTrajectoryPoints (clip, slur, segments, layouts, paintClipBounds);
            if (points.size() < 2)
                continue;

            const auto selected = selectedPitchSlurId_.has_value() && *selectedPitchSlurId_ == slur.id();

            juce::Path path;
            path.startNewSubPath (points.front());
            for (auto index = std::size_t { 1 }; index < points.size(); ++index)
                path.lineTo (points[index]);

            graphics.setColour ((selected ? selectedOutlineColour : expressionPhraseColour).withAlpha (selected ? 0.94f : 0.74f));
            graphics.strokePath (path, juce::PathStrokeType { selected ? 2.4f : 1.7f,
                                                              juce::PathStrokeType::mitered,
                                                              juce::PathStrokeType::rounded });
            graphics.setColour ((selected ? selectedNoteColour : expressionPhraseColour).withAlpha (selected ? 0.82f : 0.60f));
            graphics.fillEllipse (juce::Rectangle<float> { 5.0f, 5.0f }.withCentre (points.front()));
            if (const auto destination = visibleNoteCenter (clip, slur.destinationNoteId(), segments, layouts, paintClipBounds))
                graphics.fillEllipse (juce::Rectangle<float> { 5.0f, 5.0f }.withCentre (*destination));
        }

        graphics.restoreState();
    }

    struct VibratoVisualSettings
    {
        double amplitudeSemitones = 0.0;
        core::time::TickDuration attackTime {};
        core::time::TickDuration releaseTime {};
        std::string frequencyDivisionId;
        core::sequencing::CyclicWaveShape waveShape = core::sequencing::CyclicWaveShape::sine;
        double phase = 0.0;
        bool voiceOverride = false;
    };

    static float positiveModulo (float value, float modulus) noexcept
    {
        if (modulus <= 0.0f)
            return 0.0f;

        auto result = std::fmod (value, modulus);
        if (result < 0.0f)
            result += modulus;
        return result;
    }

    static float bipolarWave (core::sequencing::CyclicWaveShape shape, float phase) noexcept
    {
        const auto normalizedPhase = positiveModulo (phase, 1.0f);
        switch (shape)
        {
            case core::sequencing::CyclicWaveShape::sine:
                return std::sin (normalizedPhase * juce::MathConstants<float>::twoPi);

            case core::sequencing::CyclicWaveShape::triangle:
                return 1.0f - (4.0f * std::abs (normalizedPhase - 0.5f));

            case core::sequencing::CyclicWaveShape::rampUp:
                return (normalizedPhase * 2.0f) - 1.0f;

            case core::sequencing::CyclicWaveShape::rampDown:
                return 1.0f - (normalizedPhase * 2.0f);

            case core::sequencing::CyclicWaveShape::square:
                return normalizedPhase < 0.5f ? 1.0f : -1.0f;
        }

        return 0.0f;
    }

    VibratoVisualSettings visualSettingsForVibratoVoice (const core::sequencing::VibratoExpression& vibrato,
                                                         const std::string& noteId) const
    {
        for (const auto& override : vibrato.voiceOverrides())
        {
            if (override.noteId == noteId)
            {
                return VibratoVisualSettings {
                    override.amplitudeSemitones,
                    override.attackTime,
                    override.releaseTime,
                    override.frequencyDivisionId,
                    override.waveShape,
                    override.phase,
                    true
                };
            }
        }

        return VibratoVisualSettings {
            vibrato.amplitudeSemitones(),
            vibrato.attackTime(),
            vibrato.releaseTime(),
            vibrato.frequencyDivisionId(),
            vibrato.waveShape(),
            vibrato.phase(),
            false
        };
    }

    double vibratoFadeAt (const core::sequencing::Region& region,
                          core::time::TickPosition position,
                          core::time::TickDuration attackTime,
                          core::time::TickDuration releaseTime) const noexcept
    {
        if (! region.contains (position))
            return 0.0;

        auto attack = 1.0;
        if (attackTime.ticks() > 0)
        {
            const auto elapsed = static_cast<double> ((position - region.start()).ticks());
            attack = std::clamp (elapsed / static_cast<double> (attackTime.ticks()), 0.0, 1.0);
        }

        auto release = 1.0;
        if (releaseTime.ticks() > 0)
        {
            const auto releaseStart = region.end() - releaseTime;
            if (position >= releaseStart)
            {
                const auto elapsed = static_cast<double> ((position - releaseStart).ticks());
                release = 1.0 - std::clamp (elapsed / static_cast<double> (releaseTime.ticks()), 0.0, 1.0);
            }
        }

        return std::min (attack, release);
    }

    core::time::TickPosition localPositionForX (const SegmentLayout& layout,
                                                const PianoRollSegment& segment,
                                                float x) const
    {
        const auto pixelOffset = static_cast<double> (x - static_cast<float> (layout.gridStartX));
        const auto ticks = segment.localStart.ticks()
            + static_cast<std::int64_t> (std::llround ((pixelOffset / static_cast<double> (std::max (1, pixelsPerQuarter_))) * ticksPerBeat));
        return core::time::TickPosition::fromTicks (clampTicks (ticks, segment.localStart.ticks(), segment.localEnd.ticks()));
    }

    void paintVibratoPaths (juce::Graphics& graphics,
                            const core::sequencing::MidiClip& clip,
                            const std::vector<PianoRollSegment>& segments,
                            const std::vector<SegmentLayout>& layouts,
                            juce::Rectangle<int> paintClipBounds) const
    {
        const auto* lane = clip.expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (lane == nullptr || lane->vibratoExpressions().empty())
            return;

        graphics.saveState();
        graphics.reduceClipRegion (paintClipBounds);

        for (const auto& vibrato : lane->vibratoExpressions())
        {
            const auto selected = selectedVibratoId_.has_value() && *selectedVibratoId_ == vibrato.id();

            for (const auto& noteId : vibrato.sourceNoteIds())
            {
                const auto* note = clip.findNoteById (noteId);
                if (note == nullptr)
                    continue;

                const auto settings = visualSettingsForVibratoVoice (vibrato, noteId);
                const auto depthPixels = static_cast<float> (
                    std::clamp (3.0 + std::sqrt (std::max (0.0, settings.amplitudeSemitones)) * 10.0,
                                3.0,
                                static_cast<double> (std::max (4, rowHeight_ - 2))));
                const auto frequencyDefinition = core::time::gridDivisionDefinitionFor (settings.frequencyDivisionId,
                                                                                       appServices_.project().rhythmSettings());
                const auto frequencyTicks = std::max<std::int64_t> (1, frequencyDefinition.tickDuration.ticks());
                const auto colour = settings.voiceOverride
                    ? selectedNoteColour
                    : (selected ? selectedNoteColour : expressionCyclicColour);
                graphics.setColour (colour.withAlpha (selected ? 0.94f : (settings.voiceOverride ? 0.86f : 0.70f)));

                for (const auto& renderBounds : boundsForNote (*note, segments, layouts, paintClipBounds))
                {
                    if (renderBounds.segmentIndex >= segments.size() || renderBounds.segmentIndex >= layouts.size())
                        continue;

                    const auto bounds = renderBounds.bounds.toFloat().reduced (2.0f, 2.0f);
                    if (! bounds.toNearestInt().intersects (paintClipBounds))
                        continue;

                    const auto& segment = segments[renderBounds.segmentIndex];
                    const auto& layout = layouts[renderBounds.segmentIndex];
                    juce::Path path;
                    const auto samples = std::clamp (static_cast<int> (std::ceil (bounds.getWidth() / 5.0f)), 18, 96);
                    for (auto index = 0; index <= samples; ++index)
                    {
                        const auto alpha = static_cast<float> (index) / static_cast<float> (samples);
                        const auto x = bounds.getX() + alpha * bounds.getWidth();
                        const auto localPosition = localPositionForX (layout, segment, x);
                        const auto fade = static_cast<float> (vibratoFadeAt (vibrato.phraseRegion(),
                                                                             localPosition,
                                                                             settings.attackTime,
                                                                             settings.releaseTime));
                        const auto phase = (static_cast<float> ((localPosition - vibrato.phraseRegion().start()).ticks())
                                            / static_cast<float> (frequencyTicks))
                            + static_cast<float> (settings.phase);
                        const auto wave = bipolarWave (settings.waveShape, phase);
                        const auto y = bounds.getCentreY() - (wave * depthPixels * fade);
                        if (index == 0)
                            path.startNewSubPath (x, y);
                        else
                            path.lineTo (x, y);
                    }

                    graphics.strokePath (path, juce::PathStrokeType { selected ? 1.8f : 1.25f,
                                                                      juce::PathStrokeType::curved,
                                                                      juce::PathStrokeType::rounded });

                    if (selected && bounds.getWidth() > 42.0f)
                    {
                        graphics.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
                        graphics.drawText (juce::String (settings.amplitudeSemitones * 100.0, 0) + "c",
                                           bounds.toNearestInt().reduced (4, 0),
                                           juce::Justification::centredRight);
                    }
                }
            }
        }

        graphics.restoreState();
    }

    void paintReleaseGhosts (juce::Graphics& graphics,
                             const core::sequencing::MidiClip& clip,
                             const std::vector<PianoRollSegment>& segments,
                             const std::vector<SegmentLayout>& layouts,
                             juce::Rectangle<int> paintClipBounds) const
    {
        const auto ghosts = releaseGhostsForClip (clip);
        if (ghosts.empty())
            return;

        graphics.saveState();
        graphics.reduceClipRegion (paintClipBounds);

        for (const auto& ghost : ghosts)
        {
            const auto* note = clip.findNoteById (ghost.noteId);
            if (note == nullptr)
                continue;

            const auto selected = isSelected (note->id());
            for (const auto& bounds : boundsForGhost (*note, ghost, segments, layouts))
            {
                if (! bounds.bounds.intersects (paintClipBounds))
                    continue;

                graphics.setColour ((selected ? selectedNoteColour : expressionGuideColour).withAlpha (selected ? 0.20f : 0.13f));
                graphics.fillRoundedRectangle (bounds.bounds.toFloat(), 3.0f);
                graphics.setColour ((selected ? selectedOutlineColour : expressionGuideColour).withAlpha (0.36f));
                graphics.drawRoundedRectangle (bounds.bounds.toFloat().reduced (0.5f), 3.0f, 1.0f);
            }
        }

        graphics.restoreState();
    }

    std::vector<SegmentLayout> segmentLayouts (const std::vector<PianoRollSegment>& segments) const
    {
        std::vector<SegmentLayout> layouts;
        layouts.reserve (segments.size());

        auto x = pitchHeaderWidth;
        for (std::size_t index = 0; index < segments.size(); ++index)
        {
            const auto& segment = segments[index];
            SegmentLayout layout;
            layout.segmentIndex = index;
            layout.headerWidth = index == 0 ? pitchHeaderWidth : harmonicChangeHeaderWidth;

            if (index == 0)
            {
                layout.headerX = 0;
                layout.gridStartX = pitchHeaderWidth;
            }
            else
            {
                layout.headerX = x;
                layout.gridStartX = x + harmonicChangeHeaderWidth;
                x = layout.gridStartX;
            }

            layout.gridEndX = layout.gridStartX + durationToWidth (segment.localEnd - segment.localStart);
            x = layout.gridEndX;
            layouts.push_back (layout);
        }

        return layouts;
    }

    static int maximumLaneCount (const std::vector<PianoRollSegment>& segments)
    {
        auto laneCount = 0;
        for (const auto& segment : segments)
            laneCount = std::max (laneCount, static_cast<int> (segment.lanes.size()));

        return laneCount;
    }

    int xForLocalTick (const SegmentLayout& layout,
                       const PianoRollSegment& segment,
                       core::time::TickPosition localPosition) const
    {
        const auto clampedTicks = clampTicks (localPosition.ticks(), segment.localStart.ticks(), segment.localEnd.ticks());
        return layout.gridStartX + durationToWidth (core::time::TickDuration::fromTicks (clampedTicks - segment.localStart.ticks()));
    }

    std::optional<std::size_t> segmentIndexForLocalTick (const std::vector<PianoRollSegment>& segments,
                                                         core::time::TickPosition localPosition) const
    {
        for (std::size_t index = 0; index < segments.size(); ++index)
        {
            const auto& segment = segments[index];
            if (localPosition >= segment.localStart && localPosition < segment.localEnd)
                return index;
        }

        if (! segments.empty() && localPosition == segments.back().localEnd)
            return segments.size() - 1;

        return std::nullopt;
    }

    std::optional<std::size_t> segmentIndexForPoint (juce::Point<float> point,
                                                     const std::vector<PianoRollSegment>& segments,
                                                     const std::vector<SegmentLayout>& layouts) const
    {
        const auto x = static_cast<int> (std::floor (point.x));
        for (const auto& layout : layouts)
        {
            if (x >= layout.headerX && x < layout.headerX + layout.headerWidth)
                return std::nullopt;

            if (x >= layout.gridStartX && x < layout.gridEndX)
                return layout.segmentIndex;
        }

        if (! layouts.empty() && x >= layouts.back().gridEndX)
            return layouts.back().segmentIndex;

        juce::ignoreUnused (segments);
        return std::nullopt;
    }

    std::optional<core::time::TickPosition> xToSnappedTickInSegments (float x,
                                                                      const std::vector<PianoRollSegment>& segments,
                                                                      const std::vector<SegmentLayout>& layouts) const
    {
        const auto pixelX = static_cast<int> (std::floor (x));
        for (const auto& layout : layouts)
        {
            if (pixelX >= layout.headerX && pixelX < layout.headerX + layout.headerWidth)
                return std::nullopt;

            if (pixelX < layout.gridStartX || pixelX > layout.gridEndX)
                continue;

            const auto& segment = segments[layout.segmentIndex];
            const auto relativeX = std::max (0, pixelX - layout.gridStartX);
            const auto ticks = static_cast<std::int64_t> (std::llround ((static_cast<double> (relativeX) / pixelsPerQuarter_) * ticksPerBeat));
            const auto snapped = roundToSnap (segment.localStart.ticks() + ticks, snapTicks());
            return core::time::TickPosition::fromTicks (clampTicks (snapped, segment.localStart.ticks(), segment.localEnd.ticks()));
        }

        return std::nullopt;
    }

    std::optional<juce::String> tooltipForHeaderAt (juce::Point<float> point,
                                                    const std::vector<PianoRollSegment>& segments,
                                                    const std::vector<SegmentLayout>& layouts) const
    {
        const auto x = static_cast<int> (std::floor (point.x));
        const auto y = static_cast<int> (std::floor (point.y));

        for (const auto& layout : layouts)
        {
            if (layout.segmentIndex == 0)
                continue;

            if (x >= layout.headerX
                && x < layout.headerX + layout.headerWidth
                && y >= rulerHeight)
            {
                const auto& segment = segments[layout.segmentIndex];
                return "Key or scale change at " + barBeatText (segment.localStart) + ": note lanes update here";
            }
        }

        return std::nullopt;
    }

    int laneToY (int laneIndex) const
    {
        return rulerHeight + (laneIndex * rowHeight_);
    }

    juce::Rectangle<int> visiblePaintBounds (juce::Graphics& graphics) const
    {
        auto bounds = graphics.getClipBounds();
        if (const auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            const auto viewportArea = juce::Rectangle<int> {
                viewport->getViewPositionX(),
                viewport->getViewPositionY(),
                viewport->getWidth(),
                viewport->getHeight()
            };
            bounds = bounds.getIntersection (viewportArea);
        }

        return bounds;
    }

    std::vector<LocalTickRange> visibleTickRangesForPaint (const std::vector<PianoRollSegment>& segments,
                                                           const std::vector<SegmentLayout>& layouts,
                                                           juce::Rectangle<int> paintClipBounds) const
    {
        std::vector<LocalTickRange> ranges;
        ranges.reserve (layouts.size());

        for (const auto& layout : layouts)
        {
            if (layout.segmentIndex >= segments.size())
                continue;

            if (paintClipBounds.getRight() <= layout.gridStartX || paintClipBounds.getX() >= layout.gridEndX)
                continue;

            const auto& segment = segments[layout.segmentIndex];
            const auto gridWidth = std::max (1, layout.gridEndX - layout.gridStartX);
            const auto visibleStartPixels = std::clamp (paintClipBounds.getX() - layout.gridStartX, 0, gridWidth);
            const auto visibleEndPixels = std::clamp (paintClipBounds.getRight() - layout.gridStartX, 0, gridWidth);
            if (visibleEndPixels <= visibleStartPixels)
                continue;

            const auto ticksPerPixel = static_cast<double> (ticksPerBeat) / static_cast<double> (std::max (1, pixelsPerQuarter_));
            const auto paddingTicks = static_cast<std::int64_t> (std::ceil (8.0 * ticksPerPixel));
            const auto startOffsetTicks = static_cast<std::int64_t> (std::floor (static_cast<double> (visibleStartPixels) * ticksPerPixel));
            const auto endOffsetTicks = static_cast<std::int64_t> (std::ceil (static_cast<double> (visibleEndPixels) * ticksPerPixel));
            const auto segmentStartTicks = segment.localStart.ticks();
            const auto segmentEndTicks = segment.localEnd.ticks();

            ranges.push_back (LocalTickRange {
                clampTicks (segmentStartTicks + startOffsetTicks - paddingTicks, segmentStartTicks, segmentEndTicks),
                clampTicks (segmentStartTicks + endOffsetTicks + paddingTicks, segmentStartTicks, segmentEndTicks)
            });
        }

        return ranges;
    }

    static bool noteIntersectsTickRanges (const core::sequencing::MidiNote& note,
                                          const std::vector<LocalTickRange>& ranges)
    {
        const auto noteStartTicks = note.startInClip().ticks();
        const auto noteEndTicks = note.endInClip().ticks();

        for (const auto& range : ranges)
        {
            if (noteStartTicks < range.endTicks && noteEndTicks > range.startTicks)
                return true;
        }

        return false;
    }

    static void hashCombine (std::size_t& seed, std::size_t value) noexcept
    {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    }

    std::size_t playheadLayoutFingerprint (const core::sequencing::MidiClip& clip) const
    {
        auto fingerprint = std::size_t {};
        hashCombine (fingerprint, selectedClip_.has_value() ? std::hash<std::string> {} (selectedClip_->trackId) : 0u);
        hashCombine (fingerprint, std::hash<std::string> {} (clip.id()));
        hashCombine (fingerprint, static_cast<std::size_t> (clip.startInProject().ticks()));
        hashCombine (fingerprint, static_cast<std::size_t> (clip.length().ticks()));
        hashCombine (fingerprint, static_cast<std::size_t> (clip.sourceLength().ticks()));
        hashCombine (fingerprint, static_cast<std::size_t> (pixelsPerQuarter_));

        const auto& structure = appServices_.project().musicalStructure();
        hashCombine (fingerprint, structure.keyCenterRegions().size());
        for (const auto& region : structure.keyCenterRegions())
        {
            hashCombine (fingerprint, static_cast<std::size_t> (region.start().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (region.end().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (region.pitchClass().semitonesFromC()));
        }

        hashCombine (fingerprint, structure.scaleModeRegions().size());
        for (const auto& region : structure.scaleModeRegions())
        {
            hashCombine (fingerprint, static_cast<std::size_t> (region.start().ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (region.end().ticks()));
            hashCombine (fingerprint, std::hash<std::string> {} (region.scaleDefinitionName()));
        }

        return fingerprint;
    }

    const PlayheadLayoutCache& cachedPlayheadLayoutFor (const core::sequencing::MidiClip& clip) const
    {
        const auto fingerprint = playheadLayoutFingerprint (clip);
        if (playheadLayoutCache_.fingerprint != fingerprint || playheadLayoutCache_.segments.empty())
        {
            playheadLayoutCache_.fingerprint = fingerprint;
            playheadLayoutCache_.segments = visiblePitchSegments (clip);
            playheadLayoutCache_.layouts = segmentLayouts (playheadLayoutCache_.segments);
        }

        return playheadLayoutCache_;
    }

    std::size_t laneBackgroundFingerprint (const std::vector<PianoRollSegment>& segments,
                                           const std::vector<SegmentLayout>& layouts,
                                           int contentWidth) const
    {
        auto fingerprint = std::size_t {};
        hashCombine (fingerprint, static_cast<std::size_t> (contentWidth));
        hashCombine (fingerprint, static_cast<std::size_t> (getHeight()));
        hashCombine (fingerprint, static_cast<std::size_t> (rowHeight_));
        hashCombine (fingerprint, static_cast<std::size_t> (pixelsPerQuarter_));
        hashCombine (fingerprint, chromaticReveal_ ? 1u : 0u);
        hashCombine (fingerprint, segments.size());
        hashCombine (fingerprint, layouts.size());

        for (const auto& layout : layouts)
        {
            hashCombine (fingerprint, layout.segmentIndex);
            hashCombine (fingerprint, static_cast<std::size_t> (layout.headerX));
            hashCombine (fingerprint, static_cast<std::size_t> (layout.headerWidth));
            hashCombine (fingerprint, static_cast<std::size_t> (layout.gridStartX));
            hashCombine (fingerprint, static_cast<std::size_t> (layout.gridEndX));
        }

        for (const auto& segment : segments)
        {
            hashCombine (fingerprint, static_cast<std::size_t> (segment.localStart.ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (segment.localEnd.ticks()));
            hashCombine (fingerprint, static_cast<std::size_t> (segment.context.keyCenter().semitonesFromC()));
            hashCombine (fingerprint, std::hash<std::string> {} (segment.context.scaleDefinitionName()));
            hashCombine (fingerprint, segment.insertedHeader ? 1u : 0u);
            hashCombine (fingerprint, segment.lanes.size());

            for (const auto& lane : segment.lanes)
            {
                hashCombine (fingerprint, static_cast<std::size_t> (lane.midiPitch));
                hashCombine (fingerprint, static_cast<std::size_t> (lane.spelling.letter()));
                hashCombine (fingerprint, static_cast<std::size_t> (lane.spelling.accidental().semitoneOffset() + 2));
                hashCombine (fingerprint, lane.nativeScale ? 1u : 0u);
                hashCombine (fingerprint, lane.usedAccidental ? 1u : 0u);
                hashCombine (fingerprint, lane.greyed ? 1u : 0u);
            }
        }

        return fingerprint;
    }

    void renderLaneBackground (juce::Graphics& graphics,
                               const std::vector<PianoRollSegment>& segments,
                               const std::vector<SegmentLayout>& layouts,
                               int contentWidth,
                               juce::Rectangle<int> renderArea) const
    {
        graphics.setColour (headerColour);
        graphics.fillRect (0, 0, contentWidth, rulerHeight);

        for (const auto& layout : layouts)
        {
            const auto& segment = segments[layout.segmentIndex];
            const auto layoutBounds = juce::Rectangle<int> {
                layout.headerX,
                0,
                std::max (1, layout.gridEndX - layout.headerX),
                getHeight()
            };
            if (! layoutBounds.intersects (renderArea))
                continue;

            graphics.setColour (pitchHeaderColour);
            graphics.fillRect (layout.headerX, 0, layout.headerWidth, getHeight());
            graphics.setColour (gridColour.withAlpha (layout.segmentIndex == 0 ? 0.68f : 0.92f));
            graphics.fillRect (layout.headerX, 0, 1, getHeight());
            graphics.fillRect (layout.headerX + layout.headerWidth, 0, 1, getHeight());

            for (std::size_t laneIndex = 0; laneIndex < segment.lanes.size(); ++laneIndex)
            {
                const auto& lane = segment.lanes[laneIndex];
                const auto y = laneToY (static_cast<int> (laneIndex));
                if (y >= renderArea.getBottom() || y + rowHeight_ <= renderArea.getY())
                    continue;

                graphics.setColour (laneColour (lane));
                graphics.fillRect (layout.gridStartX, y, layout.gridEndX - layout.gridStartX, rowHeight_);

                graphics.setColour (gridColour.withAlpha (lane.greyed ? 0.32f : 0.55f));
                graphics.drawHorizontalLine (y, static_cast<float> (layout.headerX), static_cast<float> (layout.gridEndX));

                const auto labelBounds = juce::Rectangle<int> {
                    layout.headerX + 3,
                    y,
                    layout.headerWidth - 6,
                    rowHeight_
                };
                if (labelBounds.intersects (renderArea))
                {
                    graphics.setColour (lane.greyed ? mutedTextColour.withAlpha (0.52f) : mutedTextColour);
                    graphics.setFont (juce::FontOptions { layout.segmentIndex == 0 ? 10.0f : 9.5f });
                    graphics.drawText (laneLabel (lane), labelBounds, juce::Justification::centredRight);
                }
            }
        }
    }

    void paintLaneBackground (juce::Graphics& graphics,
                              const std::vector<PianoRollSegment>& segments,
                              const std::vector<SegmentLayout>& layouts,
                              int contentWidth,
                              juce::Rectangle<int> paintClipBounds)
    {
        const auto cacheArea = paintClipBounds.getIntersection ({
            0,
            0,
            std::max (1, contentWidth),
            std::max (1, getHeight())
        });
        if (cacheArea.isEmpty())
            return;

        const auto cacheWidth = cacheArea.getWidth();
        const auto cacheHeight = cacheArea.getHeight();
        const auto fingerprint = laneBackgroundFingerprint (segments, layouts, contentWidth);

        if (! laneBackgroundCache_.image.isValid()
            || laneBackgroundCache_.fingerprint != fingerprint
            || laneBackgroundCache_.x != cacheArea.getX()
            || laneBackgroundCache_.y != cacheArea.getY()
            || laneBackgroundCache_.width != cacheWidth
            || laneBackgroundCache_.height != cacheHeight)
        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint lanes-cache-build" };
            laneBackgroundCache_.image = juce::Image { juce::Image::ARGB, cacheWidth, cacheHeight, true };
            laneBackgroundCache_.fingerprint = fingerprint;
            laneBackgroundCache_.x = cacheArea.getX();
            laneBackgroundCache_.y = cacheArea.getY();
            laneBackgroundCache_.width = cacheWidth;
            laneBackgroundCache_.height = cacheHeight;

            juce::Graphics cacheGraphics { laneBackgroundCache_.image };
            cacheGraphics.addTransform (juce::AffineTransform::translation (
                static_cast<float> (-cacheArea.getX()),
                static_cast<float> (-cacheArea.getY())));
            renderLaneBackground (cacheGraphics, segments, layouts, contentWidth, cacheArea);
        }

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "PianoRollContent::paint lanes-cache-draw" };
            graphics.saveState();
            graphics.reduceClipRegion (cacheArea);
            graphics.drawImageAt (laneBackgroundCache_.image, cacheArea.getX(), cacheArea.getY());
            graphics.restoreState();
        }
    }

    static std::optional<int> laneIndexForPitch (int midiPitch, const std::vector<PianoLane>& lanes)
    {
        for (std::size_t index = 0; index < lanes.size(); ++index)
        {
            if (lanes[index].midiPitch == midiPitch)
                return static_cast<int> (index);
        }

        return std::nullopt;
    }

    static std::optional<int> laneIndexForNoteState (int midiPitch,
                                                     std::optional<core::music_theory::NoteName> spelling,
                                                     const std::vector<PianoLane>& lanes)
    {
        if (const auto exactLane = laneIndexForPitch (midiPitch, lanes))
            return exactLane;

        const auto pitch = core::music_theory::MidiPitch::fromValue (midiPitch);
        const auto noteSpelling = spelling.value_or (core::music_theory::spellPitchClass (pitch.pitchClass()));
        const auto octave = pitch.octave();

        for (std::size_t index = 0; index < lanes.size(); ++index)
        {
            const auto lanePitch = core::music_theory::MidiPitch::fromValue (lanes[index].midiPitch);
            if (lanePitch.octave() == octave && lanes[index].spelling.letter() == noteSpelling.letter())
                return static_cast<int> (index);
        }

        return std::nullopt;
    }

    static std::optional<int> laneIndexForNote (const core::sequencing::MidiNote& note,
                                                const std::vector<PianoLane>& lanes)
    {
        return laneIndexForNoteState (note.pitch().value(), note.spelling(), lanes);
    }

    std::optional<PianoLane> laneAtY (float y, const std::vector<PianoLane>& lanes) const
    {
        const auto row = static_cast<int> (std::floor ((y - rulerHeight) / rowHeight_));
        if (row < 0 || row >= static_cast<int> (lanes.size()))
            return std::nullopt;

        return lanes[static_cast<std::size_t> (row)];
    }

    static juce::String laneLabel (const PianoLane& lane)
    {
        return core::music_theory::MidiPitch::fromValue (lane.midiPitch).nameWithOctave (lane.spelling);
    }

    static juce::String noteLabelForState (int midiPitch,
                                           std::optional<core::music_theory::NoteName> spelling,
                                           const std::vector<PianoLane>& lanes)
    {
        const auto pitch = core::music_theory::MidiPitch::fromValue (midiPitch);
        for (const auto& lane : lanes)
        {
            if (lane.midiPitch == midiPitch)
                return pitch.nameWithOctave (lane.spelling);
        }

        if (spelling.has_value())
            return pitch.nameWithOctave (*spelling);

        return pitch.nameWithOctave();
    }

    static juce::String noteLabel (const core::sequencing::MidiNote& note, const std::vector<PianoLane>& lanes)
    {
        return noteLabelForState (note.pitch().value(), note.spelling(), lanes);
    }

    static const PianoLane* laneForPitch (int midiPitch, const std::vector<PianoLane>& lanes)
    {
        for (const auto& lane : lanes)
        {
            if (lane.midiPitch == midiPitch)
                return &lane;
        }

        return nullptr;
    }

    static const PianoLane* laneForNote (const core::sequencing::MidiNote& note,
                                         const std::vector<PianoLane>& lanes)
    {
        return laneForNoteState (note.pitch().value(), note.spelling(), lanes);
    }

    static const PianoLane* laneForNoteState (int midiPitch,
                                              std::optional<core::music_theory::NoteName> spelling,
                                              const std::vector<PianoLane>& lanes)
    {
        const auto laneIndex = laneIndexForNoteState (midiPitch, spelling, lanes);
        if (! laneIndex.has_value())
            return nullptr;

        return &lanes[static_cast<std::size_t> (*laneIndex)];
    }

    static juce::Colour laneColour (const PianoLane& lane)
    {
        if (lane.greyed)
            return greyedLaneColour;

        if (lane.usedAccidental)
            return usedAccidentalLaneColour;

        return isAccidentalPitchClass (lane.midiPitch) ? accidentalLaneColour : naturalLaneColour;
    }

    static juce::Colour noteColourForLane (const PianoLane* lane)
    {
        if (lane == nullptr)
            return noteColour;

        if (lane->greyed)
            return noteColour.withAlpha (0.54f);

        if (lane->usedAccidental)
            return juce::Colour { 0xffe0a8ff };

        return noteColour;
    }

    static std::optional<juce::Colour> overlayColourForRole (core::sequencing::HarmonicOverlayRole role)
    {
        switch (role)
        {
            case core::sequencing::HarmonicOverlayRole::root:
                return rootOverlayColour.withAlpha (0.30f);
            case core::sequencing::HarmonicOverlayRole::chordTone:
                return chordToneOverlayColour.withAlpha (0.22f);
            case core::sequencing::HarmonicOverlayRole::nonChordScaleTone:
                return nonChordScaleToneOverlayColour.withAlpha (0.055f);
            case core::sequencing::HarmonicOverlayRole::accidental:
                return accidentalOverlayColour.withAlpha (0.24f);
            case core::sequencing::HarmonicOverlayRole::none:
                return std::nullopt;
        }

        return std::nullopt;
    }

    int durationToWidth (core::time::TickDuration duration) const
    {
        return static_cast<int> (std::llround ((static_cast<double> (duration.ticks()) / ticksPerBeat) * pixelsPerQuarter_));
    }

    std::int64_t snappedTickDelta (int pixels) const
    {
        const auto ticks = static_cast<std::int64_t> (std::llround ((static_cast<double> (pixels) / pixelsPerQuarter_) * ticksPerBeat));
        return roundToSnap (ticks, snapTicks());
    }

    std::int64_t snapTicks() const
    {
        const auto& settings = appServices_.project().rhythmSettings();
        const auto definition = core::time::gridDivisionDefinitionFor (settings.currentGridDivisionId(), settings);
        return std::max<std::int64_t> (1, definition.tickDuration.ticks());
    }

    static std::string nextNoteId (const core::sequencing::MidiClip& clip)
    {
        return nextNoteId (clip, {});
    }

    static std::string nextNoteId (const core::sequencing::MidiClip& clip, const std::vector<core::sequencing::MidiNote>& pendingNotes)
    {
        auto index = clip.notes().size() + 1;
        while (true)
        {
            auto id = "note-" + std::to_string (index);
            if (clip.findNoteById (id) == nullptr)
            {
                const auto pending = std::any_of (pendingNotes.begin(), pendingNotes.end(), [&id] (const auto& note) {
                    return note.id() == id;
                });
                if (! pending)
                    return id;
            }

            ++index;
        }
    }

    app::AppServices& appServices_;
    std::optional<ClipSelection>& selectedClip_;
    core::time::TickPosition& playheadTick_;
    bool& chromaticReveal_;
    bool& expressionModeEnabled_;
    bool& expressionReleaseModeEnabled_;
    std::optional<core::sequencing::ExpressionLaneId>& selectedExpressionLaneId_;
    std::optional<core::sequencing::ExpressionClipId>& selectedPhraseEnvelopeId_;
    std::function<void()>& expressionSelectionChanged_;
    std::function<void()>& toggleChromaticReveal_;
    std::vector<std::string> selectedNoteIds_;
    std::vector<core::sequencing::MidiNote> noteClipboard_;
    std::vector<core::sequencing::MidiNote> activeArpeggioSourceNotes_;
    std::optional<core::sequencing::PhraseEnvelopeEditKey> expressionEditPrefix_;
    bool expressionEditPrefixTargetsCyclic_ = false;
    std::optional<core::sequencing::ExpressionClipId> selectedCyclicClipId_;
    std::optional<core::sequencing::ExpressionClipId> selectedPitchSlurId_;
    std::optional<core::sequencing::ExpressionClipId> selectedVibratoId_;
    bool selectedPitchSlurSingleVoiceEdit_ = false;
    bool pitchSlurEditPrefix_ = false;
    core::sequencing::PhraseEnvelopeActiveSegment activePhraseSegment_ = core::sequencing::PhraseEnvelopeActiveSegment::attack;
    std::optional<DragState> dragState_;
    std::optional<MarqueeState> marqueeState_;
    bool draggingPasteCursor_ = false;
    int pixelsPerQuarter_ = defaultPixelsPerQuarter;
    int rowHeight_ = defaultRowHeight;
    LaneBackgroundCache laneBackgroundCache_;
    mutable ExpressionOverlayCache expressionOverlayCache_;
    mutable PlayheadLayoutCache playheadLayoutCache_;
    std::string arpeggioSubdivisionId_ { core::time::ProjectRhythmSettings::defaultGridDivisionId };
    core::sequencing::ArpeggioPattern arpeggioPattern_ = core::sequencing::ArpeggioPattern::up;
};

class PianoRollComponent::ExpressionLaneListComponent final : public juce::Component
{
public:
    explicit ExpressionLaneListComponent (PianoRollComponent& owner)
        : owner_ (owner)
    {
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (headerColour);

        const auto* clip = owner_.selectedMidiClip();
        if (clip == nullptr)
        {
            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 12.0f });
            graphics.drawText ("No clip", getLocalBounds().reduced (10), juce::Justification::centred);
            return;
        }

        auto row = getLocalBounds().reduced (8, 6).removeFromTop (laneRowHeight);
        for (const auto& lane : clip->expressionState().lanes())
        {
            const auto selected = owner_.selectedExpressionLaneId_.has_value()
                && *owner_.selectedExpressionLaneId_ == lane.id();

            graphics.setColour (selected ? buttonOnColour : buttonColour);
            graphics.fillRoundedRectangle (row.toFloat(), 4.0f);

            graphics.setColour (selected ? selectedOutlineColour.withAlpha (0.55f) : gridColour);
            graphics.drawRoundedRectangle (row.toFloat().reduced (0.5f), 4.0f, selected ? 1.4f : 1.0f);

            auto textBounds = row.reduced (10, 0);
            graphics.setColour (lane.enabled() ? textColour : mutedTextColour.withAlpha (0.65f));
            graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
            graphics.drawText (lane.name(), textBounds.removeFromTop (18), juce::Justification::centredLeft);

            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 10.5f });
            graphics.drawText (lane.polarity() == core::sequencing::ExpressionLanePolarity::bipolar ? "Bipolar" : "Unipolar",
                               textBounds,
                               juce::Justification::centredLeft);

            row.translate (0, laneRowHeight + 6);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto* clip = owner_.selectedMidiClip();
        if (clip == nullptr)
            return;

        const auto index = laneIndexAt (event.position);
        if (! index.has_value() || *index >= clip->expressionState().lanes().size())
            return;

        owner_.setSelectedExpressionLane (clip->expressionState().lanes()[*index].id());
    }

    int preferredHeight() const
    {
        const auto* clip = owner_.selectedMidiClip();
        const auto laneCount = clip == nullptr ? std::size_t { 2 } : clip->expressionState().lanes().size();
        return 12 + static_cast<int> (laneCount) * (laneRowHeight + 6);
    }

private:
    static constexpr int laneRowHeight = 44;

    std::optional<std::size_t> laneIndexAt (juce::Point<float> point) const
    {
        const auto y = static_cast<int> (point.y) - 6;
        if (y < 0)
            return std::nullopt;

        const auto stride = laneRowHeight + 6;
        const auto index = y / stride;
        const auto rowOffset = y % stride;
        if (rowOffset >= laneRowHeight)
            return std::nullopt;

        return static_cast<std::size_t> (index);
    }

    PianoRollComponent& owner_;
};

class PianoRollComponent::PhraseEnvelopeControlPanel final : public juce::Component
{
public:
    explicit PhraseEnvelopeControlPanel (PianoRollComponent& owner)
        : owner_ (owner)
    {
        titleLabel_.setText ("Phrase Envelope", juce::dontSendNotification);
        titleLabel_.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        titleLabel_.setColour (juce::Label::textColourId, textColour);
        titleLabel_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel_);

        switchToCyclicButton_.setButtonText ("Cyclic");
        switchToCyclicButton_.setTooltip ("Show cyclic expression controls for this phrase");
        switchToCyclicButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
        switchToCyclicButton_.setColour (juce::TextButton::textColourOffId, textColour);
        switchToCyclicButton_.onClick = [this] { owner_.showCyclicExpressionControlView(); };
        addAndMakeVisible (switchToCyclicButton_);

        storedLevelLabel_.setFont (juce::FontOptions { 11.0f });
        storedLevelLabel_.setColour (juce::Label::textColourId, mutedTextColour);
        storedLevelLabel_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (storedLevelLabel_);

        configureStageLabel (attackStageLabel_, "Attack", expressionPhraseColour);
        configureStageLabel (decayStageLabel_, "Decay / Sustain", expressionCurveColour);
        configureStageLabel (releaseStageLabel_, "Release", expressionWarningColour);

        configureSliderControl (attackTime_, "Attack Time", expressionPhraseColour);
        configureSliderControl (attackStart_, "Attack Start", expressionPhraseColour);
        configureComboControl (attackCurve_, "Attack Curve", expressionPhraseColour);

        configureSliderControl (decayTime_, "Decay Time", expressionCurveColour);
        configureSliderControl (peakLevel_, "Peak", expressionCurveColour);
        configureSliderControl (sustainLevel_, "Sustain", expressionCurveColour);
        configureComboControl (decayCurve_, "Decay Curve", expressionCurveColour);

        configureSliderControl (releaseTime_, "Release Time", expressionWarningColour);
        configureSliderControl (releaseEnd_, "Release End", expressionWarningColour);
        configureComboControl (releaseCurve_, "Release Curve", expressionWarningColour);
    }

    void refresh()
    {
        refreshing_ = true;
        const auto* envelope = owner_.selectedPhraseEnvelope();
        const auto* lane = owner_.selectedExpressionLane();
        const auto hasEnvelope = envelope != nullptr && lane != nullptr && owner_.shouldShowPhraseEnvelopeControls();
        setVisible (hasEnvelope);
        if (! hasEnvelope)
        {
            refreshing_ = false;
            return;
        }

        const auto phraseBeats = ticksToBeats (envelope->phraseRegion().duration());
        const auto minimum = core::sequencing::expressionLaneMinimumValue (lane->polarity());
        const auto maximum = core::sequencing::expressionLaneMaximumValue (lane->polarity());

        titleLabel_.setText ("Phrase Envelope", juce::dontSendNotification);
        storedLevelLabel_.setText ("Stored " + juce::String (envelope->storedLevel(), 2), juce::dontSendNotification);
        switchToCyclicButton_.setVisible (owner_.expressionControlSwitchAvailable());

        configureTimeRange (attackTime_, phraseBeats);
        configureLevelRange (attackStart_, minimum, maximum);
        attackTime_.slider.setValue (ticksToBeats (envelope->attackStage().duration), juce::dontSendNotification);
        attackStart_.slider.setValue (envelope->attackStage().startLevel, juce::dontSendNotification);
        attackCurve_.combo.setSelectedId (curveId (envelope->attackStage().curveShape), juce::dontSendNotification);

        const auto hasDecay = envelope->decayStage().has_value();
        decayStageLabel_.setVisible (hasDecay);
        setControlVisible (decayTime_, hasDecay);
        setControlVisible (peakLevel_, hasDecay);
        setControlVisible (sustainLevel_, hasDecay);
        setControlVisible (decayCurve_, hasDecay);
        if (hasDecay)
        {
            configureTimeRange (decayTime_, phraseBeats);
            configureLevelRange (peakLevel_, minimum, maximum);
            configureLevelRange (sustainLevel_, minimum, maximum);
            decayTime_.slider.setValue (ticksToBeats (envelope->decayStage()->duration), juce::dontSendNotification);
            peakLevel_.slider.setValue (envelope->peakLevel().value_or (envelope->decayStage()->startLevel), juce::dontSendNotification);
            sustainLevel_.slider.setValue (envelope->sustainLevel().value_or (envelope->decayStage()->endLevel), juce::dontSendNotification);
            decayCurve_.combo.setSelectedId (curveId (envelope->decayStage()->curveShape), juce::dontSendNotification);
        }

        const auto hasRelease = envelope->releaseStage().has_value();
        releaseStageLabel_.setVisible (hasRelease);
        setControlVisible (releaseTime_, hasRelease);
        setControlVisible (releaseEnd_, hasRelease);
        setControlVisible (releaseCurve_, hasRelease);
        if (hasRelease)
        {
            configureTimeRange (releaseTime_, phraseBeats);
            configureLevelRange (releaseEnd_, minimum, maximum);
            releaseTime_.slider.setValue (ticksToBeats (envelope->releaseStage()->duration), juce::dontSendNotification);
            releaseEnd_.slider.setValue (envelope->releaseStage()->endLevel, juce::dontSendNotification);
            releaseCurve_.combo.setSelectedId (curveId (envelope->releaseStage()->curveShape), juce::dontSendNotification);
        }

        refreshing_ = false;
        repaint();
    }

    bool debugSetAttackStartGesture (const std::vector<double>& values)
    {
        if (values.empty() || owner_.selectedPhraseEnvelope() == nullptr)
            return false;

        owner_.beginPhraseEnvelopeGesture();
        for (const auto value : values)
        {
            attackStart_.slider.setValue (value, juce::dontSendNotification);
            applySliderMutation (attackStart_, false);
        }
        return owner_.commitPhraseEnvelopeGesture();
    }

    bool debugDecayControlsVisible() const
    {
        return decayTime_.slider.isVisible() || peakLevel_.slider.isVisible() || sustainLevel_.slider.isVisible() || decayCurve_.combo.isVisible();
    }

    bool debugReleaseControlsVisible() const
    {
        return releaseTime_.slider.isVisible() || releaseEnd_.slider.isVisible() || releaseCurve_.combo.isVisible();
    }

    void paint (juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (surfaceColour.withAlpha (0.94f));
        graphics.fillRoundedRectangle (bounds, 5.0f);
        graphics.setColour (gridColour.withAlpha (0.85f));
        graphics.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        const auto stageArea = getLocalBounds().reduced (10, 8).withTrimmedTop (28);
        const auto columns = stageColumns (stageArea);
        for (const auto& column : columns)
        {
            graphics.setColour (column.colour.withAlpha (0.10f));
            graphics.fillRoundedRectangle (column.bounds.toFloat(), 4.0f);
            graphics.setColour (column.colour.withAlpha (0.36f));
            graphics.drawRoundedRectangle (column.bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (10, 8);
        auto header = bounds.removeFromTop (20);
        if (switchToCyclicButton_.isVisible())
        {
            switchToCyclicButton_.setBounds (header.removeFromRight (78));
            header.removeFromRight (6);
        }
        titleLabel_.setBounds (header.removeFromLeft (140));
        storedLevelLabel_.setBounds (header);
        bounds.removeFromTop (6);

        const auto columns = stageColumns (bounds);
        auto columnIndex = std::size_t {};

        if (columnIndex < columns.size())
        {
            auto attackColumn = columns[columnIndex++].bounds.reduced (8, 6);
            attackStageLabel_.setBounds (attackColumn.removeFromTop (18));
            attackColumn.removeFromTop (4);
            layoutSliderControl (attackTime_, attackColumn);
            layoutSliderControl (attackStart_, attackColumn);
            layoutComboControl (attackCurve_, attackColumn);
        }

        if (decayStageLabel_.isVisible() && columnIndex < columns.size())
        {
            auto decayColumn = columns[columnIndex++].bounds.reduced (8, 6);
            decayStageLabel_.setBounds (decayColumn.removeFromTop (18));
            decayColumn.removeFromTop (4);
            layoutSliderControl (decayTime_, decayColumn);
            layoutSliderControl (peakLevel_, decayColumn);
            layoutSliderControl (sustainLevel_, decayColumn);
            layoutComboControl (decayCurve_, decayColumn);
        }

        if (releaseStageLabel_.isVisible() && columnIndex < columns.size())
        {
            auto releaseColumn = columns[columnIndex++].bounds.reduced (8, 6);
            releaseStageLabel_.setBounds (releaseColumn.removeFromTop (18));
            releaseColumn.removeFromTop (4);
            layoutSliderControl (releaseTime_, releaseColumn);
            layoutSliderControl (releaseEnd_, releaseColumn);
            layoutComboControl (releaseCurve_, releaseColumn);
        }
    }

    int preferredHeight() const
    {
        const auto* envelope = owner_.selectedPhraseEnvelope();
        if (envelope == nullptr || ! owner_.shouldShowPhraseEnvelopeControls())
            return 0;

        auto rows = 3;
        if (envelope->decayStage().has_value())
            rows = std::max (rows, 4);
        if (envelope->releaseStage().has_value())
            rows = std::max (rows, 3);

        return 64 + (rows * 28);
    }

private:
    struct StageColumn
    {
        juce::Rectangle<int> bounds;
        juce::Colour colour;
    };

    struct SliderControl
    {
        juce::Label label;
        juce::Slider slider;
        bool dragActive = false;
    };

    struct ComboControl
    {
        juce::Label label;
        juce::ComboBox combo;
    };

    static double ticksToBeats (core::time::TickDuration duration) noexcept
    {
        return static_cast<double> (duration.ticks()) / static_cast<double> (core::time::ticksPerQuarterNote);
    }

    static core::time::TickDuration beatsToTicks (double beats) noexcept
    {
        return core::time::TickDuration::fromTicks (static_cast<std::int64_t> (std::llround (beats * core::time::ticksPerQuarterNote)));
    }

    static int curveId (core::sequencing::ExpressionCurveShape curve) noexcept
    {
        switch (curve)
        {
            case core::sequencing::ExpressionCurveShape::linear: return 1;
            case core::sequencing::ExpressionCurveShape::logarithmic: return 2;
            case core::sequencing::ExpressionCurveShape::exponential: return 3;
        }

        return 1;
    }

    static core::sequencing::ExpressionCurveShape curveForId (int id) noexcept
    {
        if (id == 2)
            return core::sequencing::ExpressionCurveShape::logarithmic;
        if (id == 3)
            return core::sequencing::ExpressionCurveShape::exponential;
        return core::sequencing::ExpressionCurveShape::linear;
    }

    void configureStageLabel (juce::Label& label, const juce::String& text, juce::Colour colour)
    {
        label.setText (text, juce::dontSendNotification);
        label.setTooltip (text);
        label.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    void configureSliderControl (SliderControl& control, const juce::String& label, juce::Colour colour)
    {
        control.label.setText (label, juce::dontSendNotification);
        control.label.setTooltip (label);
        control.label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        control.label.setColour (juce::Label::textColourId, colour);
        control.label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (control.label);

        control.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        control.slider.setTooltip (label);
        control.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        control.slider.setColour (juce::Slider::trackColourId, colour.withAlpha (0.48f));
        control.slider.setColour (juce::Slider::thumbColourId, textColour);
        control.slider.setColour (juce::Slider::textBoxTextColourId, textColour);
        control.slider.setColour (juce::Slider::textBoxBackgroundColourId, buttonColour);
        auto* rawControl = &control;
        control.slider.onDragStart = [this, rawControl]
        {
            rawControl->dragActive = true;
            owner_.beginPhraseEnvelopeGesture();
        };
        control.slider.onDragEnd = [this, rawControl]
        {
            rawControl->dragActive = false;
            owner_.commitPhraseEnvelopeGesture();
        };
        control.slider.onValueChange = [this, rawControl]
        {
            if (refreshing_)
                return;

            applySliderMutation (*rawControl, ! rawControl->dragActive);
        };
        addAndMakeVisible (control.slider);
    }

    void configureComboControl (ComboControl& control, const juce::String& label, juce::Colour colour)
    {
        control.label.setText (label, juce::dontSendNotification);
        control.label.setTooltip (label);
        control.label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        control.label.setColour (juce::Label::textColourId, colour);
        control.label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (control.label);

        control.combo.addItem ("Linear", 1);
        control.combo.addItem ("Log", 2);
        control.combo.addItem ("Exp", 3);
        control.combo.setTooltip (label);
        auto* rawControl = &control;
        control.combo.onChange = [this, rawControl]
        {
            if (! refreshing_)
                applyComboMutation (*rawControl);
        };
        addAndMakeVisible (control.combo);
    }

    static void configureTimeRange (SliderControl& control, double phraseBeats)
    {
        control.slider.setRange (0.0, std::max (0.0, phraseBeats), 1.0 / 64.0);
        control.slider.setNumDecimalPlacesToDisplay (2);
    }

    static void configureLevelRange (SliderControl& control, double minimum, double maximum)
    {
        control.slider.setRange (minimum, maximum, 0.01);
        control.slider.setNumDecimalPlacesToDisplay (2);
    }

    static void setControlVisible (SliderControl& control, bool visible)
    {
        control.label.setVisible (visible);
        control.slider.setVisible (visible);
    }

    static void setControlVisible (ComboControl& control, bool visible)
    {
        control.label.setVisible (visible);
        control.combo.setVisible (visible);
    }

    std::vector<StageColumn> stageColumns (juce::Rectangle<int> bounds) const
    {
        std::vector<StageColumn> columns;
        columns.push_back ({ {}, expressionPhraseColour });
        if (decayStageLabel_.isVisible())
            columns.push_back ({ {}, expressionCurveColour });
        if (releaseStageLabel_.isVisible())
            columns.push_back ({ {}, expressionWarningColour });

        if (columns.empty())
            return columns;

        constexpr auto gap = 8;
        const auto totalGap = gap * static_cast<int> (columns.size() - 1);
        const auto columnWidth = std::max (80, (bounds.getWidth() - totalGap) / static_cast<int> (columns.size()));
        auto x = bounds.getX();
        for (auto index = std::size_t {}; index < columns.size(); ++index)
        {
            const auto isLast = index + 1 == columns.size();
            const auto width = isLast ? bounds.getRight() - x : columnWidth;
            columns[index].bounds = juce::Rectangle<int> { x, bounds.getY(), std::max (0, width), bounds.getHeight() };
            x += columnWidth + gap;
        }

        return columns;
    }

    void applySliderMutation (SliderControl& control, bool commitToUndoStack)
    {
        owner_.mutateSelectedPhraseEnvelope ([this, &control] (auto& envelope, auto polarity) {
            try
            {
                if (&control == &attackTime_)
                {
                    auto attack = envelope.attackStage();
                    const auto available = envelope.phraseRegion().duration()
                        - (envelope.decayStage().has_value() ? envelope.decayStage()->duration : core::time::TickDuration {})
                        - (envelope.releaseStage().has_value() ? envelope.releaseStage()->duration : core::time::TickDuration {});
                    attack.duration = core::time::TickDuration::fromTicks (
                        std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, available.ticks()));
                    envelope.setAttackStage (attack);
                    return true;
                }

                if (&control == &attackStart_)
                {
                    auto attack = envelope.attackStage();
                    attack.startLevel = control.slider.getValue();
                    envelope.setAttackStage (attack);
                    return true;
                }

                if (&control == &decayTime_ && envelope.decayStage().has_value())
                {
                    auto decay = *envelope.decayStage();
                    const auto available = envelope.phraseRegion().duration() - envelope.attackStage().duration
                        - (envelope.releaseStage().has_value() ? envelope.releaseStage()->duration : core::time::TickDuration {});
                    decay.duration = core::time::TickDuration::fromTicks (
                        std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, available.ticks()));
                    envelope.setDecayStage (decay);
                    return true;
                }

                if (&control == &peakLevel_ && envelope.decayStage().has_value())
                {
                    const auto value = control.slider.getValue();
                    envelope.setPeakLevel (value, polarity);
                    auto attack = envelope.attackStage();
                    attack.endLevel = value;
                    envelope.setAttackStage (attack);
                    auto decay = *envelope.decayStage();
                    decay.startLevel = value;
                    envelope.setDecayStage (decay);
                    return true;
                }

                if (&control == &sustainLevel_ && envelope.decayStage().has_value())
                {
                    const auto value = control.slider.getValue();
                    envelope.setSustainLevel (value, polarity);
                    auto decay = *envelope.decayStage();
                    decay.endLevel = value;
                    envelope.setDecayStage (decay);
                    if (envelope.releaseStage().has_value())
                    {
                        auto release = *envelope.releaseStage();
                        release.startLevel = value;
                        envelope.setReleaseStage (release);
                    }
                    return true;
                }

                if (&control == &releaseTime_ && envelope.releaseStage().has_value())
                {
                    auto release = *envelope.releaseStage();
                    const auto available = envelope.phraseRegion().duration() - envelope.attackStage().duration
                        - (envelope.decayStage().has_value() ? envelope.decayStage()->duration : core::time::TickDuration {});
                    release.duration = core::time::TickDuration::fromTicks (
                        std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, available.ticks()));
                    envelope.setReleaseStage (release);
                    return true;
                }

                if (&control == &releaseEnd_ && envelope.releaseStage().has_value())
                {
                    auto release = *envelope.releaseStage();
                    release.endLevel = control.slider.getValue();
                    envelope.setReleaseStage (release);
                    return true;
                }
            }
            catch (...)
            {
                return false;
            }

            return false;
        }, commitToUndoStack);
    }

    void applyComboMutation (ComboControl& control)
    {
        owner_.mutateSelectedPhraseEnvelope ([this, &control] (auto& envelope, auto) {
            if (&control == &attackCurve_)
            {
                auto attack = envelope.attackStage();
                attack.curveShape = curveForId (control.combo.getSelectedId());
                envelope.setAttackStage (attack);
                return true;
            }

            if (&control == &decayCurve_ && envelope.decayStage().has_value())
            {
                auto decay = *envelope.decayStage();
                decay.curveShape = curveForId (control.combo.getSelectedId());
                envelope.setDecayStage (decay);
                return true;
            }

            if (&control == &releaseCurve_ && envelope.releaseStage().has_value())
            {
                auto release = *envelope.releaseStage();
                release.curveShape = curveForId (control.combo.getSelectedId());
                envelope.setReleaseStage (release);
                return true;
            }

            return false;
        }, true);
    }

    static void layoutSliderControl (SliderControl& control, juce::Rectangle<int>& bounds)
    {
        if (! control.slider.isVisible())
            return;

        auto row = bounds.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (78));
        control.slider.setBounds (row);
        bounds.removeFromTop (4);
    }

    static void layoutComboControl (ComboControl& control, juce::Rectangle<int>& bounds)
    {
        if (! control.combo.isVisible())
            return;

        auto row = bounds.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (78));
        control.combo.setBounds (row);
        bounds.removeFromTop (4);
    }

    PianoRollComponent& owner_;
    bool refreshing_ = false;
    juce::Label titleLabel_;
    juce::Label storedLevelLabel_;
    juce::TextButton switchToCyclicButton_;
    juce::Label attackStageLabel_;
    juce::Label decayStageLabel_;
    juce::Label releaseStageLabel_;
    SliderControl attackTime_;
    SliderControl attackStart_;
    ComboControl attackCurve_;
    SliderControl decayTime_;
    SliderControl peakLevel_;
    SliderControl sustainLevel_;
    ComboControl decayCurve_;
    SliderControl releaseTime_;
    SliderControl releaseEnd_;
    ComboControl releaseCurve_;
};

class PianoRollComponent::CyclicExpressionControlPanel final : public juce::Component
{
public:
    explicit CyclicExpressionControlPanel (PianoRollComponent& owner)
        : owner_ (owner)
    {
        titleLabel_.setText ("Cyclic Expression", juce::dontSendNotification);
        titleLabel_.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        titleLabel_.setColour (juce::Label::textColourId, textColour);
        titleLabel_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel_);

        switchToEnvelopeButton_.setButtonText ("Envelope");
        switchToEnvelopeButton_.setTooltip ("Show envelope controls for this phrase");
        switchToEnvelopeButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
        switchToEnvelopeButton_.setColour (juce::TextButton::textColourOffId, textColour);
        switchToEnvelopeButton_.onClick = [this] { owner_.showPhraseEnvelopeControlView(); };
        addAndMakeVisible (switchToEnvelopeButton_);

        summaryLabel_.setFont (juce::FontOptions { 11.0f });
        summaryLabel_.setColour (juce::Label::textColourId, mutedTextColour);
        summaryLabel_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (summaryLabel_);

        configureSectionLabel (timeLabel_, "Time", expressionPhraseColour);
        configureSectionLabel (oscillatorLabel_, "Oscillator", expressionCyclicColour);
        configureSectionLabel (shapeLabel_, "Shape", expressionCurveColour);

        configureSliderControl (attackTime_, "Attack Time", expressionPhraseColour);
        configureSliderControl (releaseTime_, "Release Time", expressionWarningColour);
        configureSliderControl (maxAmplitude_, "Max Amp", expressionCyclicColour);
        configureSliderControl (phase_, "Phase", expressionCyclicColour);

        configureComboControl (frequency_, "Frequency", expressionCyclicColour);
        configureComboControl (waveShape_, "Wave", expressionCurveColour);
        configureComboControl (blendMode_, "Blend", expressionCurveColour);
        configureComboControl (polarityMode_, "Polarity", expressionCurveColour);

        waveShape_.combo.addItem ("Sine", 1);
        waveShape_.combo.addItem ("Triangle", 2);
        waveShape_.combo.addItem ("Ramp Up", 3);
        waveShape_.combo.addItem ("Ramp Down", 4);
        waveShape_.combo.addItem ("Square", 5);

        blendMode_.combo.addItem ("Additive", 1);
        blendMode_.combo.addItem ("Multiply", 2);

        polarityMode_.combo.addItem ("Positive", 1);
        polarityMode_.combo.addItem ("Half-Wave", 2);
    }

    void refresh()
    {
        refreshing_ = true;
        const auto* cyclic = owner_.selectedCyclicExpression();
        const auto* vibrato = owner_.selectedVibratoExpression();
        const auto editingVibrato = cyclic == nullptr && vibrato != nullptr;
        const auto hasCyclic = (cyclic != nullptr || vibrato != nullptr) && owner_.shouldShowCyclicExpressionControls();
        setVisible (hasCyclic);
        if (! hasCyclic)
        {
            refreshing_ = false;
            return;
        }

        const auto phraseDuration = editingVibrato ? vibrato->phraseRegion().duration() : cyclic->phraseRegion().duration();
        const auto attackDuration = editingVibrato ? vibrato->attackTime() : cyclic->attackTime();
        const auto releaseDuration = editingVibrato ? vibrato->releaseTime() : cyclic->releaseTime();
        const auto amplitude = editingVibrato ? vibrato->amplitudeSemitones() : cyclic->maxAmplitude();
        const auto phase = editingVibrato ? vibrato->phase() : cyclic->phase();
        const auto& frequencyDivisionId = editingVibrato ? vibrato->frequencyDivisionId() : cyclic->frequencyDivisionId();
        const auto waveShape = editingVibrato ? vibrato->waveShape() : cyclic->waveShape();
        const auto phraseBeats = ticksToBeats (phraseDuration);

        titleLabel_.setText (editingVibrato ? "Pitch Vibrato" : "Cyclic Expression", juce::dontSendNotification);
        summaryLabel_.setText (juce::String (editingVibrato ? "Depth " : "Amp ") + juce::String (amplitude, 2)
                                   + (editingVibrato ? " st / " : " / ")
                                   + juce::String (frequencyDivisionId),
                               juce::dontSendNotification);
        switchToEnvelopeButton_.setVisible (owner_.expressionControlSwitchAvailable());
        maxAmplitude_.label.setText (editingVibrato ? "Depth" : "Max Amp", juce::dontSendNotification);
        maxAmplitude_.label.setTooltip (editingVibrato ? "Vibrato depth in semitones" : "Maximum cyclic amplitude");
        maxAmplitude_.slider.setTooltip (maxAmplitude_.label.getTooltip());
        setComboControlVisible (blendMode_, ! editingVibrato);
        setComboControlVisible (polarityMode_, ! editingVibrato);

        configureTimeRange (attackTime_, phraseBeats);
        configureTimeRange (releaseTime_, phraseBeats);
        if (editingVibrato)
            configureVibratoDepthRange (maxAmplitude_);
        else
            configureUnipolarRange (maxAmplitude_);
        configurePhaseRange (phase_);

        attackTime_.slider.setValue (ticksToBeats (attackDuration), juce::dontSendNotification);
        releaseTime_.slider.setValue (ticksToBeats (releaseDuration), juce::dontSendNotification);
        maxAmplitude_.slider.setValue (amplitude, juce::dontSendNotification);
        phase_.slider.setValue (phase, juce::dontSendNotification);

        refreshFrequencyCombo (frequencyDivisionId);
        waveShape_.combo.setSelectedId (waveShapeId (waveShape), juce::dontSendNotification);
        if (! editingVibrato)
        {
            blendMode_.combo.setSelectedId (cyclic->blendMode() == core::sequencing::CyclicBlendMode::multiplicative ? 2 : 1,
                                            juce::dontSendNotification);
            polarityMode_.combo.setSelectedId (cyclic->wavePolarityMode() == core::sequencing::CyclicWavePolarityMode::halfWaveRectified ? 2 : 1,
                                               juce::dontSendNotification);
        }

        refreshing_ = false;
        repaint();
    }

    int preferredHeight() const
    {
        return owner_.shouldShowCyclicExpressionControls() ? 154 : 0;
    }

    bool debugControlsVisible() const
    {
        return isVisible()
            && attackTime_.slider.isVisible()
            && releaseTime_.slider.isVisible()
            && maxAmplitude_.slider.isVisible()
            && frequency_.combo.isVisible()
            && waveShape_.combo.isVisible();
    }

    void paint (juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        graphics.setColour (surfaceColour.withAlpha (0.94f));
        graphics.fillRoundedRectangle (bounds, 5.0f);
        graphics.setColour (gridColour.withAlpha (0.85f));
        graphics.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        const auto sectionArea = getLocalBounds().reduced (10, 8).withTrimmedTop (28);
        const auto columns = sectionColumns (sectionArea);
        for (const auto& column : columns)
        {
            graphics.setColour (column.colour.withAlpha (0.10f));
            graphics.fillRoundedRectangle (column.bounds.toFloat(), 4.0f);
            graphics.setColour (column.colour.withAlpha (0.36f));
            graphics.drawRoundedRectangle (column.bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (10, 8);
        auto header = bounds.removeFromTop (20);
        if (switchToEnvelopeButton_.isVisible())
        {
            switchToEnvelopeButton_.setBounds (header.removeFromRight (86));
            header.removeFromRight (6);
        }
        titleLabel_.setBounds (header.removeFromLeft (150));
        summaryLabel_.setBounds (header);
        bounds.removeFromTop (6);

        const auto columns = sectionColumns (bounds);
        if (columns.size() < 3)
            return;

        auto timeColumn = columns[0].bounds.reduced (8, 6);
        timeLabel_.setBounds (timeColumn.removeFromTop (18));
        timeColumn.removeFromTop (4);
        layoutSliderControl (attackTime_, timeColumn);
        layoutSliderControl (releaseTime_, timeColumn);

        auto oscillatorColumn = columns[1].bounds.reduced (8, 6);
        oscillatorLabel_.setBounds (oscillatorColumn.removeFromTop (18));
        oscillatorColumn.removeFromTop (4);
        layoutSliderControl (maxAmplitude_, oscillatorColumn);
        layoutSliderControl (phase_, oscillatorColumn);
        layoutComboControl (frequency_, oscillatorColumn);

        auto shapeColumn = columns[2].bounds.reduced (8, 6);
        shapeLabel_.setBounds (shapeColumn.removeFromTop (18));
        shapeColumn.removeFromTop (4);
        layoutComboControl (waveShape_, shapeColumn);
        layoutComboControl (blendMode_, shapeColumn);
        layoutComboControl (polarityMode_, shapeColumn);
    }

private:
    struct SectionColumn
    {
        juce::Rectangle<int> bounds;
        juce::Colour colour;
    };

    struct SliderControl
    {
        juce::Label label;
        juce::Slider slider;
        bool dragActive = false;
    };

    struct ComboControl
    {
        juce::Label label;
        juce::ComboBox combo;
    };

    static double ticksToBeats (core::time::TickDuration duration) noexcept
    {
        return static_cast<double> (duration.ticks()) / static_cast<double> (core::time::ticksPerQuarterNote);
    }

    static core::time::TickDuration beatsToTicks (double beats) noexcept
    {
        return core::time::TickDuration::fromTicks (static_cast<std::int64_t> (std::llround (beats * core::time::ticksPerQuarterNote)));
    }

    static int waveShapeId (core::sequencing::CyclicWaveShape shape) noexcept
    {
        switch (shape)
        {
            case core::sequencing::CyclicWaveShape::sine: return 1;
            case core::sequencing::CyclicWaveShape::triangle: return 2;
            case core::sequencing::CyclicWaveShape::rampUp: return 3;
            case core::sequencing::CyclicWaveShape::rampDown: return 4;
            case core::sequencing::CyclicWaveShape::square: return 5;
        }

        return 1;
    }

    static core::sequencing::CyclicWaveShape waveShapeForId (int id) noexcept
    {
        switch (id)
        {
            case 2: return core::sequencing::CyclicWaveShape::triangle;
            case 3: return core::sequencing::CyclicWaveShape::rampUp;
            case 4: return core::sequencing::CyclicWaveShape::rampDown;
            case 5: return core::sequencing::CyclicWaveShape::square;
            default: return core::sequencing::CyclicWaveShape::sine;
        }
    }

    void configureSectionLabel (juce::Label& label, const juce::String& text, juce::Colour colour)
    {
        label.setText (text, juce::dontSendNotification);
        label.setTooltip (text);
        label.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    void configureSliderControl (SliderControl& control, const juce::String& label, juce::Colour colour)
    {
        control.label.setText (label, juce::dontSendNotification);
        control.label.setTooltip (label);
        control.label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        control.label.setColour (juce::Label::textColourId, colour);
        control.label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (control.label);

        control.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        control.slider.setTooltip (label);
        control.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        control.slider.setColour (juce::Slider::trackColourId, colour.withAlpha (0.48f));
        control.slider.setColour (juce::Slider::thumbColourId, textColour);
        control.slider.setColour (juce::Slider::textBoxTextColourId, textColour);
        control.slider.setColour (juce::Slider::textBoxBackgroundColourId, buttonColour);
        auto* rawControl = &control;
        control.slider.onDragStart = [this, rawControl]
        {
            rawControl->dragActive = true;
            owner_.beginPhraseEnvelopeGesture();
        };
        control.slider.onDragEnd = [this, rawControl]
        {
            rawControl->dragActive = false;
            owner_.commitPhraseEnvelopeGesture();
        };
        control.slider.onValueChange = [this, rawControl]
        {
            if (refreshing_)
                return;

            applySliderMutation (*rawControl, ! rawControl->dragActive);
        };
        addAndMakeVisible (control.slider);
    }

    void configureComboControl (ComboControl& control, const juce::String& label, juce::Colour colour)
    {
        control.label.setText (label, juce::dontSendNotification);
        control.label.setTooltip (label);
        control.label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
        control.label.setColour (juce::Label::textColourId, colour);
        control.label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (control.label);

        control.combo.setTooltip (label);
        auto* rawControl = &control;
        control.combo.onChange = [this, rawControl]
        {
            if (! refreshing_)
                applyComboMutation (*rawControl);
        };
        addAndMakeVisible (control.combo);
    }

    void refreshFrequencyCombo (const std::string& selectedId)
    {
        frequencyIds_.clear();
        frequency_.combo.clear (juce::dontSendNotification);

        const auto subdivisions = core::sequencing::availableArpeggioSubdivisions (owner_.appServices_.project().rhythmSettings());
        auto selectedComboId = 0;
        for (auto index = std::size_t {}; index < subdivisions.size(); ++index)
        {
            const auto comboId = static_cast<int> (index + 1);
            frequencyIds_.push_back (subdivisions[index].id);
            frequency_.combo.addItem (subdivisions[index].displayName, comboId);
            if (subdivisions[index].id == selectedId)
                selectedComboId = comboId;
        }

        if (selectedComboId == 0 && ! frequencyIds_.empty())
            selectedComboId = 1;
        frequency_.combo.setSelectedId (selectedComboId, juce::dontSendNotification);
    }

    static void configureTimeRange (SliderControl& control, double phraseBeats)
    {
        control.slider.setRange (0.0, std::max (0.0, phraseBeats), 1.0 / 64.0);
        control.slider.setNumDecimalPlacesToDisplay (2);
    }

    static void configureUnipolarRange (SliderControl& control)
    {
        control.slider.setRange (0.0, 1.0, 0.01);
        control.slider.setNumDecimalPlacesToDisplay (2);
    }

    static void configureVibratoDepthRange (SliderControl& control)
    {
        control.slider.setRange (0.0, 2.0, 0.01);
        control.slider.setNumDecimalPlacesToDisplay (2);
    }

    static void configurePhaseRange (SliderControl& control)
    {
        control.slider.setRange (0.0, 1.0, 0.01);
        control.slider.setNumDecimalPlacesToDisplay (2);
    }

    static void setComboControlVisible (ComboControl& control, bool visible)
    {
        control.label.setVisible (visible);
        control.combo.setVisible (visible);
    }

    std::vector<SectionColumn> sectionColumns (juce::Rectangle<int> bounds) const
    {
        std::vector<SectionColumn> columns {
            { {}, expressionPhraseColour },
            { {}, expressionCyclicColour },
            { {}, expressionCurveColour }
        };

        constexpr auto gap = 8;
        const auto totalGap = gap * static_cast<int> (columns.size() - 1);
        const auto columnWidth = std::max (80, (bounds.getWidth() - totalGap) / static_cast<int> (columns.size()));
        auto x = bounds.getX();
        for (auto index = std::size_t {}; index < columns.size(); ++index)
        {
            const auto isLast = index + 1 == columns.size();
            const auto width = isLast ? bounds.getRight() - x : columnWidth;
            columns[index].bounds = juce::Rectangle<int> { x, bounds.getY(), std::max (0, width), bounds.getHeight() };
            x += columnWidth + gap;
        }

        return columns;
    }

    void applySliderMutation (SliderControl& control, bool commitToUndoStack)
    {
        if (owner_.selectedCyclicExpression() == nullptr && owner_.selectedVibratoExpression() != nullptr)
        {
            owner_.mutateSelectedVibratoExpression ([this, &control] (auto& vibrato) {
                const auto phrase = vibrato.phraseRegion().duration();
                if (&control == &attackTime_)
                {
                    const auto availableTicks = std::max<std::int64_t> (0, phrase.ticks() - vibrato.releaseTime().ticks());
                    vibrato.setAttackTime (core::time::TickDuration::fromTicks (
                        std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, availableTicks)));
                    return true;
                }

                if (&control == &releaseTime_)
                {
                    const auto availableTicks = std::max<std::int64_t> (0, phrase.ticks() - vibrato.attackTime().ticks());
                    vibrato.setReleaseTime (core::time::TickDuration::fromTicks (
                        std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, availableTicks)));
                    return true;
                }

                if (&control == &maxAmplitude_)
                {
                    vibrato.setAmplitudeSemitones (control.slider.getValue());
                    return true;
                }

                if (&control == &phase_)
                {
                    vibrato.setPhase (control.slider.getValue());
                    return true;
                }

                return false;
            }, commitToUndoStack);
            return;
        }

        owner_.mutateSelectedCyclicExpression ([this, &control] (auto& cyclic) {
            const auto phrase = cyclic.phraseRegion().duration();
            if (&control == &attackTime_)
            {
                const auto availableTicks = std::max<std::int64_t> (0, phrase.ticks() - cyclic.releaseTime().ticks());
                cyclic.setAttackTime (core::time::TickDuration::fromTicks (
                    std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, availableTicks)));
                return true;
            }

            if (&control == &releaseTime_)
            {
                const auto availableTicks = std::max<std::int64_t> (0, phrase.ticks() - cyclic.attackTime().ticks());
                cyclic.setReleaseTime (core::time::TickDuration::fromTicks (
                    std::clamp (beatsToTicks (control.slider.getValue()).ticks(), std::int64_t { 0 }, availableTicks)));
                return true;
            }

            if (&control == &maxAmplitude_)
            {
                cyclic.setMaxAmplitude (control.slider.getValue());
                return true;
            }

            if (&control == &phase_)
            {
                cyclic.setPhase (control.slider.getValue());
                return true;
            }

            return false;
        }, commitToUndoStack);
    }

    void applyComboMutation (ComboControl& control)
    {
        if (owner_.selectedCyclicExpression() == nullptr && owner_.selectedVibratoExpression() != nullptr)
        {
            owner_.mutateSelectedVibratoExpression ([this, &control] (auto& vibrato) {
                if (&control == &frequency_)
                {
                    const auto selected = frequency_.combo.getSelectedId();
                    const auto index = selected <= 0 ? std::size_t {} : static_cast<std::size_t> (selected - 1);
                    if (index >= frequencyIds_.size())
                        return false;

                    vibrato.setFrequencyDivisionId (frequencyIds_[index]);
                    return true;
                }

                if (&control == &waveShape_)
                {
                    vibrato.setWaveShape (waveShapeForId (waveShape_.combo.getSelectedId()));
                    return true;
                }

                return false;
            }, true);
            return;
        }

        owner_.mutateSelectedCyclicExpression ([this, &control] (auto& cyclic) {
            if (&control == &frequency_)
            {
                const auto selected = frequency_.combo.getSelectedId();
                const auto index = selected <= 0 ? std::size_t {} : static_cast<std::size_t> (selected - 1);
                if (index >= frequencyIds_.size())
                    return false;

                cyclic.setFrequencyDivisionId (frequencyIds_[index]);
                return true;
            }

            if (&control == &waveShape_)
            {
                cyclic.setWaveShape (waveShapeForId (waveShape_.combo.getSelectedId()));
                return true;
            }

            if (&control == &blendMode_)
            {
                cyclic.setBlendMode (blendMode_.combo.getSelectedId() == 2
                                         ? core::sequencing::CyclicBlendMode::multiplicative
                                         : core::sequencing::CyclicBlendMode::additive);
                return true;
            }

            if (&control == &polarityMode_)
            {
                cyclic.setWavePolarityMode (polarityMode_.combo.getSelectedId() == 2
                                                ? core::sequencing::CyclicWavePolarityMode::halfWaveRectified
                                                : core::sequencing::CyclicWavePolarityMode::positiveOscillator);
                return true;
            }

            return false;
        }, true);
    }

    static void layoutSliderControl (SliderControl& control, juce::Rectangle<int>& bounds)
    {
        auto row = bounds.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (78));
        control.slider.setBounds (row);
        bounds.removeFromTop (4);
    }

    static void layoutComboControl (ComboControl& control, juce::Rectangle<int>& bounds)
    {
        auto row = bounds.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (78));
        control.combo.setBounds (row);
        bounds.removeFromTop (4);
    }

    PianoRollComponent& owner_;
    bool refreshing_ = false;
    juce::Label titleLabel_;
    juce::Label summaryLabel_;
    juce::TextButton switchToEnvelopeButton_;
    juce::Label timeLabel_;
    juce::Label oscillatorLabel_;
    juce::Label shapeLabel_;
    SliderControl attackTime_;
    SliderControl releaseTime_;
    SliderControl maxAmplitude_;
    SliderControl phase_;
    ComboControl frequency_;
    ComboControl waveShape_;
    ComboControl blendMode_;
    ComboControl polarityMode_;
    std::vector<std::string> frequencyIds_;
};

class PianoRollComponent::ExpressionRoutingPanel final : public juce::Component
{
public:
    explicit ExpressionRoutingPanel (PianoRollComponent& owner)
        : owner_ (owner)
    {
        titleLabel_.setText ("Routes", juce::dontSendNotification);
        titleLabel_.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        titleLabel_.setTooltip ("Expression routes");
        titleLabel_.setColour (juce::Label::textColourId, textColour);
        titleLabel_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel_);

        destinationCombo_.setTooltip ("Expression route destination");
        addAndMakeVisible (destinationCombo_);

        addRouteButton_.setButtonText ("Add");
        addRouteButton_.setTooltip ("Add expression route");
        addRouteButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
        addRouteButton_.setColour (juce::TextButton::textColourOffId, textColour);
        addRouteButton_.onClick = [this] { addSelectedDestination(); };
        addAndMakeVisible (addRouteButton_);
    }

    void refresh()
    {
        const auto* lane = owner_.selectedExpressionLane();
        const auto hasLane = owner_.expressionModeEnabled_ && lane != nullptr;
        setVisible (hasLane);
        if (! hasLane)
            return;

        refreshing_ = true;
        destinations_ = owner_.destinationMetadataForSelectedTrack();
        destinationCombo_.clear (juce::dontSendNotification);
        for (std::size_t index = 0; index < destinations_.size(); ++index)
        {
            const auto& metadata = destinations_[index];
            if (! metadata.available || ! metadata.expressionTarget)
                continue;

            destinationCombo_.addItem (metadata.displayName + " - " + metadata.detailText + " - " + metadata.supportLabel,
                                       static_cast<int> (index + 1));
        }
        if (destinationCombo_.getNumItems() > 0 && destinationCombo_.getSelectedId() == 0)
            destinationCombo_.setSelectedId (1, juce::dontSendNotification);

        rebuildRouteRows (*lane);
        refreshing_ = false;
        resized();
        repaint();
    }

    bool debugAddFirstAvailableRoute()
    {
        const auto destinations = owner_.destinationMetadataForSelectedTrack();
        const auto match = std::find_if (destinations.begin(), destinations.end(), [] (const auto& metadata) {
            return metadata.available && metadata.expressionTarget;
        });
        return match != destinations.end() && owner_.addExpressionRouteToSelectedLane (*match);
    }

    bool debugAddRouteByStableId (const std::string& stableId)
    {
        const auto destinations = owner_.destinationMetadataForSelectedTrack();
        const auto match = std::find_if (destinations.begin(), destinations.end(), [&] (const auto& metadata) {
            return metadata.stableId == stableId;
        });
        return match != destinations.end() && owner_.addExpressionRouteToSelectedLane (*match);
    }

    bool debugAddSimpleOscRoute (const std::string& parameterId)
    {
        const auto destinations = owner_.destinationMetadataForSelectedTrack();
        const auto match = std::find_if (destinations.begin(), destinations.end(), [&] (const auto& metadata) {
            return metadata.destination.kind == core::sequencing::ExpressionDestinationKind::firstPartyParameter
                && metadata.destination.parameterId == parameterId;
        });
        return match != destinations.end() && owner_.addExpressionRouteToSelectedLane (*match);
    }

    bool debugSetFirstRouteRange (double outputMin, double outputMax)
    {
        const auto* lane = owner_.selectedExpressionLane();
        if (lane == nullptr || lane->routes().empty())
            return false;

        const auto routeId = lane->routes().front().id();
        return owner_.mutateExpressionRouteOnSelectedLane (routeId, [=] (auto& route) {
            route.setOutputRange (outputMin, outputMax);
            return true;
        });
    }

    bool debugToggleFirstRouteEnabled()
    {
        const auto* lane = owner_.selectedExpressionLane();
        if (lane == nullptr || lane->routes().empty())
            return false;

        const auto routeId = lane->routes().front().id();
        return owner_.mutateExpressionRouteOnSelectedLane (routeId, [] (auto& route) {
            route.setEnabled (! route.enabled());
            return true;
        });
    }

    bool debugRemoveFirstRoute()
    {
        const auto* lane = owner_.selectedExpressionLane();
        if (lane == nullptr || lane->routes().empty())
            return false;

        return owner_.removeExpressionRouteFromSelectedLane (lane->routes().front().id());
    }

    std::vector<std::string> debugRouteSupportLabels() const
    {
        std::vector<std::string> labels;
        labels.reserve (routeRows_.size());
        for (const auto& row : routeRows_)
            labels.push_back (row->supportLabel.getText().toStdString());
        return labels;
    }

    bool debugRouteControlsVisible() const
    {
        const auto* lane = owner_.selectedExpressionLane();
        if (lane == nullptr || lane->routes().empty())
            return false;

        if (routeRows_.size() != lane->routes().size())
            return false;

        const auto localBounds = getLocalBounds();
        for (const auto& row : routeRows_)
        {
            if (row->enabledButton.getWidth() <= 0
                || row->removeButton.getWidth() <= 0
                || row->minSlider.getWidth() <= 0
                || row->maxSlider.getWidth() <= 0)
            {
                return false;
            }

            if (! localBounds.contains (row->enabledButton.getBounds())
                || ! localBounds.contains (row->removeButton.getBounds())
                || ! localBounds.contains (row->minSlider.getBounds())
                || ! localBounds.contains (row->maxSlider.getBounds()))
            {
                return false;
            }
        }

        return true;
    }

    int preferredHeight() const
    {
        const auto* lane = owner_.selectedExpressionLane();
        const auto routeCount = lane == nullptr ? std::size_t {} : lane->routes().size();
        return 66 + static_cast<int> (routeCount) * 106;
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (0, 4);
        titleLabel_.setBounds (bounds.removeFromTop (20));
        auto picker = bounds.removeFromTop (26);
        addRouteButton_.setBounds (picker.removeFromRight (44));
        picker.removeFromRight (6);
        destinationCombo_.setBounds (picker);
        bounds.removeFromTop (6);

        for (auto& row : routeRows_)
        {
            auto rowBounds = bounds.removeFromTop (100);
            row->nameLabel.setBounds (rowBounds.removeFromTop (18));
            row->supportLabel.setBounds (rowBounds.removeFromTop (16));
            auto controls = rowBounds.removeFromTop (22);
            row->enabledButton.setBounds (controls.removeFromLeft (68));
            row->removeButton.setBounds (controls.removeFromRight (26));
            controls.removeFromRight (6);
            row->previewLabel.setBounds (controls);
            auto sliders = rowBounds.removeFromTop (26);
            row->minSlider.setBounds (sliders.removeFromLeft (sliders.getWidth() / 2).reduced (0, 2));
            row->maxSlider.setBounds (sliders.reduced (0, 2));
            bounds.removeFromTop (6);
        }
    }

private:
    struct RouteRow
    {
        core::sequencing::ExpressionRouteId routeId;
        juce::Label nameLabel;
        juce::ToggleButton enabledButton;
        juce::TextButton removeButton;
        juce::Slider minSlider;
        juce::Slider maxSlider;
        juce::Label previewLabel;
        juce::Label supportLabel;
    };

    void addSelectedDestination()
    {
        if (refreshing_)
            return;

        const auto selected = destinationCombo_.getSelectedId();
        if (selected <= 0)
            return;

        const auto index = static_cast<std::size_t> (selected - 1);
        if (index >= destinations_.size())
            return;

        owner_.addExpressionRouteToSelectedLane (destinations_[index]);
    }

    void rebuildRouteRows (const core::sequencing::ExpressionLane& lane)
    {
        routeRows_.clear();
        for (const auto& route : lane.routes())
        {
            auto row = std::make_unique<RouteRow>();
            row->routeId = route.id();

            const auto metadata = core::sequencing::expressionDestinationMetadata (owner_.appServices_.project(), route.destination());
            row->nameLabel.setText (metadata.displayName + " - " + metadata.detailText, juce::dontSendNotification);
            row->nameLabel.setTooltip (metadata.displayName + " - " + metadata.detailText + "\n" + metadata.supportDetailText);
            row->nameLabel.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
            row->nameLabel.setColour (juce::Label::textColourId, textColour);
            row->nameLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (row->nameLabel);

            row->supportLabel.setText (metadata.supportLabel, juce::dontSendNotification);
            row->supportLabel.setTooltip (metadata.supportDetailText);
            row->supportLabel.setFont (juce::FontOptions { 10.0f });
            row->supportLabel.setColour (juce::Label::textColourId,
                                         metadata.playbackMapped ? expressionPhraseColour
                                                                 : (metadata.plainMidiExportMapped ? expressionCurveColour : mutedTextColour));
            row->supportLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (row->supportLabel);

            row->enabledButton.setButtonText ("On");
            row->enabledButton.setTooltip ("Enable expression route");
            row->enabledButton.setToggleState (route.enabled(), juce::dontSendNotification);
            row->enabledButton.setColour (juce::ToggleButton::textColourId, textColour);
            row->enabledButton.onClick = [this, rawRow = row.get()]
            {
                owner_.mutateExpressionRouteOnSelectedLane (rawRow->routeId, [rawRow] (auto& mutableRoute) {
                    mutableRoute.setEnabled (rawRow->enabledButton.getToggleState());
                    return true;
                });
            };
            addAndMakeVisible (row->enabledButton);

            row->removeButton.setButtonText ("x");
            row->removeButton.setTooltip ("Remove expression route");
            row->removeButton.setColour (juce::TextButton::buttonColourId, buttonColour);
            row->removeButton.setColour (juce::TextButton::textColourOffId, textColour);
            row->removeButton.onClick = [this, routeId = row->routeId] { owner_.removeExpressionRouteFromSelectedLane (routeId); };
            addAndMakeVisible (row->removeButton);

            configureRouteSlider (row->minSlider, metadata, route.outputMin());
            configureRouteSlider (row->maxSlider, metadata, route.outputMax());
            row->minSlider.setTooltip ("Route output minimum");
            row->maxSlider.setTooltip ("Route output maximum");
            row->minSlider.onValueChange = [this, rawRow = row.get()]
            {
                commitRouteRange (*rawRow);
            };
            row->maxSlider.onValueChange = [this, rawRow = row.get()]
            {
                commitRouteRange (*rawRow);
            };
            addAndMakeVisible (row->minSlider);
            addAndMakeVisible (row->maxSlider);

            row->previewLabel.setText (previewText (route, lane.polarity()), juce::dontSendNotification);
            row->previewLabel.setTooltip ("Expression route output range");
            row->previewLabel.setFont (juce::FontOptions { 10.5f });
            row->previewLabel.setColour (juce::Label::textColourId, mutedTextColour);
            row->previewLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (row->previewLabel);

            routeRows_.push_back (std::move (row));
        }
    }

    void configureRouteSlider (juce::Slider& slider,
                               const core::sequencing::ExpressionDestinationMetadata& metadata,
                               double value)
    {
        const auto minimum = std::min (metadata.defaultOutputMin, metadata.defaultOutputMax);
        const auto maximum = std::max (metadata.defaultOutputMin, metadata.defaultOutputMax);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        slider.setRange (minimum, maximum, metadata.discrete ? 1.0 : 0.01);
        slider.setValue (value, juce::dontSendNotification);
        slider.setColour (juce::Slider::trackColourId, expressionCurveColour.withAlpha (0.45f));
        slider.setColour (juce::Slider::thumbColourId, textColour);
        slider.setColour (juce::Slider::textBoxTextColourId, textColour);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, buttonColour);
    }

    void commitRouteRange (RouteRow& row)
    {
        if (refreshing_)
            return;

        owner_.mutateExpressionRouteOnSelectedLane (row.routeId, [&] (auto& route) {
            route.setOutputRange (row.minSlider.getValue(), row.maxSlider.getValue());
            return true;
        });
    }

    static juce::String previewText (const core::sequencing::ExpressionRoute& route,
                                     core::sequencing::ExpressionLanePolarity polarity)
    {
        const auto low = core::sequencing::mapExpressionRouteValue (
            route,
            core::sequencing::expressionLaneMinimumValue (polarity),
            polarity);
        const auto high = core::sequencing::mapExpressionRouteValue (
            route,
            core::sequencing::expressionLaneMaximumValue (polarity),
            polarity);
        return juce::String (low, 2) + " -> " + juce::String (high, 2);
    }

    PianoRollComponent& owner_;
    bool refreshing_ = false;
    juce::Label titleLabel_;
    juce::ComboBox destinationCombo_;
    juce::TextButton addRouteButton_;
    std::vector<core::sequencing::ExpressionDestinationMetadata> destinations_;
    std::vector<std::unique_ptr<RouteRow>> routeRows_;
};

PianoRollComponent::PianoRollComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    toggleChromaticRevealCallback_ = [this] { toggleChromaticReveal(); };
    expressionSelectionChangedCallback_ = [this]
    {
        refreshExpressionLaneControls (true);
        resized();
        repaint();
    };

    expressionModeButton_.setButtonText ("Expression");
    expressionModeButton_.setClickingTogglesState (true);
    expressionModeButton_.setTooltip ("Toggle Expression Mode for the open MIDI clip");
    expressionModeButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    expressionModeButton_.setColour (juce::TextButton::buttonOnColourId, buttonOnColour);
    expressionModeButton_.setColour (juce::TextButton::textColourOffId, mutedTextColour);
    expressionModeButton_.setColour (juce::TextButton::textColourOnId, textColour);
    expressionModeButton_.onClick = [this] { toggleExpressionMode(); };
    addAndMakeVisible (expressionModeButton_);

    clipLengthLabel_.setText ("Length", juce::dontSendNotification);
    clipLengthLabel_.setTooltip ("Clip length in bars:beats");
    clipLengthLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    clipLengthLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (clipLengthLabel_);

    for (auto* editor : { &clipLengthBarsEditor_, &clipLengthBeatsEditor_ })
    {
        editor->setInputRestrictions (4, "0123456789");
        editor->setJustification (juce::Justification::centred);
        editor->setSelectAllWhenFocused (true);
        editor->setColour (juce::TextEditor::backgroundColourId, buttonColour);
        editor->setColour (juce::TextEditor::textColourId, textColour);
        editor->setColour (juce::TextEditor::outlineColourId, gridColour);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, selectedNoteColour);
        editor->onReturnKey = [this] { commitClipLengthControls(); };
        editor->onFocusLost = [this] { commitClipLengthControls(); };
        editor->onEscapeKey = [this] { refreshClipControls (true); };
        addAndMakeVisible (*editor);
    }
    clipLengthBarsEditor_.setTooltip ("Clip length bars");
    clipLengthBeatsEditor_.setTooltip ("Clip length beats");

    clipLengthSeparatorLabel_.setText (":", juce::dontSendNotification);
    clipLengthSeparatorLabel_.setTooltip ("Clip length in bars:beats");
    clipLengthSeparatorLabel_.setColour (juce::Label::textColourId, mutedTextColour);
    clipLengthSeparatorLabel_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (clipLengthSeparatorLabel_);

    clipLoopButton_.setButtonText ("Loop");
    clipLoopButton_.setTooltip ("Loop this MIDI clip when stretching it on the timeline");
    clipLoopButton_.setColour (juce::ToggleButton::textColourId, textColour);
    clipLoopButton_.onClick = [this] { commitClipLoopToggle(); };
    addAndMakeVisible (clipLoopButton_);

    gridDivisionCombo_.setTooltip ("Grid / shortest note length");
    gridDivisionCombo_.onChange = [this] { setCurrentGridFromCombo(); };
    addAndMakeVisible (gridDivisionCombo_);

    tupletSettingsButton_.setButtonText ("Tuplets");
    tupletSettingsButton_.setTooltip ("Enable tuplet grid divisions");
    tupletSettingsButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    tupletSettingsButton_.setColour (juce::TextButton::textColourOffId, textColour);
    tupletSettingsButton_.onClick = [this] { showTupletSettingsMenu(); };
    addAndMakeVisible (tupletSettingsButton_);

    showChromaticButton_.setButtonText ("Show Chromatic");
    showChromaticButton_.setTooltip ("Show all chromatic note lanes");
    showChromaticButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    showChromaticButton_.setColour (juce::TextButton::buttonOnColourId, buttonOnColour);
    showChromaticButton_.setColour (juce::TextButton::textColourOffId, mutedTextColour);
    showChromaticButton_.setColour (juce::TextButton::textColourOnId, textColour);
    showChromaticButton_.onClick = [this]
    {
        chromaticReveal_ = showChromaticButton_.getToggleState();
        refreshContentBounds();
        repaint();
    };
    addAndMakeVisible (showChromaticButton_);

    chromaticTransposeButton_.setButtonText ("Chromatic Transpose");
    chromaticTransposeButton_.setTooltip ("Transpose the clip chromatically into the current harmonic context");
    chromaticTransposeButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    chromaticTransposeButton_.setColour (juce::TextButton::textColourOffId, textColour);
    chromaticTransposeButton_.onClick = [this] { chromaticTransposeToCurrentContext(); };
    addAndMakeVisible (chromaticTransposeButton_);

    scaleDegreeTransposeButton_.setButtonText ("Scale-Degree Transpose");
    scaleDegreeTransposeButton_.setTooltip ("Transpose by scale degree using the current harmonic context");
    scaleDegreeTransposeButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    scaleDegreeTransposeButton_.setColour (juce::TextButton::textColourOffId, textColour);
    scaleDegreeTransposeButton_.onClick = [this] { scaleDegreeTransposeToCurrentContext(); };
    addAndMakeVisible (scaleDegreeTransposeButton_);

    expressionLaneHeaderLabel_.setText ("Expression Lanes", juce::dontSendNotification);
    expressionLaneHeaderLabel_.setTooltip ("Expression lanes for this MIDI clip");
    expressionLaneHeaderLabel_.setColour (juce::Label::textColourId, textColour);
    expressionLaneHeaderLabel_.setJustificationType (juce::Justification::centredLeft);
    expressionLaneHeaderLabel_.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
    addAndMakeVisible (expressionLaneHeaderLabel_);

    expressionReleaseModeButton_.setButtonText ("Release Tails");
    expressionReleaseModeButton_.setTooltip ("Show selectable first-party synth release tails");
    expressionReleaseModeButton_.setColour (juce::ToggleButton::textColourId, textColour);
    expressionReleaseModeButton_.onClick = [this] { toggleExpressionReleaseMode(); };
    addAndMakeVisible (expressionReleaseModeButton_);

    addExpressionLaneButton_.setButtonText ("+");
    addExpressionLaneButton_.setTooltip ("Create expression lane");
    addExpressionLaneButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    addExpressionLaneButton_.setColour (juce::TextButton::textColourOffId, textColour);
    addExpressionLaneButton_.onClick = [this] { createExpressionLane(); };
    addAndMakeVisible (addExpressionLaneButton_);

    expressionLaneNameEditor_.setTooltip ("Rename selected expression lane");
    expressionLaneNameEditor_.setSelectAllWhenFocused (true);
    expressionLaneNameEditor_.setColour (juce::TextEditor::backgroundColourId, buttonColour);
    expressionLaneNameEditor_.setColour (juce::TextEditor::textColourId, textColour);
    expressionLaneNameEditor_.setColour (juce::TextEditor::outlineColourId, gridColour);
    expressionLaneNameEditor_.setColour (juce::TextEditor::focusedOutlineColourId, selectedNoteColour);
    expressionLaneNameEditor_.onReturnKey = [this] { commitExpressionLaneName(); };
    expressionLaneNameEditor_.onFocusLost = [this] { commitExpressionLaneName(); };
    expressionLaneNameEditor_.onEscapeKey = [this] { refreshExpressionLaneControls (true); };
    addAndMakeVisible (expressionLaneNameEditor_);

    expressionLaneEnabledButton_.setButtonText ("Lane On");
    expressionLaneEnabledButton_.setTooltip ("Enable selected expression lane");
    expressionLaneEnabledButton_.setColour (juce::ToggleButton::textColourId, textColour);
    expressionLaneEnabledButton_.onClick = [this] { commitExpressionLaneEnabled(); };
    addAndMakeVisible (expressionLaneEnabledButton_);

    expressionLanePolarityCombo_.setTooltip ("Expression lane polarity");
    expressionLanePolarityCombo_.addItem ("Unipolar", 1);
    expressionLanePolarityCombo_.addItem ("Bipolar", 2);
    expressionLanePolarityCombo_.onChange = [this] { commitExpressionLanePolarity(); };
    addAndMakeVisible (expressionLanePolarityCombo_);

    expressionLaneList_ = std::make_unique<ExpressionLaneListComponent> (*this);
    addAndMakeVisible (*expressionLaneList_);

    phraseEnvelopePanel_ = std::make_unique<PhraseEnvelopeControlPanel> (*this);
    addAndMakeVisible (*phraseEnvelopePanel_);

    cyclicExpressionPanel_ = std::make_unique<CyclicExpressionControlPanel> (*this);
    addAndMakeVisible (*cyclicExpressionPanel_);

    expressionRoutingPanel_ = std::make_unique<ExpressionRoutingPanel> (*this);
    addAndMakeVisible (*expressionRoutingPanel_);

    content_ = new RollContentComponent {
        appServices_,
        selectedClip_,
        playheadTick_,
        chromaticReveal_,
        expressionModeEnabled_,
        expressionReleaseModeEnabled_,
        selectedExpressionLaneId_,
        selectedPhraseEnvelopeId_,
        expressionSelectionChangedCallback_,
        toggleChromaticRevealCallback_
    };
    viewport_.setViewedComponent (content_, true);
    viewport_.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport_);

    refreshGridDivisionCombo();
    refreshClipControls (true);
    refreshExpressionLaneControls (true);
}

PianoRollComponent::~PianoRollComponent() = default;

void PianoRollComponent::openClip (std::string trackId, std::string clipId)
{
    selectedClip_ = ClipSelection { std::move (trackId), std::move (clipId) };
    selectedExpressionLaneId_.reset();
    selectedPhraseEnvelopeId_.reset();
    expressionControlPrefersCyclic_ = false;
    ensureSelectedExpressionLane();
    if (content_ != nullptr)
    {
        content_->clearNoteSelection();
        content_->grabKeyboardFocus();
        content_->frameNotesInViewport (true);
    }

    refreshContentBounds();
    if (content_ != nullptr)
        content_->frameNotesInViewport (true);

    repaint();
}

void PianoRollComponent::clearClip()
{
    selectedClip_.reset();
    selectedExpressionLaneId_.reset();
    if (content_ != nullptr)
        content_->clearNoteSelection();

    refreshContentBounds();
    refreshExpressionLaneControls (true);
    repaint();
}

void PianoRollComponent::toggleExpressionMode()
{
    setExpressionModeEnabled (expressionModeButton_.getToggleState());
}

void PianoRollComponent::setExpressionModeEnabled (bool enabled)
{
    if (expressionModeEnabled_ == enabled)
    {
        expressionModeButton_.setToggleState (enabled, juce::dontSendNotification);
        return;
    }

    expressionModeEnabled_ = enabled;
    expressionModeButton_.setToggleState (expressionModeEnabled_, juce::dontSendNotification);
    ensureSelectedExpressionLane();
    refreshExpressionLaneControls (true);
    refreshContentBounds();

    if (content_ != nullptr)
        content_->grabKeyboardFocus();

    repaint();
}

bool PianoRollComponent::expressionModeEnabled() const noexcept
{
    return expressionModeEnabled_;
}

void PianoRollComponent::toggleExpressionReleaseMode()
{
    setExpressionReleaseModeEnabled (expressionReleaseModeButton_.getToggleState());
}

void PianoRollComponent::setExpressionReleaseModeEnabled (bool enabled)
{
    expressionReleaseModeEnabled_ = enabled;
    expressionReleaseModeButton_.setToggleState (expressionReleaseModeEnabled_, juce::dontSendNotification);

    if (content_ != nullptr)
        content_->repaint();
}

bool PianoRollComponent::expressionReleaseModeEnabled() const noexcept
{
    return expressionReleaseModeEnabled_;
}

std::vector<std::string> PianoRollComponent::expressionLaneIds() const
{
    std::vector<std::string> result;
    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return result;

    result.reserve (clip->expressionState().lanes().size());
    for (const auto& lane : clip->expressionState().lanes())
        result.push_back (lane.id().value);

    return result;
}

std::optional<std::string> PianoRollComponent::selectedExpressionLaneId() const
{
    if (! selectedExpressionLaneId_.has_value())
        return std::nullopt;

    return selectedExpressionLaneId_->value;
}

bool PianoRollComponent::debugCreateExpressionLane()
{
    const auto before = expressionLaneIds().size();
    createExpressionLane();
    return expressionLaneIds().size() > before;
}

bool PianoRollComponent::debugRenameSelectedExpressionLane (std::string name)
{
    const auto* lane = selectedExpressionLane();
    if (lane == nullptr || ! selectedClip_.has_value())
        return false;

    const auto laneId = lane->id();
    if (! appServices_.renameExpressionLane (selectedClip_->trackId, selectedClip_->clipId, laneId, std::move (name)))
        return false;

    refreshExpressionLaneControls (true);
    refreshContentBounds();
    repaint();
    return true;
}

bool PianoRollComponent::debugEmulateMarqueeSelectFirstReleaseGhost()
{
    return content_ != nullptr && content_->debugEmulateMarqueeSelectFirstReleaseGhost();
}

bool PianoRollComponent::debugExpressionKeyPress (char editKey, int arrowKeyCode, bool shiftDown)
{
    return content_ != nullptr && content_->debugExpressionKeyPress (editKey, arrowKeyCode, shiftDown);
}

bool PianoRollComponent::debugSelectFirstPhraseEnvelope()
{
    if (content_ == nullptr)
        return false;

    const auto selected = content_->debugSelectFirstPhraseEnvelope();
    if (selected)
    {
        refreshExpressionLaneControls (true);
        resized();
    }
    return selected;
}

bool PianoRollComponent::debugSelectExpressionLane (std::string laneId)
{
    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return false;

    const auto id = core::sequencing::ExpressionLaneId { std::move (laneId) };
    if (clip->expressionState().findLane (id) == nullptr)
        return false;

    setSelectedExpressionLane (id);
    return selectedExpressionLaneId_.has_value() && *selectedExpressionLaneId_ == id;
}

bool PianoRollComponent::debugSelectPitchExpressionLane()
{
    setSelectedExpressionLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    return selectedExpressionLaneId_.has_value();
}

std::size_t PianoRollComponent::debugPitchSlurCount() const
{
    return content_ == nullptr ? 0 : content_->debugPitchSlurCount();
}

std::optional<std::int64_t> PianoRollComponent::debugSelectedPitchSlurTimeTicks() const
{
    return content_ == nullptr ? std::nullopt : content_->debugSelectedPitchSlurTimeTicks();
}

std::vector<juce::Point<float>> PianoRollComponent::debugFirstPitchSlurTrajectoryPoints() const
{
    return content_ == nullptr ? std::vector<juce::Point<float>> {} : content_->debugFirstPitchSlurTrajectoryPoints();
}

std::size_t PianoRollComponent::debugVibratoExpressionCount() const
{
    return content_ == nullptr ? 0 : content_->debugVibratoExpressionCount();
}

bool PianoRollComponent::debugApplyFirstPitchSlurVoiceOverride()
{
    return content_ != nullptr && content_->debugApplyFirstPitchSlurVoiceOverride();
}

bool PianoRollComponent::debugApplyFirstVibratoVoiceOverride()
{
    return content_ != nullptr && content_->debugApplyFirstVibratoVoiceOverride();
}

bool PianoRollComponent::debugPhraseEnvelopeControlsVisible() const
{
    return phraseEnvelopePanel_ != nullptr && phraseEnvelopePanel_->isVisible() && ! phraseEnvelopePanel_->getBounds().isEmpty();
}

bool PianoRollComponent::debugPhraseEnvelopeDecayControlsVisible() const
{
    return phraseEnvelopePanel_ != nullptr && phraseEnvelopePanel_->debugDecayControlsVisible();
}

bool PianoRollComponent::debugPhraseEnvelopeReleaseControlsVisible() const
{
    return phraseEnvelopePanel_ != nullptr && phraseEnvelopePanel_->debugReleaseControlsVisible();
}

bool PianoRollComponent::debugCyclicExpressionControlsVisible() const
{
    return cyclicExpressionPanel_ != nullptr
        && cyclicExpressionPanel_->debugControlsVisible()
        && ! cyclicExpressionPanel_->getBounds().isEmpty();
}

std::size_t PianoRollComponent::debugCyclicExpressionWaveformCount() const
{
    return content_ == nullptr ? std::size_t {} : content_->debugCyclicWaveformCount();
}

bool PianoRollComponent::debugShowPhraseEnvelopeControls()
{
    if (! expressionControlSwitchAvailable() && selectedPhraseEnvelope() == nullptr)
        return false;

    showPhraseEnvelopeControlView();
    return shouldShowPhraseEnvelopeControls();
}

bool PianoRollComponent::debugShowCyclicExpressionControls()
{
    if (! expressionControlSwitchAvailable()
        && selectedCyclicExpression() == nullptr
        && selectedVibratoExpression() == nullptr)
    {
        return false;
    }

    showCyclicExpressionControlView();
    return shouldShowCyclicExpressionControls();
}

bool PianoRollComponent::debugSetPhraseEnvelopeAttackStartGesture (std::vector<double> values)
{
    return phraseEnvelopePanel_ != nullptr && phraseEnvelopePanel_->debugSetAttackStartGesture (values);
}

bool PianoRollComponent::debugAddFirstAvailableExpressionRoute()
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugAddFirstAvailableRoute();
}

bool PianoRollComponent::debugAddExpressionRouteByStableId (std::string stableId)
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugAddRouteByStableId (stableId);
}

bool PianoRollComponent::debugAddSimpleOscExpressionRoute (std::string parameterId)
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugAddSimpleOscRoute (parameterId);
}

bool PianoRollComponent::debugSetFirstExpressionRouteRange (double outputMin, double outputMax)
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugSetFirstRouteRange (outputMin, outputMax);
}

bool PianoRollComponent::debugToggleFirstExpressionRouteEnabled()
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugToggleFirstRouteEnabled();
}

bool PianoRollComponent::debugRemoveFirstExpressionRoute()
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugRemoveFirstRoute();
}

std::vector<std::string> PianoRollComponent::debugExpressionRouteSupportLabels() const
{
    return expressionRoutingPanel_ == nullptr ? std::vector<std::string> {} : expressionRoutingPanel_->debugRouteSupportLabels();
}

bool PianoRollComponent::debugExpressionRouteControlsVisible() const
{
    return expressionRoutingPanel_ != nullptr && expressionRoutingPanel_->debugRouteControlsVisible();
}

std::size_t PianoRollComponent::debugExpressionRouteCount() const
{
    const auto* lane = selectedExpressionLane();
    return lane == nullptr ? std::size_t {} : lane->routes().size();
}

std::size_t PianoRollComponent::debugCyclicExpressionCount() const
{
    const auto* lane = selectedExpressionLane();
    return lane == nullptr ? std::size_t {} : lane->cyclicClips().size();
}

bool PianoRollComponent::debugSelectNoteIds (std::vector<std::string> noteIds)
{
    return content_ != nullptr && content_->debugSelectNoteIds (std::move (noteIds));
}

void PianoRollComponent::setSelectedExpressionLane (core::sequencing::ExpressionLaneId laneId)
{
    selectedExpressionLaneId_ = std::move (laneId);
    selectedPhraseEnvelopeId_.reset();
    expressionControlPrefersCyclic_ = false;
    if (content_ != nullptr)
        content_->resolveExpressionObjectsForCurrentLane();

    refreshExpressionLaneControls (true);

    if (expressionLaneList_ != nullptr)
        expressionLaneList_->repaint();
}

const core::sequencing::ExpressionLane* PianoRollComponent::selectedExpressionLane() const
{
    const auto* clip = selectedMidiClip();
    if (clip == nullptr || ! selectedExpressionLaneId_.has_value())
        return nullptr;

    return clip->expressionState().findLane (*selectedExpressionLaneId_);
}

const core::sequencing::PhraseEnvelopeClip* PianoRollComponent::selectedPhraseEnvelope() const
{
    const auto* lane = selectedExpressionLane();
    if (lane == nullptr || ! selectedPhraseEnvelopeId_.has_value())
        return nullptr;

    return lane->findPhraseEnvelopeClip (*selectedPhraseEnvelopeId_);
}

const core::sequencing::CyclicExpressionClip* PianoRollComponent::selectedCyclicExpression() const
{
    return content_ == nullptr ? nullptr : content_->selectedCyclicExpression();
}

const core::sequencing::VibratoExpression* PianoRollComponent::selectedVibratoExpression() const
{
    return content_ == nullptr ? nullptr : content_->selectedVibratoExpression();
}

bool PianoRollComponent::expressionControlSwitchAvailable() const
{
    return selectedPhraseEnvelope() != nullptr && selectedCyclicExpression() != nullptr;
}

bool PianoRollComponent::shouldShowPhraseEnvelopeControls() const
{
    const auto hasEnvelope = selectedPhraseEnvelope() != nullptr;
    const auto hasCyclic = selectedCyclicExpression() != nullptr;
    return expressionModeEnabled_ && hasEnvelope && (! hasCyclic || ! expressionControlPrefersCyclic_);
}

bool PianoRollComponent::shouldShowCyclicExpressionControls() const
{
    const auto hasEnvelope = selectedPhraseEnvelope() != nullptr;
    const auto hasCyclic = selectedCyclicExpression() != nullptr || selectedVibratoExpression() != nullptr;
    return expressionModeEnabled_ && hasCyclic && (! hasEnvelope || expressionControlPrefersCyclic_);
}

void PianoRollComponent::showPhraseEnvelopeControlView()
{
    expressionControlPrefersCyclic_ = false;
    refreshExpressionLaneControls (true);
    resized();
}

void PianoRollComponent::showCyclicExpressionControlView()
{
    expressionControlPrefersCyclic_ = true;
    refreshExpressionLaneControls (true);
    resized();
}

void PianoRollComponent::setSelectedPhraseEnvelope (std::optional<core::sequencing::ExpressionClipId> envelopeId)
{
    selectedPhraseEnvelopeId_ = std::move (envelopeId);
    refreshExpressionLaneControls (true);
    if (content_ != nullptr)
        content_->repaint();
}

bool PianoRollComponent::mutateSelectedPhraseEnvelope (
    const std::function<bool (core::sequencing::PhraseEnvelopeClip&, core::sequencing::ExpressionLanePolarity)>& mutate,
    bool commitToUndoStack)
{
    if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value() || ! selectedPhraseEnvelopeId_.has_value())
        return false;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return false;

    auto expression = clip->expressionState();
    auto* lane = expression.findLane (*selectedExpressionLaneId_);
    if (lane == nullptr || lane->findPhraseEnvelopeClip (*selectedPhraseEnvelopeId_) == nullptr)
        return false;

    auto envelope = lane->removePhraseEnvelopeClip (*selectedPhraseEnvelopeId_);
    if (! mutate (envelope, lane->polarity()))
    {
        lane->addPhraseEnvelopeClip (envelope);
        return false;
    }

    const auto replacementEnvelope = envelope;
    lane->addPhraseEnvelopeClip (envelope);

    auto* mutableTrack = appServices_.project().findTrackById (selectedClip_->trackId);
    auto* mutableClip = mutableTrack == nullptr ? nullptr : mutableTrack->findClipById (selectedClip_->clipId);
    if (mutableClip == nullptr)
        return false;

    if (commitToUndoStack)
    {
        if (! appServices_.replacePhraseEnvelopeClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                *selectedExpressionLaneId_,
                *selectedPhraseEnvelopeId_,
                replacementEnvelope))
        {
            return false;
        }
    }
    else
    {
        mutableClip->setExpressionState (expression);
    }

    if (content_ != nullptr)
        content_->repaint();
    return true;
}

bool PianoRollComponent::mutateSelectedCyclicExpression (
    const std::function<bool (core::sequencing::CyclicExpressionClip&)>& mutate,
    bool commitToUndoStack)
{
    if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value())
        return false;

    const auto* selected = selectedCyclicExpression();
    if (selected == nullptr)
        return false;

    const auto selectedId = selected->id();
    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return false;

    auto expression = clip->expressionState();
    auto* lane = expression.findLane (*selectedExpressionLaneId_);
    if (lane == nullptr)
        return false;

    auto cyclic = core::sequencing::CyclicExpressionClip { selectedId, selected->sourceNoteIds(), selected->phraseRegion() };
    try
    {
        cyclic = lane->removeCyclicClip (selectedId);
    }
    catch (...)
    {
        return false;
    }

    auto mutated = false;
    try
    {
        mutated = mutate (cyclic);
    }
    catch (...)
    {
        mutated = false;
    }

    if (! mutated)
    {
        try { lane->addCyclicClip (cyclic); } catch (...) {}
        return false;
    }

    const auto replacementCyclic = cyclic;
    try
    {
        lane->addCyclicClip (cyclic);
    }
    catch (...)
    {
        return false;
    }

    auto* mutableTrack = appServices_.project().findTrackById (selectedClip_->trackId);
    auto* mutableClip = mutableTrack == nullptr ? nullptr : mutableTrack->findClipById (selectedClip_->clipId);
    if (mutableClip == nullptr)
        return false;

    if (commitToUndoStack)
    {
        if (! appServices_.replaceCyclicExpressionClip (
                selectedClip_->trackId,
                selectedClip_->clipId,
                *selectedExpressionLaneId_,
                selectedId,
                replacementCyclic))
        {
            return false;
        }
    }
    else
    {
        mutableClip->setExpressionState (expression);
    }

    refreshExpressionLaneControls (true);
    resized();
    if (content_ != nullptr)
        content_->repaint();
    return true;
}

bool PianoRollComponent::mutateSelectedVibratoExpression (
    const std::function<bool (core::sequencing::VibratoExpression&)>& mutate,
    bool commitToUndoStack)
{
    if (! selectedClip_.has_value())
        return false;

    const auto* selected = selectedVibratoExpression();
    if (selected == nullptr)
        return false;

    const auto selectedId = selected->id();
    auto vibrato = *selected;
    auto mutated = false;
    try
    {
        mutated = mutate (vibrato);
    }
    catch (...)
    {
        mutated = false;
    }

    if (! mutated)
        return false;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return false;

    const auto pitchLaneId = core::sequencing::ExpressionState::defaultPitchLaneId();
    if (commitToUndoStack)
    {
        if (! appServices_.replaceVibratoExpression (
                selectedClip_->trackId,
                selectedClip_->clipId,
                pitchLaneId,
                vibrato))
        {
            return false;
        }
    }
    else
    {
        auto expression = clip->expressionState();
        auto* lane = expression.findLane (pitchLaneId);
        auto* mutableVibrato = lane == nullptr ? nullptr : lane->findVibratoExpression (selectedId);
        if (mutableVibrato == nullptr)
            return false;

        *mutableVibrato = std::move (vibrato);

        auto* mutableTrack = appServices_.project().findTrackById (selectedClip_->trackId);
        auto* mutableClip = mutableTrack == nullptr ? nullptr : mutableTrack->findClipById (selectedClip_->clipId);
        if (mutableClip == nullptr)
            return false;

        mutableClip->setExpressionState (std::move (expression));
    }

    refreshExpressionLaneControls (true);
    resized();
    if (content_ != nullptr)
        content_->repaint();
    return true;
}

std::vector<core::sequencing::ExpressionDestinationMetadata> PianoRollComponent::destinationMetadataForSelectedTrack() const
{
    if (! selectedClip_.has_value())
        return {};

    const auto* track = appServices_.project().findTrackById (selectedClip_->trackId);
    if (track == nullptr)
        return {};

    return core::sequencing::expressionDestinationMetadataForTrack (appServices_.project(), *track);
}

bool PianoRollComponent::addExpressionRouteToSelectedLane (const core::sequencing::ExpressionDestinationMetadata& metadata)
{
    if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value())
        return false;

    const auto* lane = selectedExpressionLane();
    if (lane == nullptr)
        return false;

    auto nextIndex = lane->routes().size() + 1;
    while (true)
    {
        auto routeId = core::sequencing::ExpressionRouteId { "route-" + std::to_string (nextIndex) };
        if (lane->findRoute (routeId) == nullptr)
        {
            auto route = core::sequencing::ExpressionRoute {
                routeId,
                metadata.destination,
                metadata.defaultOutputMin,
                metadata.defaultOutputMax
            };

            const auto added = appServices_.addExpressionRoute (selectedClip_->trackId,
                                                                selectedClip_->clipId,
                                                                *selectedExpressionLaneId_,
                                                                std::move (route));
            refreshExpressionLaneControls (true);
            resized();
            if (content_ != nullptr)
                content_->repaint();
            return added;
        }

        ++nextIndex;
    }
}

bool PianoRollComponent::mutateExpressionRouteOnSelectedLane (
    const core::sequencing::ExpressionRouteId& routeId,
    const std::function<bool (core::sequencing::ExpressionRoute&)>& mutate)
{
    if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value())
        return false;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return false;

    auto expression = clip->expressionState();
    auto* lane = expression.findLane (*selectedExpressionLaneId_);
    if (lane == nullptr)
        return false;

    auto* route = lane->findRoute (routeId);
    if (route == nullptr || ! mutate (*route))
        return false;

    const auto committed = appServices_.setClipExpressionState (selectedClip_->trackId, selectedClip_->clipId, std::move (expression));
    refreshExpressionLaneControls (true);
    resized();
    if (content_ != nullptr)
        content_->repaint();
    return committed;
}

bool PianoRollComponent::removeExpressionRouteFromSelectedLane (const core::sequencing::ExpressionRouteId& routeId)
{
    if (! selectedClip_.has_value() || ! selectedExpressionLaneId_.has_value())
        return false;

    const auto removed = appServices_.removeExpressionRoute (selectedClip_->trackId,
                                                             selectedClip_->clipId,
                                                             *selectedExpressionLaneId_,
                                                             routeId);
    refreshExpressionLaneControls (true);
    resized();
    if (content_ != nullptr)
        content_->repaint();
    return removed;
}

void PianoRollComponent::beginPhraseEnvelopeGesture()
{
    if (phraseEnvelopeGestureOriginal_.has_value())
        return;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return;

    phraseEnvelopeGestureOriginal_ = clip->expressionState();
}

bool PianoRollComponent::commitPhraseEnvelopeGesture()
{
    if (! phraseEnvelopeGestureOriginal_.has_value() || ! selectedClip_.has_value())
        return false;

    auto* track = appServices_.project().findTrackById (selectedClip_->trackId);
    auto* clip = track == nullptr ? nullptr : track->findClipById (selectedClip_->clipId);
    if (clip == nullptr)
    {
        phraseEnvelopeGestureOriginal_.reset();
        return false;
    }

    auto finalExpression = clip->expressionState();
    clip->setExpressionState (*phraseEnvelopeGestureOriginal_);
    phraseEnvelopeGestureOriginal_.reset();

    const auto committed = appServices_.setClipExpressionState (selectedClip_->trackId, selectedClip_->clipId, std::move (finalExpression));
    refreshExpressionLaneControls (true);
    if (content_ != nullptr)
        content_->repaint();
    return committed;
}

void PianoRollComponent::ensureSelectedExpressionLane()
{
    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
    {
        selectedExpressionLaneId_.reset();
        return;
    }

    if (selectedExpressionLaneId_.has_value()
        && clip->expressionState().findLane (*selectedExpressionLaneId_) != nullptr)
    {
        return;
    }

    const auto& lanes = clip->expressionState().lanes();
    selectedExpressionLaneId_ = lanes.empty()
        ? std::optional<core::sequencing::ExpressionLaneId> {}
        : std::optional<core::sequencing::ExpressionLaneId> { lanes.front().id() };
}

void PianoRollComponent::refreshExpressionLaneControls (bool forceTextRefresh)
{
    refreshingExpressionLaneControls_ = true;
    ensureSelectedExpressionLane();

    const auto* lane = selectedExpressionLane();
    const auto hasLane = lane != nullptr;
    for (auto* component : { static_cast<juce::Component*> (&expressionLaneHeaderLabel_),
                             static_cast<juce::Component*> (&expressionReleaseModeButton_),
                             static_cast<juce::Component*> (&addExpressionLaneButton_),
                             static_cast<juce::Component*> (&expressionLaneNameEditor_),
                             static_cast<juce::Component*> (&expressionLaneEnabledButton_),
                             static_cast<juce::Component*> (&expressionLanePolarityCombo_),
                             static_cast<juce::Component*> (phraseEnvelopePanel_.get()),
                             static_cast<juce::Component*> (cyclicExpressionPanel_.get()),
                             static_cast<juce::Component*> (expressionRoutingPanel_.get()),
                             static_cast<juce::Component*> (expressionLaneList_.get()) })
    {
        if (component != nullptr)
            component->setVisible (expressionModeEnabled_);
    }

    const auto hasClip = selectedMidiClip() != nullptr;
    addExpressionLaneButton_.setEnabled (hasClip);
    expressionReleaseModeButton_.setEnabled (hasClip);
    expressionLaneNameEditor_.setEnabled (hasLane);
    expressionLaneEnabledButton_.setEnabled (hasLane);
    expressionLanePolarityCombo_.setEnabled (hasLane);

    if (lane == nullptr)
    {
        expressionLaneNameEditor_.setText ({}, juce::dontSendNotification);
        expressionLaneEnabledButton_.setToggleState (false, juce::dontSendNotification);
        expressionLanePolarityCombo_.setSelectedId (0, juce::dontSendNotification);
        if (phraseEnvelopePanel_ != nullptr)
            phraseEnvelopePanel_->refresh();
        if (cyclicExpressionPanel_ != nullptr)
            cyclicExpressionPanel_->refresh();
        if (expressionRoutingPanel_ != nullptr)
            expressionRoutingPanel_->refresh();
        refreshingExpressionLaneControls_ = false;
        return;
    }

    const auto editingName = expressionLaneNameEditor_.hasKeyboardFocus (true);
    if (forceTextRefresh || ! editingName)
        expressionLaneNameEditor_.setText (lane->name(), juce::dontSendNotification);

    expressionLaneEnabledButton_.setToggleState (lane->enabled(), juce::dontSendNotification);
    expressionLanePolarityCombo_.setSelectedId (lane->polarity() == core::sequencing::ExpressionLanePolarity::bipolar ? 2 : 1,
                                                juce::dontSendNotification);

    if (phraseEnvelopePanel_ != nullptr)
        phraseEnvelopePanel_->refresh();
    if (cyclicExpressionPanel_ != nullptr)
        cyclicExpressionPanel_->refresh();
    if (expressionRoutingPanel_ != nullptr)
        expressionRoutingPanel_->refresh();

    refreshingExpressionLaneControls_ = false;
}

void PianoRollComponent::createExpressionLane()
{
    if (! selectedClip_.has_value())
        return;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
        return;

    auto nextIndex = clip->expressionState().lanes().size() + 1;
    while (true)
    {
        auto id = core::sequencing::ExpressionLaneId { "expr-lane-" + std::to_string (nextIndex) };
        if (clip->expressionState().findLane (id) == nullptr)
        {
            auto lane = core::sequencing::ExpressionLane {
                id,
                "Expression " + std::to_string (nextIndex),
                core::sequencing::ExpressionLanePolarity::unipolar
            };

            if (appServices_.createExpressionLane (selectedClip_->trackId, selectedClip_->clipId, lane))
                setSelectedExpressionLane (id);

            refreshContentBounds();
            repaint();
            return;
        }

        ++nextIndex;
    }
}

void PianoRollComponent::commitExpressionLaneName()
{
    if (refreshingExpressionLaneControls_ || ! selectedClip_.has_value())
        return;

    const auto* lane = selectedExpressionLane();
    if (lane == nullptr)
    {
        refreshExpressionLaneControls (true);
        return;
    }

    auto name = expressionLaneNameEditor_.getText().trim().toStdString();
    if (name.empty())
    {
        refreshExpressionLaneControls (true);
        return;
    }

    if (name == lane->name())
        return;

    const auto laneId = lane->id();
    if (! appServices_.renameExpressionLane (selectedClip_->trackId, selectedClip_->clipId, laneId, std::move (name)))
        refreshExpressionLaneControls (true);

    refreshContentBounds();
    repaint();
}

void PianoRollComponent::commitExpressionLaneEnabled()
{
    if (refreshingExpressionLaneControls_ || ! selectedClip_.has_value())
        return;

    const auto* lane = selectedExpressionLane();
    if (lane == nullptr)
    {
        refreshExpressionLaneControls (true);
        return;
    }

    const auto enabled = expressionLaneEnabledButton_.getToggleState();
    if (enabled == lane->enabled())
        return;

    appServices_.setExpressionLaneEnabled (selectedClip_->trackId, selectedClip_->clipId, lane->id(), enabled);
    refreshContentBounds();
    repaint();
}

void PianoRollComponent::commitExpressionLanePolarity()
{
    if (refreshingExpressionLaneControls_ || ! selectedClip_.has_value())
        return;

    const auto* lane = selectedExpressionLane();
    if (lane == nullptr)
    {
        refreshExpressionLaneControls (true);
        return;
    }

    const auto polarity = expressionLanePolarityCombo_.getSelectedId() == 2
        ? core::sequencing::ExpressionLanePolarity::bipolar
        : core::sequencing::ExpressionLanePolarity::unipolar;
    if (polarity == lane->polarity())
        return;

    appServices_.setExpressionLanePolarity (selectedClip_->trackId, selectedClip_->clipId, lane->id(), polarity);
    refreshContentBounds();
    repaint();
}

void PianoRollComponent::toggleChromaticReveal()
{
    chromaticReveal_ = ! chromaticReveal_;
    showChromaticButton_.setToggleState (chromaticReveal_, juce::dontSendNotification);
    refreshContentBounds();
    repaint();
}

const core::sequencing::MidiClip* PianoRollComponent::selectedMidiClip() const
{
    if (! selectedClip_.has_value())
        return nullptr;

    const auto* track = appServices_.project().findTrackById (selectedClip_->trackId);
    if (track == nullptr)
        return nullptr;

    return track->findClipById (selectedClip_->clipId);
}

void PianoRollComponent::commitClipLengthControls()
{
    if (refreshingClipControls_ || ! selectedClip_.has_value())
        return;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
    {
        refreshClipControls (true);
        return;
    }

    const auto barsText = clipLengthBarsEditor_.getText().trim();
    const auto beatsText = clipLengthBeatsEditor_.getText().trim();
    if (barsText.isEmpty() || beatsText.isEmpty())
    {
        refreshClipControls (true);
        return;
    }

    const auto bars = std::max (0, barsText.getIntValue());
    const auto beats = std::max (0, beatsText.getIntValue());
    const auto totalBeats = std::max (1, (bars * beatsPerBar) + beats);
    const auto newLength = core::time::TickDuration::fromTicks (static_cast<std::int64_t> (totalBeats) * ticksPerBeat);
    if (newLength == clip->length())
    {
        refreshClipControls (true);
        return;
    }

    const auto result = appServices_.commandStack().execute (
        std::make_unique<core::commands::ResizeClipCommand> (selectedClip_->trackId, selectedClip_->clipId, newLength));
    if (result.failed())
        appServices_.reportWarning ("Clip length update failed: " + result.error());

    refreshContentBounds();
    refreshClipControls (true);
    repaint();
}

void PianoRollComponent::commitClipLoopToggle()
{
    if (refreshingClipControls_ || ! selectedClip_.has_value())
        return;

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
    {
        refreshClipControls (true);
        return;
    }

    const auto shouldLoop = clipLoopButton_.getToggleState();
    if (shouldLoop == clip->loop().isEnabled())
        return;

    auto newLength = clip->length();
    auto newLoop = core::sequencing::ClipLoop::disabled();
    if (shouldLoop)
    {
        const auto loopDuration = clip->sourceLength().ticks() > 0 ? clip->sourceLength() : clip->length();
        newLoop = core::sequencing::ClipLoop::enabled (loopDuration);
    }
    else if (newLength < clip->sourceLength())
    {
        newLength = clip->sourceLength();
    }

    const auto result = appServices_.commandStack().execute (
        std::make_unique<core::commands::ResizeClipCommand> (selectedClip_->trackId, selectedClip_->clipId, newLength, newLoop));
    if (result.failed())
        appServices_.reportWarning ("Clip loop update failed: " + result.error());

    refreshContentBounds();
    refreshClipControls (true);
    repaint();
}

void PianoRollComponent::refreshClipControls (bool forceTextRefresh)
{
    refreshingClipControls_ = true;

    const auto* clip = selectedMidiClip();
    const auto hasClip = clip != nullptr;
    clipLengthLabel_.setEnabled (hasClip);
    clipLengthSeparatorLabel_.setEnabled (hasClip);
    clipLengthBarsEditor_.setEnabled (hasClip);
    clipLengthBeatsEditor_.setEnabled (hasClip);
    clipLoopButton_.setEnabled (hasClip);

    if (clip == nullptr)
    {
        clipLengthBarsEditor_.setText ({}, juce::dontSendNotification);
        clipLengthBeatsEditor_.setText ({}, juce::dontSendNotification);
        clipLoopButton_.setToggleState (false, juce::dontSendNotification);
        refreshingClipControls_ = false;
        return;
    }

    const auto editingLength = clipLengthBarsEditor_.hasKeyboardFocus (true) || clipLengthBeatsEditor_.hasKeyboardFocus (true);
    if (forceTextRefresh || ! editingLength)
    {
        const auto roundedTicks = roundToSnap (clip->length().ticks(), ticksPerBeat);
        const auto totalBeats = std::max<std::int64_t> (1, roundedTicks / ticksPerBeat);
        clipLengthBarsEditor_.setText (juce::String (static_cast<int> (totalBeats / beatsPerBar)), juce::dontSendNotification);
        clipLengthBeatsEditor_.setText (juce::String (static_cast<int> (totalBeats % beatsPerBar)), juce::dontSendNotification);
    }

    clipLoopButton_.setToggleState (clip->loop().isEnabled(), juce::dontSendNotification);
    refreshingClipControls_ = false;
}

void PianoRollComponent::refreshGridDivisionCombo()
{
    const auto& settings = appServices_.project().rhythmSettings();
    const auto definitions = core::time::availableGridDivisions (settings);
    const auto currentGridId = core::time::gridDivisionDefinitionFor (settings.currentGridDivisionId(), settings).id;

    gridDivisionCombo_.clear (juce::dontSendNotification);

    auto itemId = 1;
    auto selectedItemId = itemId;
    for (const auto& definition : definitions)
    {
        gridDivisionCombo_.addItem (definition.displayName, itemId);
        if (definition.id == currentGridId)
            selectedItemId = itemId;

        ++itemId;
    }

    gridDivisionCombo_.setSelectedId (selectedItemId, juce::dontSendNotification);
}

void PianoRollComponent::setCurrentGridFromCombo()
{
    const auto selectedIndex = gridDivisionCombo_.getSelectedId() - 1;
    if (selectedIndex < 0)
        return;

    const auto definitions = core::time::availableGridDivisions (appServices_.project().rhythmSettings());
    if (selectedIndex >= static_cast<int> (definitions.size()))
        return;

    auto settings = appServices_.project().rhythmSettings();
    settings.setCurrentGridDivisionId (definitions[static_cast<std::size_t> (selectedIndex)].id);

    const auto result = appServices_.commandStack().execute (std::make_unique<core::commands::SetProjectRhythmSettingsCommand> (settings));
    if (result.failed())
        appServices_.reportWarning ("Grid setting update failed: " + result.error());

    refreshContentBounds();
    repaint();
}

void PianoRollComponent::showTupletSettingsMenu()
{
    const auto settings = appServices_.project().rhythmSettings();

    juce::PopupMenu menu;
    menu.addItem (1, "Triplets", true, settings.tripletsEnabled());
    menu.addItem (2, "Quintuplets", true, settings.quintupletsEnabled());
    menu.addItem (3, "Septuplets", true, settings.septupletsEnabled());
    menu.addItem (4, "Nonuplets", true, settings.nonupletsEnabled());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tupletSettingsButton_),
                        [this] (int result)
                        {
                            if (result <= 0)
                                return;

                            auto newSettings = appServices_.project().rhythmSettings();
                            switch (result)
                            {
                                case 1: newSettings.setTripletsEnabled (! newSettings.tripletsEnabled()); break;
                                case 2: newSettings.setQuintupletsEnabled (! newSettings.quintupletsEnabled()); break;
                                case 3: newSettings.setSeptupletsEnabled (! newSettings.septupletsEnabled()); break;
                                case 4: newSettings.setNonupletsEnabled (! newSettings.nonupletsEnabled()); break;
                                default: return;
                            }

                            const auto currentGrid = core::time::gridDivisionDefinitionFor (newSettings.currentGridDivisionId(), newSettings);
                            newSettings.setCurrentGridDivisionId (currentGrid.id);

                            const auto commandResult = appServices_.commandStack().execute (
                                std::make_unique<core::commands::SetProjectRhythmSettingsCommand> (newSettings));
                            if (commandResult.failed())
                                appServices_.reportWarning ("Tuplet setting update failed: " + commandResult.error());

                            refreshContentBounds();
                            repaint();
                        });
}

std::optional<core::sequencing::HarmonicContext> PianoRollComponent::targetContextForCurrentClip() const
{
    if (! selectedClip_.has_value())
        return std::nullopt;

    const auto* track = appServices_.project().findTrackById (selectedClip_->trackId);
    const auto* clip = track == nullptr ? nullptr : track->findClipById (selectedClip_->clipId);
    if (clip == nullptr)
        return std::nullopt;

    const core::sequencing::HarmonicContextResolver resolver { appServices_.project().musicalStructure() };
    const auto focusTick = playheadTick_ >= clip->startInProject() && playheadTick_ < clip->endInProject()
        ? playheadTick_
        : clip->startInProject();
    return resolver.resolveAt (focusTick);
}

void PianoRollComponent::chromaticTransposeToCurrentContext()
{
    if (! selectedClip_.has_value())
        return;

    const auto targetContext = targetContextForCurrentClip();
    if (! targetContext.has_value())
        return;

    const auto result = appServices_.commandStack().execute (std::make_unique<core::commands::ChromaticTransposeClipCommand> (
        selectedClip_->trackId,
        selectedClip_->clipId,
        *targetContext));

    if (result.failed())
        appServices_.reportWarning ("Chromatic clip transpose failed: " + result.error());

    refreshContentBounds();
    repaint();
}

void PianoRollComponent::scaleDegreeTransposeToCurrentContext()
{
    if (! selectedClip_.has_value())
        return;

    const auto selectedNoteIds = content_ != nullptr ? content_->selectedNoteIds() : std::vector<std::string> {};
    if (! selectedNoteIds.empty())
    {
        const auto result = appServices_.commandStack().execute (
            std::make_unique<core::commands::ScaleDegreeTransposeSelectedNotesCommand> (
                selectedClip_->trackId,
                selectedClip_->clipId,
                selectedNoteIds));

        if (result.failed())
            appServices_.reportWarning ("Scale-degree selected-note transpose failed: " + result.error());

        refreshContentBounds();
        repaint();
        return;
    }

    const auto targetContext = targetContextForCurrentClip();
    if (! targetContext.has_value())
        return;

    const auto result = appServices_.commandStack().execute (std::make_unique<core::commands::ScaleDegreeTransposeClipCommand> (
        selectedClip_->trackId,
        selectedClip_->clipId,
        *targetContext));

    if (result.failed())
        appServices_.reportWarning ("Scale-degree clip transpose failed: " + result.error());

    refreshContentBounds();
    repaint();
}

void PianoRollComponent::setPlayheadTick (core::time::TickPosition playheadTick)
{
    if (playheadTick_ == playheadTick)
        return;

    const auto previousTick = playheadTick_;
    playheadTick_ = playheadTick;

    if (content_ != nullptr)
        content_->repaintPlayheadTransition (previousTick, playheadTick_);
    else
        repaint();
}

bool PianoRollComponent::selectAllNotes()
{
    return content_ != nullptr && content_->selectAllNotesInClip();
}

bool PianoRollComponent::hasOpenClip() const
{
    return content_ != nullptr && content_->hasOpenClip();
}

bool PianoRollComponent::hasSelectedNotes() const
{
    return content_ != nullptr && content_->hasSelectedNotes();
}

bool PianoRollComponent::copySelectedNotes()
{
    return content_ != nullptr && content_->copySelectedNotes();
}

bool PianoRollComponent::pasteCopiedNotes()
{
    return ! expressionModeEnabled_ && content_ != nullptr && content_->pasteCopiedNotes();
}

bool PianoRollComponent::debugEmulateSingleClickAtFirstEditableCell()
{
    return content_ != nullptr && content_->debugEmulateSingleClickAtFirstEditableCell();
}

bool PianoRollComponent::debugEmulateDoubleClickAtFirstEditableCell()
{
    return content_ != nullptr && content_->debugEmulateDoubleClickAtFirstEditableCell();
}

bool PianoRollComponent::debugEmulateMarqueeSelectAllVisibleNotes()
{
    return content_ != nullptr && content_->debugEmulateMarqueeSelectAllVisibleNotes();
}

std::optional<juce::Point<float>> PianoRollComponent::debugFirstEditableCellGlobalPoint() const
{
    if (content_ == nullptr)
        return std::nullopt;

    const auto contentPoint = content_->debugFirstEditableCellGlobalPoint();
    if (! contentPoint.has_value())
        return std::nullopt;

    const auto viewportOffset = juce::Point<float> {
        static_cast<float> (viewport_.getViewPositionX()),
        static_cast<float> (viewport_.getViewPositionY())
    };
    const auto viewportPoint = viewport_.getPosition().toFloat() + (*contentPoint - viewportOffset);
    return getScreenBounds().getPosition().toFloat() + viewportPoint;
}

std::optional<std::pair<std::string, std::string>> PianoRollComponent::openClipIds() const
{
    if (! selectedClip_.has_value())
        return std::nullopt;

    return std::make_pair (selectedClip_->trackId, selectedClip_->clipId);
}

std::vector<std::string> PianoRollComponent::selectedNoteIdsForClip (const std::string& trackId, const std::string& clipId) const
{
    if (! selectedClip_.has_value()
        || selectedClip_->trackId != trackId
        || selectedClip_->clipId != clipId
        || content_ == nullptr)
    {
        return {};
    }

    return content_->selectedNoteIds();
}

void PianoRollComponent::paint (juce::Graphics& graphics)
{
    core::diagnostics::ScopedPerformanceTimer timer { "PianoRollComponent::paint" };

    graphics.fillAll (surfaceColour);

    auto header = getLocalBounds().removeFromTop (30).reduced (12, 0);
    header.removeFromRight (headerControlsReservedWidth);
    graphics.setColour (textColour);
    graphics.setFont (juce::FontOptions { 13.5f, juce::Font::bold });

    if (! selectedClip_.has_value())
    {
        graphics.drawText (expressionModeEnabled_ ? "Expression Mode" : "Piano Roll", header, juce::Justification::centredLeft);
        return;
    }

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
    {
        graphics.drawText (expressionModeEnabled_ ? "Expression Mode" : "Piano Roll", header, juce::Justification::centredLeft);
        return;
    }

    const core::sequencing::HarmonicContextResolver resolver { appServices_.project().musicalStructure() };
    const auto focusTick = playheadTick_ >= clip->startInProject() && playheadTick_ < clip->endInProject()
        ? playheadTick_
        : clip->startInProject();
    const auto context = resolver.resolveAt (focusTick);
    graphics.drawText ((expressionModeEnabled_ ? "Expression - " : "Piano Roll - ") + clip->name(),
                       header.removeFromLeft (std::min (300, header.getWidth())),
                       juce::Justification::centredLeft);
    header.removeFromLeft (8);

    if (header.getWidth() >= 110)
    {
        graphics.setColour (mutedTextColour);
        graphics.setFont (juce::FontOptions { 12.0f });
        graphics.drawText (keyNameFor (context.keyCenter()) + " " + context.scaleDefinitionName(),
                           header,
                           juce::Justification::centredRight);
    }
}

void PianoRollComponent::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (30).reduced (12, 3);
    expressionModeButton_.setBounds (header.removeFromLeft (104));
    header.removeFromLeft (8);
    gridDivisionCombo_.setBounds (header.removeFromRight (96));
    header.removeFromRight (8);
    tupletSettingsButton_.setBounds (header.removeFromRight (76));
    header.removeFromRight (8);
    scaleDegreeTransposeButton_.setBounds (header.removeFromRight (168));
    header.removeFromRight (8);
    chromaticTransposeButton_.setBounds (header.removeFromRight (158));
    header.removeFromRight (8);
    showChromaticButton_.setBounds (header.removeFromRight (138));
    header.removeFromRight (8);
    clipLoopButton_.setBounds (header.removeFromRight (64));
    header.removeFromRight (8);
    clipLengthBeatsEditor_.setBounds (header.removeFromRight (34));
    clipLengthSeparatorLabel_.setBounds (header.removeFromRight (10));
    clipLengthBarsEditor_.setBounds (header.removeFromRight (34));
    header.removeFromRight (4);
    clipLengthLabel_.setBounds (header.removeFromRight (52));

    if (expressionModeEnabled_)
    {
        auto lanePanel = bounds.removeFromLeft (expressionLanePanelWidth);
        lanePanel.reduce (8, 8);
        auto laneHeader = lanePanel.removeFromTop (28);
        expressionLaneHeaderLabel_.setBounds (laneHeader.removeFromLeft (std::max (0, laneHeader.getWidth() - 34)));
        addExpressionLaneButton_.setBounds (laneHeader.removeFromRight (30));
        lanePanel.removeFromTop (8);
        expressionReleaseModeButton_.setBounds (lanePanel.removeFromTop (24));
        lanePanel.removeFromTop (8);
        expressionLaneList_->setBounds (lanePanel.removeFromTop (std::min (expressionLaneList_->preferredHeight(), std::max (80, lanePanel.getHeight() - 96))));
        lanePanel.removeFromTop (8);
        expressionLaneNameEditor_.setBounds (lanePanel.removeFromTop (24));
        lanePanel.removeFromTop (6);
        expressionLaneEnabledButton_.setBounds (lanePanel.removeFromTop (24));
        lanePanel.removeFromTop (6);
        expressionLanePolarityCombo_.setBounds (lanePanel.removeFromTop (26));
        lanePanel.removeFromTop (8);
        if (expressionRoutingPanel_ != nullptr)
            expressionRoutingPanel_->setBounds (lanePanel.removeFromTop (std::min (expressionRoutingPanel_->preferredHeight(), lanePanel.getHeight())));
    }
    else
    {
        expressionLaneHeaderLabel_.setBounds ({});
        expressionReleaseModeButton_.setBounds ({});
        addExpressionLaneButton_.setBounds ({});
        expressionLaneList_->setBounds ({});
        if (expressionRoutingPanel_ != nullptr)
            expressionRoutingPanel_->setBounds ({});
        if (phraseEnvelopePanel_ != nullptr)
            phraseEnvelopePanel_->setBounds ({});
        if (cyclicExpressionPanel_ != nullptr)
            cyclicExpressionPanel_->setBounds ({});
        expressionLaneNameEditor_.setBounds ({});
        expressionLaneEnabledButton_.setBounds ({});
        expressionLanePolarityCombo_.setBounds ({});
    }

    viewport_.setBounds (bounds);
    const auto layoutFloatingExpressionPanel = [this] (juce::Component* panel, int panelHeight)
    {
        if (expressionModeEnabled_ && panelHeight > 0)
        {
            const auto viewportBounds = viewport_.getBounds();
            const auto horizontalScroll = viewport_.getViewPositionX();
            const auto visibleGridInset = std::max (0, pitchHeaderWidth - horizontalScroll);
            auto panelBounds = viewportBounds.withTrimmedLeft (visibleGridInset + 8).withTrimmedRight (12);
            panelBounds.setY (viewportBounds.getY() + rulerHeight + 4);
            panelBounds.setHeight (std::min (panelHeight, std::max (96, viewportBounds.getHeight() / 3)));
            panel->setBounds (panelBounds);
            panel->toFront (false);
        }
        else
        {
            panel->setBounds ({});
        }
    };
    if (phraseEnvelopePanel_ != nullptr)
        layoutFloatingExpressionPanel (phraseEnvelopePanel_.get(), phraseEnvelopePanel_->preferredHeight());
    if (cyclicExpressionPanel_ != nullptr)
        layoutFloatingExpressionPanel (cyclicExpressionPanel_.get(), cyclicExpressionPanel_->preferredHeight());
    refreshContentBounds();
}

void PianoRollComponent::refreshContentBounds()
{
    if (content_ == nullptr)
        return;

    refreshGridDivisionCombo();
    refreshClipControls();
    refreshExpressionLaneControls();

    if (expressionLaneList_ != nullptr)
        expressionLaneList_->repaint();

    const auto width = std::max (viewport_.getWidth(), content_->preferredWidth());
    const auto height = std::max (viewport_.getHeight(), content_->preferredHeight());
    content_->setSize (width, height);
    content_->repaint();
}
}
