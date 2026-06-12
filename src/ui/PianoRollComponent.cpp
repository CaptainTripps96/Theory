#include "ui/PianoRollComponent.h"

#include "app/AppServices.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/ArpeggiateSelectionCommand.h"
#include "core/commands/ChordStackingCommand.h"
#include "core/commands/DeleteNoteCommand.h"
#include "core/commands/MoveNoteCommand.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/commands/ResizeNoteCommand.h"
#include "core/commands/SetProjectRhythmSettingsCommand.h"
#include "core/commands/TransposeClipCommand.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/MidiPitch.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/HarmonicContextResolver.h"
#include "core/sequencing/HarmonicOverlay.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"
#include "core/sequencing/Arpeggiator.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/time/GridDivision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
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
const auto noteOutlineColour = juce::Colour { 0xff0f2830 };
const auto selectedOutlineColour = juce::Colour { 0xfff8fafc };
const auto buttonColour = juce::Colour { 0xff24303d };
const auto buttonOnColour = juce::Colour { 0xff3f6f7f };

constexpr int pitchHeaderWidth = 68;
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
                          std::function<void()>& toggleChromaticReveal)
        : appServices_ (appServices),
          selectedClip_ (selectedClip),
          playheadTick_ (playheadTick),
          chromaticReveal_ (chromaticReveal),
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

    void clearNoteSelection()
    {
        selectedNoteIds_.clear();
        dragState_.reset();
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

                const auto noteBounds = dragState_.has_value()
                    ? previewBoundsFor (note, *dragState_, clipLength, segments, layouts)
                    : boundsForNote (note, segments, layouts, paintClipBounds);
                const auto visualState = dragState_.has_value()
                    ? visualStateFor (note, *dragState_, clipLength, segments)
                    : NoteVisualState { note.pitch().value(), note.spelling() };

                const auto selected = isSelected (note.id());
                for (const auto& bounds : noteBounds)
                {
                    if (! bounds.bounds.intersects (paintClipBounds))
                        continue;

                    const auto& segment = segments[bounds.segmentIndex];
                    const auto* lane = laneForNoteState (visualState.midiPitch, visualState.spelling, segment.lanes);
                    graphics.setColour (selected ? selectedNoteColour : noteColourForLane (lane));
                    graphics.fillRoundedRectangle (bounds.bounds.toFloat(), 3.0f);
                    graphics.setColour (selected ? selectedOutlineColour : noteOutlineColour);
                    graphics.drawRoundedRectangle (bounds.bounds.toFloat().reduced (0.5f), 3.0f, selected ? 1.5f : 1.0f);

                    if (bounds.bounds.getWidth() > 24)
                    {
                        graphics.setColour (juce::Colours::black.withAlpha (0.72f));
                        graphics.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
                        const auto label = noteLabelForState (visualState.midiPitch, visualState.spelling, segment.lanes);
                        graphics.drawText (label, bounds.bounds.reduced (5, 0), juce::Justification::centredLeft);
                    }
                }
            }
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
        appServices_.observeLivePluginParameterState();
        appServices_.restoreObservedPluginParameterStateSoon();
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

            dragState_ = DragState {
                dragModeForHit (*hit, event.position),
                event.x,
                event.y,
                event.x,
                event.y,
                originalStatesForSelection (*clip)
            };
            repaint();
            return;
        }

        if (event.x < pitchHeaderWidth || ! segmentIndexForPoint (event.position, segments, layouts).has_value())
        {
            activeArpeggioSourceNotes_.clear();
            if (! hasSelectionModifier (event.mods))
                selectedNoteIds_.clear();
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
        appServices_.observeLivePluginParameterState();
        appServices_.restoreObservedPluginParameterStateSoon();
        grabKeyboardFocus();

        auto* clip = selectedMidiClip();
        if (clip == nullptr)
            return;

        const auto segments = visiblePitchSegments (*clip);
        const auto layouts = segmentLayouts (segments);
        if (auto hit = noteAt (event.position, segments, layouts))
        {
            selectedNoteIds_ = { hit->noteId };
            deleteSelectedNotes();
            return;
        }

        if (event.x >= pitchHeaderWidth && event.y >= rulerHeight)
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
        if (key == juce::KeyPress::deleteKey
            || key == juce::KeyPress::backspaceKey
            || key == juce::KeyPress::numberPadDelete)
        {
            deleteSelectedNotes();
            return true;
        }

        const auto modifiers = key.getModifiers();
        const auto altDown = modifiers.isAltDown();
        const auto commandDown = modifiers.isCommandDown() || modifiers.isCtrlDown();
        const auto shiftDown = modifiers.isShiftDown();

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

private:
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
            for (const auto& note : clip->notes())
                selectedNoteIds_.push_back (note.id());

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
        if (! additive)
            selectedNoteIds_.clear();

        for (const auto& note : clip->notes())
        {
            const auto noteBounds = boundsForNote (note, segments, layouts);
            if (std::any_of (noteBounds.begin(), noteBounds.end(), [bounds] (const auto& rendered) {
                    return bounds.intersects (rendered.bounds);
                }))
            {
                if (! isSelected (note.id()))
                    selectedNoteIds_.push_back (note.id());
            }
        }
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

    std::vector<NoteRenderBounds> boundsForNoteState (core::time::TickPosition start,
                                                      core::time::TickDuration duration,
                                                      int pitch,
                                                      std::optional<core::music_theory::NoteName> spelling,
                                                      const std::vector<PianoRollSegment>& segments,
                                                      const std::vector<SegmentLayout>& layouts,
                                                      std::optional<juce::Rectangle<int>> paintClipBounds = std::nullopt) const
    {
        std::vector<NoteRenderBounds> result;
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

            result.push_back (NoteRenderBounds {
                juce::Rectangle<int> {
                    x,
                    y,
                    width,
                    height
                },
                layout.segmentIndex
            });
        }

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
    std::function<void()>& toggleChromaticReveal_;
    std::vector<std::string> selectedNoteIds_;
    std::vector<core::sequencing::MidiNote> noteClipboard_;
    std::vector<core::sequencing::MidiNote> activeArpeggioSourceNotes_;
    std::optional<DragState> dragState_;
    std::optional<MarqueeState> marqueeState_;
    bool draggingPasteCursor_ = false;
    int pixelsPerQuarter_ = defaultPixelsPerQuarter;
    int rowHeight_ = defaultRowHeight;
    LaneBackgroundCache laneBackgroundCache_;
    std::string arpeggioSubdivisionId_ { core::time::ProjectRhythmSettings::defaultGridDivisionId };
    core::sequencing::ArpeggioPattern arpeggioPattern_ = core::sequencing::ArpeggioPattern::up;
};

PianoRollComponent::PianoRollComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    toggleChromaticRevealCallback_ = [this] { toggleChromaticReveal(); };

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
    chromaticTransposeButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    chromaticTransposeButton_.setColour (juce::TextButton::textColourOffId, textColour);
    chromaticTransposeButton_.onClick = [this] { chromaticTransposeToCurrentContext(); };
    addAndMakeVisible (chromaticTransposeButton_);

    scaleDegreeTransposeButton_.setButtonText ("Scale-Degree Transpose");
    scaleDegreeTransposeButton_.setColour (juce::TextButton::buttonColourId, buttonColour);
    scaleDegreeTransposeButton_.setColour (juce::TextButton::textColourOffId, textColour);
    scaleDegreeTransposeButton_.onClick = [this] { scaleDegreeTransposeToCurrentContext(); };
    addAndMakeVisible (scaleDegreeTransposeButton_);

    content_ = new RollContentComponent { appServices_, selectedClip_, playheadTick_, chromaticReveal_, toggleChromaticRevealCallback_ };
    viewport_.setViewedComponent (content_, true);
    viewport_.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport_);

    refreshGridDivisionCombo();
    refreshClipControls (true);
}

void PianoRollComponent::openClip (std::string trackId, std::string clipId)
{
    selectedClip_ = ClipSelection { std::move (trackId), std::move (clipId) };
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
    if (content_ != nullptr)
        content_->clearNoteSelection();

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

    playheadTick_ = playheadTick;
    refreshContentBounds();
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
    return content_ != nullptr && content_->pasteCopiedNotes();
}

bool PianoRollComponent::debugEmulateSingleClickAtFirstEditableCell()
{
    return content_ != nullptr && content_->debugEmulateSingleClickAtFirstEditableCell();
}

bool PianoRollComponent::debugEmulateDoubleClickAtFirstEditableCell()
{
    return content_ != nullptr && content_->debugEmulateDoubleClickAtFirstEditableCell();
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
        graphics.drawText ("Piano Roll", header, juce::Justification::centredLeft);
        return;
    }

    const auto* clip = selectedMidiClip();
    if (clip == nullptr)
    {
        graphics.drawText ("Piano Roll", header, juce::Justification::centredLeft);
        return;
    }

    const core::sequencing::HarmonicContextResolver resolver { appServices_.project().musicalStructure() };
    const auto focusTick = playheadTick_ >= clip->startInProject() && playheadTick_ < clip->endInProject()
        ? playheadTick_
        : clip->startInProject();
    const auto context = resolver.resolveAt (focusTick);
    graphics.drawText ("Piano Roll - " + clip->name(), header.removeFromLeft (std::min (300, header.getWidth())), juce::Justification::centredLeft);
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
    viewport_.setBounds (bounds);
    refreshContentBounds();
}

void PianoRollComponent::refreshContentBounds()
{
    if (content_ == nullptr)
        return;

    refreshGridDivisionCombo();
    refreshClipControls();

    const auto width = std::max (viewport_.getWidth(), content_->preferredWidth());
    const auto height = std::max (viewport_.getHeight(), content_->preferredHeight());
    content_->setSize (width, height);
    content_->repaint();
}
}
