#include "ui/TimelineComponent.h"

#include "app/AppServices.h"
#include "core/commands/AddChordRegionCommand.h"
#include "core/commands/AddClipCommand.h"
#include "core/commands/AddKeyCenterRegionCommand.h"
#include "core/commands/AddScaleModeRegionCommand.h"
#include "core/commands/AddTempoNodeCommand.h"
#include "core/commands/AddTimeSignatureMarkerCommand.h"
#include "core/commands/DeleteChordRegionCommand.h"
#include "core/commands/DeleteClipCommand.h"
#include "core/commands/DeleteKeyCenterRegionCommand.h"
#include "core/commands/DeleteScaleModeRegionCommand.h"
#include "core/commands/GlobalizeChordProgressionCommand.h"
#include "core/commands/MixerCommands.h"
#include "core/commands/MoveClipCommand.h"
#include "core/commands/ReplaceChordRegionCommand.h"
#include "core/commands/ReplaceKeyCenterRegionCommand.h"
#include "core/commands/ReplaceScaleModeRegionCommand.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/AutomationPlayback.h"
#include "core/sequencing/KeyCenterRegion.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Region.h"
#include "core/sequencing/ScaleModeRegion.h"
#include "core/sequencing/Track.h"
#include "core/time/TimeSignatureMap.h"
#include "engine/plugins/PluginDescription.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserDragPayload.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <variant>
#include <vector>

namespace tsq::ui
{
namespace
{
const auto backgroundColour = juce::Colour { 0xff11161d };
const auto laneColour = juce::Colour { 0xff171e28 };
const auto trackColour = juce::Colour { 0xff1d2632 };
const auto selectedTrackColour = juce::Colour { 0xff223246 };
const auto gridColour = juce::Colour { 0xff2a3441 };
const auto beatGridColour = juce::Colour { 0xff232c37 };
const auto textColour = juce::Colour { 0xffdbe4ef };
const auto mutedTextColour = juce::Colour { 0xff8290a1 };
const auto clipColour = juce::Colour { 0xff4fb3c8 };
const auto audioClipColour = juce::Colour { 0xff78b86c };
const auto selectedClipColour = juce::Colour { 0xff70d3e5 };
const auto invalidClipColour = juce::Colour { 0xffd96a5f };
const auto warningColour = juce::Colour { 0xffe3b45d };
const auto keyRegionColour = juce::Colour { 0xffd6b35f };
const auto scaleRegionColour = juce::Colour { 0xffa58fe8 };
const auto chordRegionColour = juce::Colour { 0xff6fd0a8 };
const auto borrowedChordRegionColour = juce::Colour { 0xffe0967a };
const auto tempoColour = juce::Colour { 0xffe36f6f };
const auto meterColour = juce::Colour { 0xff87c36f };
const auto automationColour = juce::Colour { 0xffd8a04f };
const auto automationPointColour = juce::Colour { 0xfff1d184 };

constexpr int rulerHeight = 32;
constexpr int structureHeight = 166;
constexpr int trackTopPadding = 12;
constexpr int rowHeight = 86;
constexpr int automationLaneHeight = 42;
constexpr int automationLaneGap = 4;
constexpr int rowGap = 8;
constexpr int horizontalPadding = 8;
constexpr int beatsPerBar = 4;
constexpr int resizeHandleWidth = 9;
constexpr int defaultClipBars = 4;
constexpr int defaultRegionBars = 4;
constexpr int defaultTimelineBars = 58;
constexpr int timelineExtensionBars = 2;
constexpr int minimumAudioWaveformWidth = 42;
constexpr int minimumAudioLabelWidth = 64;
constexpr int minimumAudioBeatLabelWidth = 92;
constexpr auto ticksPerBeat = core::time::ticksPerQuarterNote;

constexpr int insertMidiTrackMenuId = 101;
constexpr int insertAudioTrackMenuId = 102;
constexpr int insertReturnTrackMenuId = 103;
constexpr int insertMasterTrackMenuId = 104;

int visibleAutomationLaneCount (const core::sequencing::Track& track)
{
    return static_cast<int> (std::count_if (track.automationLanes().begin(),
                                            track.automationLanes().end(),
                                            [] (const auto& lane) { return lane.visible(); }));
}

int trackHeightFor (const core::sequencing::Track& track)
{
    const auto laneCount = visibleAutomationLaneCount (track);
    return rowHeight + (laneCount * (automationLaneHeight + automationLaneGap));
}

juce::String automationTargetLabel (const core::sequencing::Project& project,
                                    const core::sequencing::AutomationTarget& target)
{
    using core::sequencing::AutomationTargetKind;

    switch (target.kind)
    {
        case AutomationTargetKind::trackVolume:
            return "Volume";
        case AutomationTargetKind::trackPan:
            return "Pan";
        case AutomationTargetKind::trackMute:
            return "Mute";
        case AutomationTargetKind::sendLevel:
            if (const auto* returnTrack = project.findTrackById (target.sendTargetTrackId))
                return "Send " + juce::String::fromUTF8 (returnTrack->name().c_str());
            return "Send";
        case AutomationTargetKind::deviceBypass:
            return "Bypass " + juce::String::fromUTF8 (target.deviceSlotId.value.c_str());
        case AutomationTargetKind::pluginParameter:
            return juce::String::fromUTF8 (target.pluginParameterId.c_str());
    }

    return "Automation";
}

double automationValueForY (juce::Rectangle<int> bounds, int y)
{
    if (bounds.getHeight() <= 1)
        return 0.0;

    const auto value = 1.0 - (static_cast<double> (y - bounds.getY()) / static_cast<double> (bounds.getHeight()));
    return std::clamp (value, 0.0, 1.0);
}

int yForAutomationValue (juce::Rectangle<int> bounds, double normalizedValue)
{
    const auto value = std::clamp (normalizedValue, 0.0, 1.0);
    return bounds.getY() + static_cast<int> (std::llround ((1.0 - value) * static_cast<double> (bounds.getHeight())));
}

std::string stableIdForPluginReference (const core::sequencing::PluginReference& reference)
{
    if (! reference.uniqueIdentifier.empty())
        return reference.uniqueIdentifier;

    if (! reference.format.empty() && reference.uniqueId != 0)
        return reference.format + ":" + std::to_string (reference.uniqueId);

    if (! reference.fileOrIdentifier.empty())
        return reference.fileOrIdentifier;

    return {};
}

std::string lowercase (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char character) {
        return static_cast<char> (std::tolower (character));
    });
    return text;
}

bool isSupportedAudioFile (const std::filesystem::path& path)
{
    const auto extension = lowercase (path.extension().string());
    return extension == ".wav" || extension == ".aif" || extension == ".aiff" || extension == ".flac"
        || extension == ".mp3" || extension == ".ogg";
}

bool containsSupportedAudioFile (const juce::StringArray& files)
{
    for (const auto& file : files)
        if (isSupportedAudioFile (std::filesystem::path { file.toStdString() }))
            return true;

    return false;
}

std::int64_t roundToGrid (std::int64_t ticks)
{
    const auto grid = ticksPerBeat;
    if (ticks <= 0)
        return 0;

    return ((ticks + (grid / 2)) / grid) * grid;
}

juce::String pitchClassName (core::music_theory::PitchClass pitchClass)
{
    return core::music_theory::spellPitchClass (pitchClass).toString();
}

core::music_theory::PitchClass pitchClassFromSemitone (int semitone)
{
    return core::music_theory::PitchClass::fromSemitonesFromC (semitone);
}

juce::String timeSignatureText (const core::time::TimeSignature& timeSignature)
{
    return juce::String (timeSignature.numerator()) + "/" + juce::String (timeSignature.denominator());
}

bool parsePositiveInt (const juce::String& text, int& value)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return false;

    value = trimmed.getIntValue();
    return value > 0;
}

bool parsePositiveDouble (const juce::String& text, double& value)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return false;

    value = trimmed.getDoubleValue();
    return value > 0.0;
}

bool isScaleDragDescription (const juce::var& description)
{
    return description.isString() && description.toString().startsWith ("tsq-scale:");
}

core::time::TickDuration barSpanDuration (const core::time::TimeSignatureMap& timeSignatureMap,
                                          core::time::TickPosition position,
                                          int bars)
{
    return timeSignatureMap.barDurationAt (position) * bars;
}

int contextualStepForPixels (double pixelsPerUnit, double minimumPixels)
{
    static constexpr int steps[] { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    for (const auto step : steps)
        if (pixelsPerUnit * static_cast<double> (step) >= minimumPixels)
            return step;

    return steps[(sizeof (steps) / sizeof (steps[0])) - 1];
}

std::string scaleNameFromDragDescription (const juce::var& description)
{
    auto text = description.toString();
    return text.fromFirstOccurrenceOf ("tsq-scale:", false, false).toStdString();
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

void showModalAlert (juce::AlertWindow* window, juce::Component& owner, std::function<void (juce::AlertWindow&)> onOk)
{
    window->setBounds ({ 0, 0, 420, 220 });
    window->centreAroundComponent (&owner, window->getWidth(), window->getHeight());
    window->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
    window->setVisible (true);
    window->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [window, callback = std::move (onOk)] (int result)
            {
                std::unique_ptr<juce::AlertWindow> ownedWindow { window };
                if (result == 1)
                    callback (*window);
            }),
        false);
}
}

TimelineComponent::TimelineComponent (app::AppServices& appServices)
    : appServices_ (appServices),
      addTrackButton_ ("+ Track"),
      globalizeChordButton_ ("Globalize Chords")
{
    visibleTimelineBars_ = defaultTimelineBars;
    setWantsKeyboardFocus (true);
    setTitle ("Timeline");
    setDescription ("Arrangement timeline with musical structure lanes, clips, automation lanes, and drag-to-create track area.");
    waveformCache_.onWaveformChanged = [this] { repaint(); };

    addTrackButton_.setTooltip ("Add MIDI track");
    addTrackButton_.setTitle ("Add MIDI Track");
    addTrackButton_.setDescription ("Adds a new MIDI track to the project.");
    addTrackButton_.onClick = [this] { addTrack(); };
    addAndMakeVisible (addTrackButton_);

    globalizeChordButton_.setTooltip ("Globalize Chord Progression in Clip's Current Region");
    globalizeChordButton_.setTitle ("Globalize Chords");
    globalizeChordButton_.setDescription ("Creates chord-progression regions from the selected piano-roll notes.");
    globalizeChordButton_.onClick = [this] { globalizeSelectedClipChordProgression(); };
    addAndMakeVisible (globalizeChordButton_);
}

void TimelineComponent::setPlayheadTick (core::time::TickPosition playheadTick)
{
    if (playheadTick_ == playheadTick)
        return;

    const auto previousX = playheadPixelValid_ ? playheadPixelX_ : tickToX (playheadTick_);
    playheadTick_ = playheadTick;
    const auto currentX = tickToX (playheadTick_);
    playheadPixelX_ = currentX;
    playheadPixelValid_ = true;
    const auto left = std::min (previousX, currentX) - 8;
    const auto right = std::max (previousX, currentX) + 9;
    repaint (juce::Rectangle<int>::leftTopRightBottom (left, 0, right, getHeight()).getIntersection (getLocalBounds()));
}

bool TimelineComponent::copySelectionToClipboard()
{
    copySelection();
    return true;
}

bool TimelineComponent::pasteSelectionFromClipboard()
{
    pasteSelection();
    return true;
}

bool TimelineComponent::selectAllInFocusedField()
{
    selectedClip_.reset();
    selectedStructureRegion_.reset();
    selectedAutomationPoint_.reset();

    if (focusedField_ == FocusedField::automation)
    {
        repaint();
        return true;
    }

    if (focusedField_ == FocusedField::structure)
    {
        selectedStructureRegions_.clear();

        const auto& structure = appServices_.project().musicalStructure();
        if (structure.keyCenterRegions().empty())
            selectedStructureRegions_.push_back (StructureRegionSelection { StructureRegionKind::keyCenter, -1 });
        else
            for (int index = 0; index < static_cast<int> (structure.keyCenterRegions().size()); ++index)
                selectedStructureRegions_.push_back (StructureRegionSelection { StructureRegionKind::keyCenter, index });

        if (structure.scaleModeRegions().empty())
            selectedStructureRegions_.push_back (StructureRegionSelection { StructureRegionKind::scaleMode, -1 });
        else
            for (int index = 0; index < static_cast<int> (structure.scaleModeRegions().size()); ++index)
                selectedStructureRegions_.push_back (StructureRegionSelection { StructureRegionKind::scaleMode, index });

        for (int index = 0; index < static_cast<int> (structure.chordRegions().size()); ++index)
            selectedStructureRegions_.push_back (StructureRegionSelection { StructureRegionKind::chordProgression, index });

        if (! selectedStructureRegions_.empty())
            selectedStructureRegion_ = selectedStructureRegions_.front();
    }
    else
    {
        selectedClips_.clear();
        for (const auto& track : appServices_.project().tracks())
        {
            for (const auto& clip : track.clips())
                selectedClips_.push_back (ClipSelection { track.id(), clip.id(), ClipKind::midi });

            for (const auto& clip : track.audioClips())
                selectedClips_.push_back (ClipSelection { track.id(), clip.id(), ClipKind::audio });
        }

        if (! selectedClips_.empty())
            selectedClip_ = selectedClips_.front();
    }

    repaint();
    return true;
}

void TimelineComponent::paint (juce::Graphics& graphics)
{
    core::diagnostics::ScopedPerformanceTimer timer { "TimelineComponent::paint" };

    graphics.fillAll (backgroundColour);

    auto bounds = getLocalBounds();
    auto ruler = bounds.removeFromTop (rulerHeight);

    graphics.setColour (laneColour);
    graphics.fillRect (ruler);

    ensureTimelineFitsAllClips();
    const auto visibleEndTick = timelineEndTick();
    const auto& timeSignatureMap = appServices_.project().timeSignatureMap();

    struct ContextualGridLine
    {
        core::time::TickPosition position {};
        int x = 0;
        int strength = 0;
        juce::String label;
    };

    std::vector<ContextualGridLine> gridLines;
    const auto addGridLine = [&gridLines, this] (core::time::TickPosition position, int strength, juce::String label = {})
    {
        const auto x = tickToX (position);
        gridLines.push_back (ContextualGridLine { position, x, strength, std::move (label) });
    };

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TimelineComponent::paint grid-build" };

        const auto pixelsPerBeat = pixelsPerQuarter();
        const auto pixelsPerBar = std::max (1.0, (static_cast<double> (timeSignatureMap.barDurationAt ({}).ticks()) / ticksPerBeat) * pixelsPerBeat);
        const auto barLineStep = pixelsPerBar >= 8.0 ? 1 : contextualStepForPixels (pixelsPerBar, 10.0);
        const auto labelBarStep = contextualStepForPixels (pixelsPerBar, 58.0);

        if (pixelsPerBeat >= 72.0 || pixelsPerBeat >= 44.0)
        {
            const auto subdivisionTicks = pixelsPerBeat >= 72.0 ? ticksPerBeat / 4 : ticksPerBeat / 2;
            for (auto tick = subdivisionTicks; tick < visibleEndTick.ticks(); tick += subdivisionTicks)
            {
                if ((tick % ticksPerBeat) != 0)
                    addGridLine (core::time::TickPosition::fromTicks (tick), 0);
            }
        }

        if (pixelsPerBeat >= 14.0)
        {
            const auto beatLabelStep = pixelsPerBeat >= 34.0 ? contextualStepForPixels (pixelsPerBeat, 44.0) : 0;
            for (auto tick = ticksPerBeat; tick < visibleEndTick.ticks(); tick += ticksPerBeat)
            {
                const auto position = core::time::TickPosition::fromTicks (tick);
                const auto barBeat = timeSignatureMap.fromTicks (position);
                juce::String label;
                if (beatLabelStep > 0 && (tick / ticksPerBeat) % beatLabelStep == 0 && barBeat.beat != 1)
                    label = juce::String (barBeat.bar) + "." + juce::String (barBeat.beat);

                addGridLine (position, 1, label);
            }
        }

        for (auto bar = 1; ; ++bar)
        {
            const auto barTick = timeSignatureMap.tickAtBar (bar);
            if (barTick > visibleEndTick)
                break;

            if ((bar - 1) % barLineStep != 0)
                continue;

            const auto isMajorBar = (bar - 1) % labelBarStep == 0;
            addGridLine (barTick, isMajorBar ? 3 : 2, isMajorBar ? juce::String (bar) : juce::String {});
        }

        std::sort (gridLines.begin(), gridLines.end(), [] (const auto& lhs, const auto& rhs) {
            if (lhs.x != rhs.x)
                return lhs.x < rhs.x;

            return lhs.strength > rhs.strength;
        });

        std::vector<ContextualGridLine> mergedGridLines;
        mergedGridLines.reserve (gridLines.size());

        for (auto& line : gridLines)
        {
            if (! mergedGridLines.empty() && mergedGridLines.back().x == line.x)
            {
                auto& mergedLine = mergedGridLines.back();
                if (line.strength > mergedLine.strength)
                {
                    mergedLine.position = line.position;
                    mergedLine.strength = line.strength;
                }

                if (mergedLine.label.isEmpty() && line.label.isNotEmpty())
                    mergedLine.label = std::move (line.label);
                continue;
            }

            mergedGridLines.push_back (std::move (line));
        }

        gridLines = std::move (mergedGridLines);
    }

    const auto gridColourForStrength = [] (int strength)
    {
        if (strength >= 3)
            return gridColour;
        if (strength == 2)
            return gridColour.withAlpha (0.72f);
        if (strength == 1)
            return beatGridColour.withAlpha (0.82f);
        return beatGridColour.withAlpha (0.42f);
    };

    const auto& tracks = appServices_.project().tracks();

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TimelineComponent::paint grid-draw" };

        const auto drawGridSegment = [&graphics] (int x, int top, int bottom)
        {
            if (bottom > top)
                graphics.fillRect (x, top, 1, bottom - top);
        };

        for (const auto& line : gridLines)
        {
            graphics.setColour (gridColourForStrength (line.strength));
            if (line.strength >= 2)
                drawGridSegment (line.x, 0, rulerHeight);

            if (tracks.empty())
                drawGridSegment (line.x, rulerHeight + structureHeight, getHeight());
        }

        for (const auto& line : gridLines)
        {
            if (line.label.isNotEmpty() && line.x >= 0 && line.x <= getWidth())
            {
                graphics.setColour (mutedTextColour);
                graphics.setFont (line.strength >= 3 ? juce::FontOptions { 12.0f, juce::Font::bold }
                                                      : juce::FontOptions { 10.5f });
                graphics.drawText (line.label,
                                   line.x + 5,
                                   ruler.getY(),
                                   line.strength >= 3 ? 46 : 54,
                                   ruler.getHeight(),
                                   juce::Justification::centredLeft);
            }
        }
    }

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TimelineComponent::paint structure" };

        bounds.removeFromTop (structureHeight);
        const auto drawLaneBase = [&graphics] (juce::Rectangle<int> lane, const juce::String& label)
        {
            graphics.setColour (laneColour);
            graphics.fillRoundedRectangle (lane.toFloat(), 4.0f);
            graphics.setColour (gridColour);
            graphics.drawRoundedRectangle (lane.toFloat().reduced (0.5f), 4.0f, 1.0f);
            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
            graphics.drawText (label, lane.removeFromLeft (86).reduced (8, 0), juce::Justification::centredLeft);
        };

        drawLaneBase (structureLaneBounds (StructureLane::tempo), "Tempo");
        drawLaneBase (structureLaneBounds (StructureLane::timeSignature), "Meter");
        drawLaneBase (structureLaneBounds (StructureLane::keyCenter), "Key");
        drawLaneBase (structureLaneBounds (StructureLane::scaleMode), "Scale");
        drawLaneBase (structureLaneBounds (StructureLane::chordProgression), "Chords");

        const auto drawBraceRegion = [&graphics, this] (juce::Rectangle<int> lane,
                                                        core::time::TickPosition start,
                                                        core::time::TickPosition end,
                                                        juce::String label,
                                                        juce::Colour colour,
                                                        bool selected)
        {
            auto bounds = juce::Rectangle<int> {
                tickToX (start),
                lane.getY() + 3,
                std::max (12, durationToWidth (end - start)),
                lane.getHeight() - 6
            }.getIntersection (lane.reduced (2, 2));

            if (bounds.isEmpty())
                return;

            graphics.setColour (colour.withAlpha (0.72f));
            graphics.fillRoundedRectangle (bounds.toFloat(), 4.0f);
            graphics.setColour (juce::Colours::black.withAlpha (0.35f));
            graphics.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
            graphics.setColour (juce::Colours::white.withAlpha (0.62f));
            graphics.fillRect (bounds.withWidth (4).reduced (1, 4));
            graphics.fillRect (bounds.withLeft (bounds.getRight() - 4).reduced (1, 4));
            graphics.setColour (juce::Colours::black.withAlpha (0.70f));
            graphics.setFont (juce::FontOptions { 11.5f, juce::Font::bold });
            graphics.drawText (label, bounds.reduced (8, 1), juce::Justification::centredLeft);

            if (selected)
            {
                graphics.setColour (juce::Colours::white.withAlpha (0.88f));
                graphics.drawRoundedRectangle (bounds.toFloat().reduced (1.0f), 4.0f, 2.0f);
            }
        };

    const auto tempoLane = structureLaneBounds (StructureLane::tempo);
    const auto& tempoNodes = appServices_.project().tempoMap().nodes();
    for (std::size_t index = 0; index < tempoNodes.size(); ++index)
    {
        const auto& node = tempoNodes[index];
        const auto x = tickToX (node.position);
        const auto y = tempoLane.getCentreY();

        if (index + 1 < tempoNodes.size())
        {
            const auto nextX = tickToX (tempoNodes[index + 1].position);
            graphics.setColour (tempoColour.withAlpha (0.42f));
            graphics.drawLine (static_cast<float> (x),
                               static_cast<float> (y),
                               static_cast<float> (nextX),
                               static_cast<float> (tempoLane.getCentreY() - 2),
                               1.4f);
        }

        graphics.setColour (tempoColour);
        graphics.fillEllipse (static_cast<float> (x - 5), static_cast<float> (y - 5), 10.0f, 10.0f);
        graphics.setColour (textColour);
        graphics.setFont (juce::FontOptions { 11.0f });
        graphics.drawText (juce::String (node.tempo.bpm(), 0), x + 7, tempoLane.getY(), 48, tempoLane.getHeight(), juce::Justification::centredLeft);
    }

    const auto meterLane = structureLaneBounds (StructureLane::timeSignature);
    for (const auto& marker : appServices_.project().timeSignatureMap().markers())
    {
        const auto x = tickToX (marker.position);
        auto markerBounds = juce::Rectangle<int> { x, meterLane.getY() + 3, 48, meterLane.getHeight() - 6 }.getIntersection (meterLane.reduced (2, 2));
        if (markerBounds.isEmpty())
            continue;

        graphics.setColour (meterColour.withAlpha (0.75f));
        graphics.fillRoundedRectangle (markerBounds.toFloat(), 4.0f);
        graphics.setColour (juce::Colours::black.withAlpha (0.70f));
        graphics.setFont (juce::FontOptions { 11.5f, juce::Font::bold });
        graphics.drawText (timeSignatureText (marker.timeSignature), markerBounds, juce::Justification::centred);
    }

    const auto keyLane = structureLaneBounds (StructureLane::keyCenter);
    if (appServices_.project().musicalStructure().keyCenterRegions().empty())
    {
        auto defaultStart = core::time::TickPosition {};
        auto defaultEnd = core::time::TickPosition {} + barSpanDuration (timeSignatureMap, core::time::TickPosition {}, 8);
        if (structureDragState_.has_value()
            && structureDragState_->selection.kind == StructureRegionKind::keyCenter
            && structureDragState_->selection.index < 0)
        {
            defaultStart = structureDragState_->previewStart;
            defaultEnd = structureDragState_->previewEnd;
        }

        drawBraceRegion (keyLane,
                          defaultStart,
                          defaultEnd,
                          pitchClassName (appServices_.project().musicalStructure().defaultKeyCenter()),
                          keyRegionColour.withAlpha (0.55f),
                          isStructureRegionSelected (StructureRegionSelection { StructureRegionKind::keyCenter, -1 }));
    }

    for (int index = 0; index < static_cast<int> (appServices_.project().musicalStructure().keyCenterRegions().size()); ++index)
    {
        const auto& region = appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (index)];
        auto start = region.start();
        auto end = region.end();
        if (structureDragState_.has_value()
            && structureDragState_->selection.kind == StructureRegionKind::keyCenter
            && structureDragState_->selection.index == index)
        {
            start = structureDragState_->previewStart;
            end = structureDragState_->previewEnd;
        }

        drawBraceRegion (keyLane,
                          start,
                          end,
                          pitchClassName (region.pitchClass()),
                          keyRegionColour,
                          isStructureRegionSelected (StructureRegionSelection { StructureRegionKind::keyCenter, index }));
    }

    const auto scaleLane = structureLaneBounds (StructureLane::scaleMode);
    if (appServices_.project().musicalStructure().scaleModeRegions().empty())
    {
        auto defaultStart = core::time::TickPosition {};
        auto defaultEnd = core::time::TickPosition {} + barSpanDuration (timeSignatureMap, core::time::TickPosition {}, 8);
        if (structureDragState_.has_value()
            && structureDragState_->selection.kind == StructureRegionKind::scaleMode
            && structureDragState_->selection.index < 0)
        {
            defaultStart = structureDragState_->previewStart;
            defaultEnd = structureDragState_->previewEnd;
        }

        drawBraceRegion (scaleLane,
                          defaultStart,
                          defaultEnd,
                          appServices_.project().musicalStructure().defaultScaleDefinitionName(),
                          scaleRegionColour.withAlpha (0.55f),
                          isStructureRegionSelected (StructureRegionSelection { StructureRegionKind::scaleMode, -1 }));
    }

    for (int index = 0; index < static_cast<int> (appServices_.project().musicalStructure().scaleModeRegions().size()); ++index)
    {
        const auto& region = appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (index)];
        auto start = region.start();
        auto end = region.end();
        if (structureDragState_.has_value()
            && structureDragState_->selection.kind == StructureRegionKind::scaleMode
            && structureDragState_->selection.index == index)
        {
            start = structureDragState_->previewStart;
            end = structureDragState_->previewEnd;
        }

        drawBraceRegion (scaleLane,
                          start,
                          end,
                          region.scaleDefinitionName(),
                          scaleRegionColour,
                          isStructureRegionSelected (StructureRegionSelection { StructureRegionKind::scaleMode, index }));
    }

    const auto chordLane = structureLaneBounds (StructureLane::chordProgression);
    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TimelineComponent::paint chord-regions" };
        const auto& chordRegions = appServices_.project().musicalStructure().chordRegions();
        std::optional<core::music_theory::ScaleLibrary> scaleLibrary;

        for (int index = 0; index < static_cast<int> (chordRegions.size()); ++index)
        {
            const auto& region = chordRegions[static_cast<std::size_t> (index)];
            auto start = region.start();
            auto end = region.end();
            if (structureDragState_.has_value()
                && structureDragState_->selection.kind == StructureRegionKind::chordProgression
                && structureDragState_->selection.index == index)
            {
                start = structureDragState_->previewStart;
                end = structureDragState_->previewEnd;
            }

            if (! scaleLibrary.has_value())
                scaleLibrary.emplace (scaleLibraryForProject());

            drawBraceRegion (chordLane,
                              start,
                              end,
                              region.chordName(),
                              core::sequencing::isBorrowedChordRegion (appServices_.project().musicalStructure(), region, *scaleLibrary)
                                  ? borrowedChordRegionColour
                                  : chordRegionColour,
                              isStructureRegionSelected (StructureRegionSelection { StructureRegionKind::chordProgression, index }));
        }
    }

    if (scaleDropPreviewTick_.has_value())
    {
        auto preview = juce::Rectangle<int> {
            tickToX (*scaleDropPreviewTick_),
            scaleLane.getY() + 2,
            durationToWidth (barSpanDuration (timeSignatureMap, *scaleDropPreviewTick_, defaultRegionBars)),
            scaleLane.getHeight() - 4
        }.getIntersection (scaleLane.reduced (2, 2));

        if (! preview.isEmpty())
        {
            graphics.setColour (scaleRegionColour.withAlpha (0.24f));
            graphics.fillRoundedRectangle (preview.toFloat(), 4.0f);
            graphics.setColour (scaleRegionColour);
            graphics.drawRoundedRectangle (preview.toFloat().reduced (0.5f), 4.0f, 1.5f);
        }
    }
    }

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TimelineComponent::paint tracks" };

        for (int trackIndex = 0; trackIndex < static_cast<int> (tracks.size()); ++trackIndex)
        {
            const auto& track = tracks[static_cast<std::size_t> (trackIndex)];
            const auto row = trackHeaderBoundsForIndex (trackIndex);
            const auto header = trackHeaderBoundsForIndex (trackIndex);
            const auto rowIsSelected = std::any_of (selectedClips_.begin(), selectedClips_.end(), [&track] (const auto& selection) {
                return selection.trackId == track.id();
            });

        graphics.setColour (rowIsSelected ? selectedTrackColour : trackColour);
        graphics.fillRoundedRectangle (row.toFloat(), 5.0f);
        graphics.setColour (gridColour);
        graphics.drawRoundedRectangle (row.toFloat().reduced (0.5f), 5.0f, 1.0f);

        for (const auto& line : gridLines)
        {
            if (row.contains (line.x, row.getCentreY()))
            {
                graphics.setColour (gridColourForStrength (line.strength));
                graphics.fillRect (line.x, row.getY(), 1, row.getHeight());
            }
        }

        graphics.setFont (juce::FontOptions { 12.0f });
        graphics.setColour (mutedTextColour);
        graphics.drawText (track.name(), header.reduced (10, 4).removeFromTop (18), juce::Justification::centredLeft);

        for (const auto& clip : track.clips())
        {
            auto clipStart = clip.startInProject();
            auto clipLength = clip.length();
            auto clipIsPreview = false;
            auto clipIsInvalid = false;
            auto previewLoopDuration = clip.loop().isEnabled() ? clip.loop().loopDuration() : core::time::TickDuration {};

            if (dragState_.has_value()
                && dragState_->selection.trackId == track.id()
                && dragState_->selection.clipId == clip.id()
                && dragState_->selection.kind == ClipKind::midi
                && (dragState_->previewTrackId.empty() || dragState_->previewTrackId == track.id()))
            {
                clipStart = dragState_->previewStart;
                clipLength = dragState_->previewLength;
                clipIsPreview = true;
                clipIsInvalid = dragState_->previewOverlaps;

            }

            const auto clipLeft = tickToX (clipStart);
            const auto clipWidth = std::max (18, durationToWidth (clipLength));
            auto clipBounds = juce::Rectangle<int> {
                clipLeft,
                header.getY() + 22,
                clipWidth,
                rowHeight - 32
            };

            clipBounds = clipBounds.getIntersection (header.reduced (3, 0));
            if (clipBounds.isEmpty())
                continue;

            const auto isSelected = isClipSelected (ClipSelection { track.id(), clip.id(), ClipKind::midi });

            graphics.setColour (clipIsInvalid ? invalidClipColour : (isSelected ? selectedClipColour : clipColour));
            graphics.fillRoundedRectangle (clipBounds.toFloat(), 4.0f);

            if (previewLoopDuration.ticks() > 0 && clipLength > previewLoopDuration)
            {
                for (auto start = previewLoopDuration.ticks(); start < clipLength.ticks(); start += previewLoopDuration.ticks())
                {
                    const auto dividerX = clipLeft + durationToWidth (core::time::TickDuration::fromTicks (start));
                    if (dividerX >= clipBounds.getX() && dividerX <= clipBounds.getRight())
                    {
                        graphics.setColour (juce::Colours::black.withAlpha (0.22f));
                        graphics.drawVerticalLine (dividerX, static_cast<float> (clipBounds.getY() + 2), static_cast<float> (clipBounds.getBottom() - 2));
                        graphics.setColour (juce::Colours::white.withAlpha (0.26f));
                        graphics.drawVerticalLine (dividerX + 1, static_cast<float> (clipBounds.getY() + 2), static_cast<float> (clipBounds.getBottom() - 2));
                    }
                }

                auto shadeIndex = 1;
                for (auto start = previewLoopDuration.ticks(); start < clipLength.ticks(); start += previewLoopDuration.ticks(), ++shadeIndex)
                {
                    const auto segmentStart = clipLeft + durationToWidth (core::time::TickDuration::fromTicks (start));
                    const auto segmentEnd = clipLeft
                        + durationToWidth (core::time::TickDuration::fromTicks (std::min (start + previewLoopDuration.ticks(), clipLength.ticks())));
                    auto segment = juce::Rectangle<int> {
                        segmentStart,
                        clipBounds.getY(),
                        std::max (0, segmentEnd - segmentStart),
                        clipBounds.getHeight()
                    }.getIntersection (clipBounds);

                    if (! segment.isEmpty() && (shadeIndex % 2) == 1)
                    {
                        graphics.setColour (juce::Colours::black.withAlpha (0.08f));
                        graphics.fillRect (segment.reduced (0, 1));
                    }
                }
            }

            graphics.setColour (juce::Colours::black.withAlpha (0.35f));
            graphics.drawRoundedRectangle (clipBounds.toFloat().reduced (0.5f), 4.0f, isSelected ? 2.0f : 1.0f);

            const auto handle = clipBounds.withLeft (clipBounds.getRight() - resizeHandleWidth);
            graphics.setColour (juce::Colours::white.withAlpha (clipIsPreview ? 0.50f : 0.34f));
            graphics.fillRect (handle.reduced (2, 7));

            graphics.setColour (juce::Colours::black.withAlpha (0.72f));
            graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
            graphics.drawText (clip.name(), clipBounds.reduced (8, 2), juce::Justification::centredLeft);

            graphics.setFont (juce::FontOptions { 10.5f });
            graphics.drawText (formatBarBeat (clipStart),
                               clipBounds.reduced (8, 4),
                               juce::Justification::centredRight);
        }

        if (dragState_.has_value()
            && dragState_->mode == DragMode::move
            && dragState_->selection.kind == ClipKind::midi
            && dragState_->previewTrackId == track.id()
            && dragState_->previewTrackId != dragState_->selection.trackId)
        {
            const auto* sourceTrack = appServices_.project().findTrackById (dragState_->selection.trackId);
            const auto* sourceClip = sourceTrack == nullptr ? nullptr : sourceTrack->findClipById (dragState_->selection.clipId);
            if (sourceClip != nullptr)
            {
                const auto clipLeft = tickToX (dragState_->previewStart);
                const auto clipWidth = std::max (18, durationToWidth (dragState_->previewLength));
                auto clipBounds = juce::Rectangle<int> {
                    clipLeft,
                    header.getY() + 22,
                    clipWidth,
                    rowHeight - 32
                }.getIntersection (header.reduced (3, 0));

                if (! clipBounds.isEmpty())
                {
                    graphics.setColour (dragState_->previewOverlaps ? invalidClipColour : selectedClipColour.withAlpha (0.82f));
                    graphics.fillRoundedRectangle (clipBounds.toFloat(), 4.0f);
                    graphics.setColour (juce::Colours::black.withAlpha (0.35f));
                    graphics.drawRoundedRectangle (clipBounds.toFloat().reduced (0.5f), 4.0f, 2.0f);

                    const auto handle = clipBounds.withLeft (clipBounds.getRight() - resizeHandleWidth);
                    graphics.setColour (juce::Colours::white.withAlpha (0.50f));
                    graphics.fillRect (handle.reduced (2, 7));

                    graphics.setColour (juce::Colours::black.withAlpha (0.72f));
                    graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
                    graphics.drawText (sourceClip->name(), clipBounds.reduced (8, 2), juce::Justification::centredLeft);

                    graphics.setFont (juce::FontOptions { 10.5f });
                    graphics.drawText (formatBarBeat (dragState_->previewStart),
                                       clipBounds.reduced (8, 4),
                                       juce::Justification::centredRight);
                }
            }
        }

        for (const auto& clip : track.audioClips())
        {
            auto clipStart = clip.startInProject();
            auto clipLength = clip.length();
            auto clipIsPreview = false;
            auto clipIsInvalid = false;

            if (dragState_.has_value()
                && dragState_->selection.trackId == track.id()
                && dragState_->selection.clipId == clip.id()
                && dragState_->selection.kind == ClipKind::audio)
            {
                clipStart = dragState_->previewStart;
                clipLength = dragState_->previewLength;
                clipIsPreview = true;
                clipIsInvalid = dragState_->previewOverlaps;
            }

            const auto clipLeft = tickToX (clipStart);
            const auto clipWidth = std::max (18, durationToWidth (clipLength));
            auto clipBounds = juce::Rectangle<int> {
                clipLeft,
                header.getY() + 22,
                clipWidth,
                rowHeight - 32
            };

            clipBounds = clipBounds.getIntersection (header.reduced (3, 0));
            if (clipBounds.isEmpty())
                continue;

            const auto isSelected = isClipSelected (ClipSelection { track.id(), clip.id(), ClipKind::audio });

            graphics.setColour (clipIsInvalid ? invalidClipColour : (isSelected ? selectedClipColour : audioClipColour));
            graphics.fillRoundedRectangle (clipBounds.toFloat(), 4.0f);

            if (clipBounds.getWidth() >= minimumAudioWaveformWidth)
            {
                waveformCache_.drawWaveform (graphics,
                                             clip,
                                             clipBounds,
                                             juce::Colours::black.withAlpha (0.64f),
                                             juce::Colours::black.withAlpha (0.52f));
            }
            else
            {
                graphics.setColour (juce::Colours::black.withAlpha (0.32f));
                graphics.drawLine (static_cast<float> (clipBounds.getX() + 4),
                                   static_cast<float> (clipBounds.getCentreY()),
                                   static_cast<float> (clipBounds.getRight() - 4),
                                   static_cast<float> (clipBounds.getCentreY()),
                                   1.0f);
            }

            graphics.setColour (juce::Colours::black.withAlpha (0.35f));
            graphics.drawRoundedRectangle (clipBounds.toFloat().reduced (0.5f), 4.0f, isSelected ? 2.0f : 1.0f);

            if (clipBounds.getWidth() > resizeHandleWidth + 16)
            {
                const auto handle = clipBounds.withLeft (clipBounds.getRight() - resizeHandleWidth);
                graphics.setColour (juce::Colours::white.withAlpha (clipIsPreview ? 0.50f : 0.34f));
                graphics.fillRect (handle.reduced (2, 7));
            }

            if (clipBounds.getWidth() >= minimumAudioLabelWidth)
            {
                graphics.setColour (juce::Colours::black.withAlpha (0.76f));
                graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
                graphics.drawText (clip.name(), clipBounds.reduced (8, 2), juce::Justification::centredLeft);
            }

            if (clipBounds.getWidth() >= minimumAudioBeatLabelWidth)
            {
                graphics.setFont (juce::FontOptions { 10.5f });
                graphics.drawText (formatBarBeat (clipStart),
                                   clipBounds.reduced (8, 4),
                                   juce::Justification::centredRight);
            }
        }

        {
            core::diagnostics::ScopedPerformanceTimer automationTimer {
                "TimelineComponent::paint automation-lanes track=" + track.id()
            };

            auto visibleLaneIndex = 0;
            for (const auto& lane : track.automationLanes())
            {
                if (! lane.visible())
                    continue;

                const auto laneHit = AutomationLaneHit { trackIndex, visibleLaneIndex++, lane.target() };
                const auto laneBounds = automationLaneBoundsForHit (laneHit);
                if (laneBounds.isEmpty())
                    continue;

                graphics.setColour (laneColour.withAlpha (0.86f));
                graphics.fillRoundedRectangle (laneBounds.toFloat(), 4.0f);
                graphics.setColour (gridColour.withAlpha (0.78f));
                graphics.drawRoundedRectangle (laneBounds.toFloat().reduced (0.5f), 4.0f, 1.0f);

                auto laneBody = laneBounds;
                auto labelBounds = laneBody.removeFromLeft (96).reduced (8, 0);
                graphics.setColour (automationColour);
                graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
                graphics.drawText (automationTargetLabelFor (lane.target()), labelBounds, juce::Justification::centredLeft);

                const auto graphBounds = laneBody.reduced (5, 6);
                if (graphBounds.getWidth() <= 1 || graphBounds.getHeight() <= 1)
                    continue;

                for (const auto& line : gridLines)
                {
                    if (graphBounds.contains (line.x, graphBounds.getCentreY()))
                    {
                        graphics.setColour (gridColourForStrength (line.strength).withAlpha (0.55f));
                        graphics.fillRect (line.x, graphBounds.getY(), 1, graphBounds.getHeight());
                    }
                }

                const auto* pointsToDraw = &lane.curve().points();
                std::vector<core::sequencing::AutomationPoint> previewPoints;
                if (automationDragState_.has_value()
                    && automationDragState_->selection.trackId == track.id()
                    && automationDragState_->selection.target == lane.target())
                {
                    previewPoints = lane.curve().points();
                    const auto match = std::find_if (previewPoints.begin(), previewPoints.end(), [this] (const auto& point) {
                        return point.position == automationDragState_->selection.position;
                    });

                    if (match != previewPoints.end())
                    {
                        match->position = automationDragState_->previewPosition;
                        match->normalizedValue = automationDragState_->previewValue;
                        std::stable_sort (previewPoints.begin(), previewPoints.end(), [] (const auto& lhs, const auto& rhs) {
                            return lhs.position < rhs.position;
                        });
                    }

                    pointsToDraw = &previewPoints;
                }

                const auto& points = *pointsToDraw;
                const auto defaultValue = core::sequencing::defaultAutomationValueForTarget (appServices_.project(), lane.target());
                const auto visibleEndTick = timelineEndTick();
                const auto firstPointAfterVisibleEnd = std::upper_bound (
                    points.begin(),
                    points.end(),
                    visibleEndTick,
                    [] (auto position, const auto& point)
                    {
                        return position < point.position;
                    });
                const auto visiblePointCount = static_cast<std::size_t> (std::distance (points.begin(), firstPointAfterVisibleEnd));

                const auto drawValueLine = [&graphics, graphBounds, this] (core::time::TickPosition from,
                                                                           double fromValue,
                                                                           core::time::TickPosition to,
                                                                           double toValue,
                                                                           float thickness)
                {
                    const auto x1 = std::clamp (tickToX (from), graphBounds.getX(), graphBounds.getRight());
                    const auto x2 = std::clamp (tickToX (to), graphBounds.getX(), graphBounds.getRight());
                    const auto y1 = yForAutomationValue (graphBounds, fromValue);
                    const auto y2 = yForAutomationValue (graphBounds, toValue);
                    graphics.drawLine (static_cast<float> (x1),
                                       static_cast<float> (y1),
                                       static_cast<float> (x2),
                                       static_cast<float> (y2),
                                       thickness);
                };

                graphics.setColour (automationColour.withAlpha (0.68f));
                if (points.empty())
                {
                    const auto y = yForAutomationValue (graphBounds, defaultValue);
                    graphics.drawHorizontalLine (y, static_cast<float> (graphBounds.getX()), static_cast<float> (graphBounds.getRight()));
                }
                else if (firstPointAfterVisibleEnd == points.begin())
                {
                    const auto y = yForAutomationValue (graphBounds, defaultValue);
                    graphics.drawHorizontalLine (y, static_cast<float> (graphBounds.getX()), static_cast<float> (graphBounds.getRight()));
                }
                else
                {
                    drawValueLine (core::time::TickPosition {}, defaultValue, points.front().position, defaultValue, 1.4f);
                    for (std::size_t index = 0; index < visiblePointCount; ++index)
                    {
                        const auto& point = points[index];
                        if (index + 1 >= points.size())
                        {
                            drawValueLine (point.position, point.normalizedValue, visibleEndTick, point.normalizedValue, 1.4f);
                            continue;
                        }

                        const auto& next = points[index + 1];
                        const auto nextWithinVisibleRange = next.position <= visibleEndTick;
                        const auto segmentEnd = nextWithinVisibleRange ? next.position : visibleEndTick;
                        if (point.interpolationToNext == core::sequencing::AutomationInterpolation::hold)
                        {
                            drawValueLine (point.position, point.normalizedValue, segmentEnd, point.normalizedValue, 1.4f);
                            if (nextWithinVisibleRange)
                                drawValueLine (next.position, point.normalizedValue, next.position, next.normalizedValue, 1.0f);
                        }
                        else
                        {
                            auto segmentEndValue = next.normalizedValue;
                            if (! nextWithinVisibleRange)
                            {
                                const auto span = static_cast<double> ((next.position - point.position).ticks());
                                const auto offset = static_cast<double> ((visibleEndTick - point.position).ticks());
                                const auto alpha = span <= 0.0 ? 0.0 : std::clamp (offset / span, 0.0, 1.0);
                                segmentEndValue = point.normalizedValue + ((next.normalizedValue - point.normalizedValue) * alpha);
                            }

                            drawValueLine (point.position, point.normalizedValue, segmentEnd, segmentEndValue, 1.6f);
                        }
                    }
                }

                const auto densePointMarkers = visiblePointCount > 64;
                for (auto point = points.begin(); point != firstPointAfterVisibleEnd; ++point)
                {
                    const auto pointX = tickToX (point->position);
                    if (pointX < graphBounds.getX() - 6 || pointX > graphBounds.getRight() + 6)
                        continue;

                    const auto pointY = yForAutomationValue (graphBounds, point->normalizedValue);
                    const auto isSelectedPoint = selectedAutomationPoint_.has_value()
                        && selectedAutomationPoint_->trackId == track.id()
                        && selectedAutomationPoint_->target == lane.target()
                        && selectedAutomationPoint_->position == point->position;

                    graphics.setColour (isSelectedPoint ? textColour : automationPointColour);
                    if (densePointMarkers && ! isSelectedPoint)
                    {
                        graphics.fillRect (pointX - 2, pointY - 2, 4, 4);
                    }
                    else
                    {
                        graphics.fillEllipse (static_cast<float> (pointX - 4), static_cast<float> (pointY - 4), 8.0f, 8.0f);
                        graphics.setColour (juce::Colours::black.withAlpha (0.62f));
                        graphics.drawEllipse (static_cast<float> (pointX - 4), static_cast<float> (pointY - 4), 8.0f, 8.0f, isSelectedPoint ? 2.0f : 1.0f);
                    }
                }
            }
        }
    }
    }

    if (tracks.empty())
    {
        graphics.setColour (mutedTextColour);
        graphics.setFont (juce::FontOptions { 14.0f });
        graphics.drawText ("No tracks - add a track or drop a plugin, audio file, or MIDI file",
                           getLocalBounds().withTrimmedTop (rulerHeight + structureHeight),
                           juce::Justification::centred);
    }

    if (trackCreationDropPreview_)
    {
        const auto trackCount = static_cast<int> (tracks.size());
        const auto top = trackCount == 0
            ? rulerHeight + structureHeight + trackTopPadding
            : trackRowForIndex (trackCount - 1).getBottom() + rowGap;
        auto preview = juce::Rectangle<int> {
            horizontalPadding,
            top,
            std::max (0, getWidth() - (horizontalPadding * 2)),
            rowHeight
        }.getIntersection (getLocalBounds().reduced (horizontalPadding, 0));

        if (! preview.isEmpty())
        {
            graphics.setColour (clipColour.withAlpha (0.12f));
            graphics.fillRoundedRectangle (preview.toFloat(), 5.0f);
            graphics.setColour (clipColour.withAlpha (0.85f));
            graphics.drawRoundedRectangle (preview.toFloat().reduced (0.5f), 5.0f, 1.5f);
        }
    }

    if (! feedbackText_.empty())
    {
        auto feedbackBounds = getLocalBounds().removeFromBottom (26).reduced (10, 0);
        graphics.setColour (warningColour);
        graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
        graphics.drawText (feedbackText_, feedbackBounds, juce::Justification::centredLeft);
    }

    if (marqueeState_.has_value())
    {
        const auto bounds = marqueeBounds();
        graphics.setColour (selectedClipColour.withAlpha (0.14f));
        graphics.fillRect (bounds);
        graphics.setColour (selectedClipColour.withAlpha (0.82f));
        graphics.drawRect (bounds, 1);
    }

    const auto playheadX = tickToX (playheadTick_);
    playheadPixelX_ = playheadX;
    playheadPixelValid_ = true;
    graphics.setColour (clipColour);
    graphics.drawVerticalLine (playheadX, 0.0f, static_cast<float> (getHeight()));
    juce::Path marker;
    marker.startNewSubPath (static_cast<float> (playheadX - 5), 1.0f);
    marker.lineTo (static_cast<float> (playheadX + 5), 1.0f);
    marker.lineTo (static_cast<float> (playheadX), 10.0f);
    marker.closeSubPath();
    graphics.fillPath (marker);
}

void TimelineComponent::resized()
{
    playheadPixelValid_ = false;
    auto buttons = getLocalBounds().removeFromTop (rulerHeight).removeFromRight (230).reduced (8, 5);
    addTrackButton_.setBounds (buttons.removeFromRight (78));
    buttons.removeFromRight (8);
    globalizeChordButton_.setBounds (buttons);
}

void TimelineComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    feedbackText_.clear();
    structureDragState_.reset();
    dragState_.reset();
    draggingPlayhead_ = false;

    if (event.mods.isPopupMenu() && isEmptyTrackCreationArea (event.position.roundToInt()))
    {
        showInsertTrackMenu();
        repaint();
        return;
    }

    if (event.y < rulerHeight)
    {
        selectedStructureRegion_.reset();
        selectedAutomationPoint_.reset();
        draggingPlayhead_ = true;
        playheadTick_ = xToSnappedTick (event.x);
        if (onPlayheadMoved)
            onPlayheadMoved (playheadTick_);
        repaint();
        return;
    }

    if (auto laneHit = automationLaneAt (event.position))
    {
        focusedField_ = FocusedField::automation;
        selectedClip_.reset();
        selectedClips_.clear();
        selectedStructureRegion_.reset();
        selectedStructureRegions_.clear();

        const auto& track = appServices_.project().tracks()[static_cast<std::size_t> (laneHit->trackIndex)];
        appServices_.setSelectedTrack (track.id());
        if (event.mods.isPopupMenu())
        {
            showTrackAutomationMenu (track.id());
            repaint();
            return;
        }

        if (auto pointSelection = automationPointAt (event.position))
        {
            selectedAutomationPoint_ = pointSelection;
            const auto* lane = track.findAutomationLane (pointSelection->target);
            if (lane != nullptr)
            {
                const auto match = std::find_if (lane->curve().points().begin(),
                                                 lane->curve().points().end(),
                                                 [&pointSelection] (const auto& point)
                                                 {
                                                     return point.position == pointSelection->position;
                                                 });

                if (match != lane->curve().points().end())
                {
                    automationDragState_ = AutomationDragState {
                        *pointSelection,
                        pointSelection->position,
                        match->normalizedValue
                    };
                }
            }

            repaint();
            return;
        }

        auto laneBody = automationLaneBoundsForHit (*laneHit);
        laneBody.removeFromLeft (96);
        const auto graphBounds = laneBody.reduced (5, 6);
        const auto position = xToSnappedTick (event.x);
        const auto value = automationValueForY (graphBounds, event.y);
        if (const auto* existingLane = track.findAutomationLane (laneHit->target))
        {
            auto curve = existingLane->curve();
            const auto duplicate = std::find_if (curve.points().begin(), curve.points().end(), [position] (const auto& point) {
                return point.position == position;
            });
            if (duplicate != curve.points().end())
                curve.removePointAt (position);

            curve.addPoint (core::sequencing::AutomationPoint { position, value });
            auto lane = core::sequencing::AutomationLane { laneHit->target, curve };
            lane.setVisible (true);
            setAutomationLaneForTrack (track.id(), std::move (lane));
            selectedAutomationPoint_ = AutomationPointSelection { track.id(), laneHit->target, position };
        }

        repaint();
        return;
    }

    if (const auto lane = structureLaneAt (event.position); lane != StructureLane::none)
    {
        focusedField_ = FocusedField::structure;
        selectedClip_.reset();
        selectedClips_.clear();
        selectedAutomationPoint_.reset();
        if (const auto selection = structureRegionAt (event.position))
        {
            selectedStructureRegion_ = selection;
            if (! event.mods.isShiftDown() && ! event.mods.isCommandDown() && ! event.mods.isCtrlDown())
                selectedStructureRegions_.clear();

            if (! isStructureRegionSelected (*selection))
                selectedStructureRegions_.push_back (*selection);

            if (event.mods.isPopupMenu())
            {
                if (selection->kind == StructureRegionKind::chordProgression)
                    showChordRegionPopup (selection->index);

                repaint();
                return;
            }

            if (selection->kind == StructureRegionKind::keyCenter)
            {
                const auto start = selection->index < 0 ? core::time::TickPosition {}
                                                        : appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (selection->index)].start();
                const auto end = selection->index < 0
                    ? core::time::TickPosition {} + barSpanDuration (appServices_.project().timeSignatureMap(), core::time::TickPosition {}, 8)
                    : appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (selection->index)].end();
                const auto startX = tickToX (start);
                const auto endX = tickToX (end);
                const auto mode = std::abs (event.x - startX) <= resizeHandleWidth + 2 ? DragMode::resizeLeft
                    : (std::abs (event.x - endX) <= resizeHandleWidth + 2 ? DragMode::resizeRight : DragMode::move);

                structureDragState_ = StructureDragState { *selection, mode, event.x, start, end, start, end };
            }
            else if (selection->kind == StructureRegionKind::scaleMode)
            {
                const auto start = selection->index < 0 ? core::time::TickPosition {}
                                                        : appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (selection->index)].start();
                const auto end = selection->index < 0
                    ? core::time::TickPosition {} + barSpanDuration (appServices_.project().timeSignatureMap(), core::time::TickPosition {}, 8)
                    : appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (selection->index)].end();
                const auto startX = tickToX (start);
                const auto endX = tickToX (end);
                const auto mode = std::abs (event.x - startX) <= resizeHandleWidth + 2 ? DragMode::resizeLeft
                    : (std::abs (event.x - endX) <= resizeHandleWidth + 2 ? DragMode::resizeRight : DragMode::move);

                structureDragState_ = StructureDragState { *selection, mode, event.x, start, end, start, end };
            }
            else
            {
                const auto& region = appServices_.project().musicalStructure().chordRegions()[static_cast<std::size_t> (selection->index)];
                const auto startX = tickToX (region.start());
                const auto endX = tickToX (region.end());
                const auto mode = std::abs (event.x - startX) <= resizeHandleWidth + 2 ? DragMode::resizeLeft
                    : (std::abs (event.x - endX) <= resizeHandleWidth + 2 ? DragMode::resizeRight : DragMode::move);

                structureDragState_ = StructureDragState { *selection, mode, event.x, region.start(), region.end(), region.start(), region.end() };
            }
        }
        else
        {
            selectedStructureRegion_.reset();
            if (! event.mods.isShiftDown() && ! event.mods.isCommandDown() && ! event.mods.isCtrlDown())
                selectedStructureRegions_.clear();

            marqueeState_ = MarqueeState { event.position.roundToInt(), event.position.roundToInt(), FocusedField::structure };
        }

        repaint();
        return;
    }

    if (event.mods.isPopupMenu())
    {
        const auto trackIndex = trackIndexAt (event.position);
        if (trackIndex >= 0
            && trackIndex < static_cast<int> (appServices_.project().tracks().size())
            && ! clipAt (event.position).has_value())
        {
            showTrackAutomationMenu (appServices_.project().tracks()[static_cast<std::size_t> (trackIndex)].id());
            repaint();
            return;
        }
    }

    selectedStructureRegion_.reset();
    selectedStructureRegions_.clear();
    selectedAutomationPoint_.reset();
    focusedField_ = FocusedField::clips;
    const auto trackIndex = trackIndexAt (event.position);
    const auto& tracks = appServices_.project().tracks();
    if (trackIndex >= 0 && trackIndex < static_cast<int> (tracks.size()))
        appServices_.setSelectedTrack (tracks[static_cast<std::size_t> (trackIndex)].id());

    selectedClip_ = clipAt (event.position);

    if (selectedClip_.has_value())
    {
        if (! event.mods.isShiftDown() && ! event.mods.isCommandDown() && ! event.mods.isCtrlDown())
            selectedClips_.clear();

        if (! isClipSelected (*selectedClip_))
            selectedClips_.push_back (*selectedClip_);

        auto clipStart = core::time::TickPosition {};
        auto clipLength = core::time::TickDuration {};
        auto hasClip = false;
        if (selectedClip_->kind == ClipKind::audio)
        {
            if (const auto* clip = findAudioClip (*selectedClip_))
            {
                clipStart = clip->startInProject();
                clipLength = clip->length();
                hasClip = true;
            }
        }
        else if (const auto* clip = findClip (*selectedClip_))
        {
            clipStart = clip->startInProject();
            clipLength = clip->length();
            hasClip = true;
        }

        if (hasClip)
        {
            const auto clipRight = tickToX (clipStart) + durationToWidth (clipLength);
            const auto mode = std::abs (event.x - clipRight) <= resizeHandleWidth + 2 ? DragMode::resizeRight : DragMode::move;

            dragState_ = DragState {
                *selectedClip_,
                mode,
                event.x,
                clipStart,
                clipLength,
                clipStart,
                clipLength,
                selectedClip_->trackId,
                false,
                false
            };
        }
    }
    else
    {
        if (! event.mods.isShiftDown() && ! event.mods.isCommandDown() && ! event.mods.isCtrlDown())
            selectedClips_.clear();

        marqueeState_ = MarqueeState { event.position.roundToInt(), event.position.roundToInt(), FocusedField::clips };
    }

    repaint();
}

void TimelineComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingPlayhead_)
    {
        playheadTick_ = xToSnappedTick (event.x);
        if (onPlayheadMoved)
            onPlayheadMoved (playheadTick_);

        repaint();
        return;
    }

    if (marqueeState_.has_value())
    {
        marqueeState_->current = event.position.roundToInt();
        repaint();
        return;
    }

    if (structureDragState_.has_value())
    {
        auto& drag = *structureDragState_;
        const auto deltaTicks = xToSnappedTick (tickToX (drag.originalStart) + (event.x - drag.mouseStartX)) - drag.originalStart;
        const auto minimumLength = core::time::TickDuration::fromTicks (ticksPerBeat);

        if (drag.mode == DragMode::move)
        {
            const auto length = drag.originalEnd - drag.originalStart;
            drag.previewStart = snappedPosition (std::max<std::int64_t> (0, (drag.originalStart + deltaTicks).ticks()));
            drag.previewEnd = drag.previewStart + length;
        }
        else if (drag.mode == DragMode::resizeLeft)
        {
            const auto proposed = snappedPosition (std::max<std::int64_t> (0, (drag.originalStart + deltaTicks).ticks()));
            drag.previewStart = proposed <= drag.previewEnd - minimumLength ? proposed : drag.previewEnd - minimumLength;
        }
        else if (drag.mode == DragMode::resizeRight)
        {
            const auto proposed = xToSnappedTick (event.x);
            drag.previewEnd = proposed >= drag.previewStart + minimumLength ? proposed : drag.previewStart + minimumLength;
        }

        repaint();
        return;
    }

    if (automationDragState_.has_value())
    {
        auto& drag = *automationDragState_;
        drag.previewPosition = xToSnappedTick (event.x);

        const auto& tracks = appServices_.project().tracks();
        for (int trackIndex = 0; trackIndex < static_cast<int> (tracks.size()); ++trackIndex)
        {
            if (tracks[static_cast<std::size_t> (trackIndex)].id() != drag.selection.trackId)
                continue;

            auto visibleLaneIndex = 0;
            for (const auto& lane : tracks[static_cast<std::size_t> (trackIndex)].automationLanes())
            {
                if (! lane.visible())
                    continue;

                if (lane.target() == drag.selection.target)
                {
                    auto laneBody = automationLaneBoundsForHit (AutomationLaneHit { trackIndex, visibleLaneIndex, lane.target() });
                    laneBody.removeFromLeft (96);
                    drag.previewValue = automationValueForY (laneBody.reduced (5, 6), event.y);
                    repaint();
                    return;
                }

                ++visibleLaneIndex;
            }
        }

        repaint();
        return;
    }

    if (! dragState_.has_value())
        return;

    auto& drag = *dragState_;

    if (drag.mode == DragMode::move)
    {
        drag.previewStart = xToSnappedTick (tickToX (drag.originalStart) + (event.x - drag.mouseStartX));
        drag.previewLength = drag.originalLength;
        drag.previewTrackId = drag.selection.trackId;
        drag.previewTrackInvalid = false;

        if (drag.selection.kind == ClipKind::midi)
        {
            const auto targetTrackIndex = trackIndexAt (event.position);
            const auto& tracks = appServices_.project().tracks();
            if (targetTrackIndex >= 0 && targetTrackIndex < static_cast<int> (tracks.size()))
            {
                const auto& targetTrack = tracks[static_cast<std::size_t> (targetTrackIndex)];
                drag.previewTrackId = targetTrack.id();
                drag.previewTrackInvalid = ! core::sequencing::trackTypeCanOwnMidiClips (targetTrack.type());
            }
        }
    }
    else if (drag.mode == DragMode::resizeRight)
    {
        const auto pixelsPerBeat = pixelsPerQuarter();
        const auto width = std::max (pixelsPerBeat, static_cast<double> (durationToWidth (drag.originalLength) + (event.x - drag.mouseStartX)));
        const auto ticks = static_cast<std::int64_t> (std::llround ((width / pixelsPerBeat) * ticksPerBeat));
        drag.previewStart = drag.originalStart;
        drag.previewLength = snappedDuration (ticks);
    }

    const auto overlapTrackId = drag.previewTrackId.empty() ? drag.selection.trackId : drag.previewTrackId;
    const auto ignoredClipId = overlapTrackId == drag.selection.trackId ? drag.selection.clipId : std::string {};
    drag.previewOverlaps = drag.previewTrackInvalid || wouldOverlap (overlapTrackId, ignoredClipId, drag.previewStart, drag.previewLength);
    ensureTimelineFits (drag.previewStart + drag.previewLength);
    repaint();
}

void TimelineComponent::mouseUp (const juce::MouseEvent&)
{
    if (draggingPlayhead_)
    {
        draggingPlayhead_ = false;
        return;
    }

    if (marqueeState_.has_value())
    {
        const auto marquee = *marqueeState_;
        marqueeState_.reset();

        if (marquee.field == FocusedField::structure)
            selectStructureRegionsInMarquee (juce::Rectangle<int>::leftTopRightBottom (
                std::min (marquee.start.x, marquee.current.x),
                std::min (marquee.start.y, marquee.current.y),
                std::max (marquee.start.x, marquee.current.x),
                std::max (marquee.start.y, marquee.current.y)));
        else
            selectClipsInMarquee (juce::Rectangle<int>::leftTopRightBottom (
                std::min (marquee.start.x, marquee.current.x),
                std::min (marquee.start.y, marquee.current.y),
                std::max (marquee.start.x, marquee.current.x),
                std::max (marquee.start.y, marquee.current.y)));

        repaint();
        return;
    }

    if (structureDragState_.has_value())
    {
        auto drag = *structureDragState_;
        structureDragState_.reset();
        commitStructureDrag (drag);
        return;
    }

    if (automationDragState_.has_value())
    {
        auto drag = *automationDragState_;
        automationDragState_.reset();
        commitAutomationPointDrag (drag);
        return;
    }

    if (! dragState_.has_value())
        return;

    auto drag = *dragState_;
    dragState_.reset();

    if (drag.previewOverlaps)
    {
        feedbackText_ = drag.previewTrackInvalid ? "MIDI clips can only be moved to MIDI tracks"
                                                 : "Clips cannot overlap on the same track";
        repaint();
        return;
    }

    if (drag.mode == DragMode::move)
    {
        const auto targetTrackId = drag.previewTrackId.empty() ? drag.selection.trackId : drag.previewTrackId;
        if (drag.selection.kind == ClipKind::midi && targetTrackId != drag.selection.trackId)
        {
            runCommand (std::make_unique<core::commands::MoveMidiClipToTrackCommand> (
                drag.selection.trackId,
                targetTrackId,
                drag.selection.clipId,
                drag.previewStart));
            const auto* targetTrack = appServices_.project().findTrackById (targetTrackId);
            if (targetTrack != nullptr && targetTrack->findClipById (drag.selection.clipId) != nullptr)
            {
                appServices_.setSelectedTrack (targetTrackId);
                selectedClip_ = ClipSelection { targetTrackId, drag.selection.clipId, ClipKind::midi };
                selectedClips_ = { *selectedClip_ };
            }
            return;
        }

        if (drag.previewStart != drag.originalStart)
        {
            runCommand (std::make_unique<core::commands::MoveClipCommand> (drag.selection.trackId, drag.selection.clipId, drag.previewStart));
            return;
        }
    }

    if (drag.mode == DragMode::resizeRight && drag.previewLength != drag.originalLength)
    {
        runCommand (std::make_unique<core::commands::ResizeClipCommand> (drag.selection.trackId, drag.selection.clipId, drag.previewLength));
        return;
    }

    repaint();
}

void TimelineComponent::mouseMove (const juce::MouseEvent& event)
{
    if (event.y < rulerHeight)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (const auto mode = resizeModeAtStructureRegionEdge (event.position))
    {
        setMouseCursor (*mode == DragMode::resizeLeft ? juce::MouseCursor::LeftEdgeResizeCursor
                                                      : juce::MouseCursor::RightEdgeResizeCursor);
        return;
    }

    if (automationPointAt (event.position).has_value())
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (const auto mode = resizeModeAtClipEdge (event.position))
    {
        setMouseCursor (*mode == DragMode::resizeLeft ? juce::MouseCursor::LeftEdgeResizeCursor
                                                      : juce::MouseCursor::RightEdgeResizeCursor);
        return;
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (! (event.mods.isCommandDown() || event.mods.isCtrlDown()))
    {
        juce::Component::mouseWheelMove (event, wheel);
        return;
    }

    const auto lane = structureLaneAt (event.position);
    if (lane != StructureLane::none && lane != StructureLane::tempo)
        return;

    zoomHorizontally (wheel.deltaY, event.x);
}

void TimelineComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (const auto lane = structureLaneAt (event.position); lane != StructureLane::none)
    {
        handleStructureLaneDoubleClick (lane, xToSnappedTick (event.x), structureRegionAt (event.position));
        return;
    }

    if (auto selection = clipAt (event.position))
    {
        selectedClip_ = selection;
        if (selection->kind == ClipKind::midi && onClipOpened)
            onClipOpened (selection->trackId, selection->clipId);
        else if (selection->kind == ClipKind::audio)
            feedbackText_ = "Audio clip selected";

        repaint();
        return;
    }

    if (automationLaneAt (event.position).has_value())
        return;

    const auto trackIndex = trackIndexAt (event.position);
    const auto& tracks = appServices_.project().tracks();
    if (trackIndex < 0 || trackIndex >= static_cast<int> (tracks.size()))
        return;

    addClipAt (tracks[static_cast<std::size_t> (trackIndex)].id(), xToSnappedTick (event.x));
}

bool TimelineComponent::keyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto commandDown = modifiers.isCommandDown() || modifiers.isCtrlDown();

    if (commandDown && keyMatchesCharacter (key, 'z'))
    {
        selectedClip_.reset();
        selectedStructureRegion_.reset();
        selectedAutomationPoint_.reset();
        selectedClips_.clear();
        selectedStructureRegions_.clear();
        feedbackText_.clear();
        const auto result = modifiers.isShiftDown() ? appServices_.commandStack().redo() : appServices_.commandStack().undo();
        if (result.failed())
            feedbackText_ = result.error();

        refreshAfterEdit();
        return true;
    }

    if (commandDown && keyMatchesCharacter (key, 'y'))
    {
        selectedClip_.reset();
        selectedStructureRegion_.reset();
        selectedAutomationPoint_.reset();
        selectedClips_.clear();
        selectedStructureRegions_.clear();
        feedbackText_.clear();
        const auto result = appServices_.commandStack().redo();
        if (result.failed())
            feedbackText_ = result.error();

        refreshAfterEdit();
        return true;
    }

    if (commandDown && keyMatchesCharacter (key, 'a'))
        return selectAllInFocusedField();

    if (commandDown && keyMatchesCharacter (key, 'c'))
    {
        copySelection();
        return true;
    }

    if (commandDown && keyMatchesCharacter (key, 'v'))
    {
        pasteSelection();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (selectedAutomationPoint_.has_value())
        {
            deleteSelectedAutomationPoint();
            return true;
        }

        if (selectedStructureRegion_.has_value())
        {
            deleteSelectedStructureRegion();
            return true;
        }

        deleteSelectedClip();
        return true;
    }

    return false;
}

bool TimelineComponent::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (isScaleDragDescription (details.description))
        return true;

    const auto canCreateTrack = pluginDragPayloadFromVar (details.description).has_value()
        || projectFileDragPayloadFromVar (details.description).has_value();
    return canCreateTrack && isEmptyTrackCreationArea (details.localPosition);
}

void TimelineComponent::itemDragMove (const juce::DragAndDropTarget::SourceDetails& details)
{
    trackCreationDropPreview_ = false;

    if (! isScaleDragDescription (details.description)
        || ! structureLaneBounds (StructureLane::scaleMode).contains (details.localPosition))
    {
        scaleDropPreviewTick_.reset();
        if ((pluginDragPayloadFromVar (details.description).has_value()
             || projectFileDragPayloadFromVar (details.description).has_value())
            && isEmptyTrackCreationArea (details.localPosition))
        {
            trackCreationDropPreview_ = true;
        }

        repaint();
        return;
    }

    scaleDropPreviewTick_ = xToSnappedTick (details.localPosition.x);
    repaint();
}

void TimelineComponent::itemDragExit (const juce::DragAndDropTarget::SourceDetails&)
{
    scaleDropPreviewTick_.reset();
    trackCreationDropPreview_ = false;
    repaint();
}

void TimelineComponent::itemDropped (const juce::DragAndDropTarget::SourceDetails& details)
{
    scaleDropPreviewTick_.reset();
    trackCreationDropPreview_ = false;

    if (isScaleDragDescription (details.description)
        && structureLaneBounds (StructureLane::scaleMode).contains (details.localPosition))
    {
        addOrReplaceScaleModeRegionFromPalette (
            scaleNameFromDragDescription (details.description),
            xToSnappedTick (details.localPosition.x));
        return;
    }

    if (isEmptyTrackCreationArea (details.localPosition))
    {
        createTrackFromPayload (details.description);
        refreshAfterEdit();
        return;
    }

    if (pluginDragPayloadFromVar (details.description).has_value()
        || projectFileDragPayloadFromVar (details.description).has_value())
    {
        appServices_.reportWarning ("Track creation failed: drop below the existing tracks");
        repaint();
        return;
    }

    repaint();
}

bool TimelineComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return containsSupportedAudioFile (files);
}

void TimelineComponent::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    trackCreationDropPreview_ = containsSupportedAudioFile (files)
        && isEmptyTrackCreationArea (juce::Point<int> { x, y });
    repaint();
}

void TimelineComponent::fileDragMove (const juce::StringArray& files, int x, int y)
{
    trackCreationDropPreview_ = containsSupportedAudioFile (files)
        && isEmptyTrackCreationArea (juce::Point<int> { x, y });
    repaint();
}

void TimelineComponent::fileDragExit (const juce::StringArray&)
{
    trackCreationDropPreview_ = false;
    repaint();
}

void TimelineComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    trackCreationDropPreview_ = false;

    if (! isEmptyTrackCreationArea (juce::Point<int> { x, y }))
    {
        appServices_.reportWarning ("Audio import failed: drop audio files below the existing tracks");
        repaint();
        return;
    }

    for (const auto& file : files)
    {
        const auto path = std::filesystem::path { file.toStdString() };
        if (! isSupportedAudioFile (path))
            continue;

        appServices_.createAudioTrackFromFile (path, path.stem().string());
        refreshAfterEdit();
        return;
    }

    appServices_.reportWarning ("Audio import failed: unsupported audio file");
    repaint();
}

void TimelineComponent::addTrack()
{
    appServices_.insertTrack (core::sequencing::TrackType::midi);
    refreshAfterEdit();
}

void TimelineComponent::showInsertTrackMenu()
{
    juce::PopupMenu menu;
    menu.addItem (insertMidiTrackMenuId, "Insert MIDI Track");
    menu.addItem (insertAudioTrackMenuId, "Insert Audio Track");
    menu.addItem (insertReturnTrackMenuId, "Insert Return Track");
    menu.addItem (insertMasterTrackMenuId, "Insert Master Track", appServices_.project().masterTrack() == nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this),
                        [this] (int result)
                        {
                            switch (result)
                            {
                                case insertMidiTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::midi);
                                    break;
                                case insertAudioTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::audio);
                                    break;
                                case insertReturnTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::returnTrack);
                                    break;
                                case insertMasterTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::master);
                                    break;
                                default:
                                    break;
                            }

                            refreshAfterEdit();
                        });
}

bool TimelineComponent::isEmptyTrackCreationArea (juce::Point<int> position) const
{
    if (position.y < rulerHeight + structureHeight + trackTopPadding)
        return false;

    const auto trackCount = static_cast<int> (appServices_.project().tracks().size());
    if (trackCount == 0)
        return true;

    return position.y > trackRowForIndex (trackCount - 1).getBottom() + (rowGap / 2);
}

bool TimelineComponent::createTrackFromPayload (const juce::var& payload)
{
    if (const auto plugin = pluginDragPayloadFromVar (payload))
        return appServices_.createTrackFromPluginStableId (plugin->stableId);

    if (const auto file = projectFileDragPayloadFromVar (payload))
    {
        if (file->kind == "Audio")
            return appServices_.createAudioTrackFromFile (std::filesystem::path { file->absolutePath }, file->displayName);

        if (file->kind == "MIDI")
            return appServices_.createMidiTrackFromFile (std::filesystem::path { file->absolutePath }, file->displayName);

        appServices_.reportWarning ("Track creation failed: drag a scanned plugin row or supported audio/MIDI file");
        return false;
    }

    return false;
}

void TimelineComponent::addClipAt (const std::string& trackId, core::time::TickPosition startInProject)
{
    const auto* track = appServices_.project().findTrackById (trackId);
    if (track == nullptr || ! core::sequencing::trackTypeCanOwnMidiClips (track->type()))
    {
        feedbackText_ = "Double-click creates MIDI clips on MIDI tracks; import audio to create audio clips";
        repaint();
        return;
    }

    const auto clipNumber = nextClipId();
    auto clipName = clipNumber;
    if (const auto separator = clipName.find_last_of ('-'); separator != std::string::npos)
        clipName = "Clip " + clipName.substr (separator + 1);

    const auto length = barSpanDuration (appServices_.project().timeSignatureMap(), startInProject, defaultClipBars);
    runCommand (std::make_unique<core::commands::AddClipCommand> (
        trackId,
        core::sequencing::MidiClip { clipNumber, clipName, startInProject, length }));
}

void TimelineComponent::deleteSelectedClip()
{
    if (selectedClips_.empty() && selectedClip_.has_value())
        selectedClips_.push_back (*selectedClip_);

    if (selectedClips_.empty())
        return;

    const auto selections = selectedClips_;
    selectedClip_.reset();
    selectedClips_.clear();

    for (const auto& selection : selections)
        runCommand (std::make_unique<core::commands::DeleteClipCommand> (selection.trackId, selection.clipId));
}

void TimelineComponent::deleteSelectedStructureRegion()
{
    if (selectedStructureRegions_.empty() && selectedStructureRegion_.has_value())
        selectedStructureRegions_.push_back (*selectedStructureRegion_);

    if (selectedStructureRegions_.empty())
        return;

    const auto selections = selectedStructureRegions_;
    selectedStructureRegion_.reset();
    selectedStructureRegions_.clear();

    for (const auto& selection : selections)
    {
        const auto& structure = appServices_.project().musicalStructure();
        if (selection.kind == StructureRegionKind::keyCenter)
        {
            if (selection.index < 0 || selection.index >= static_cast<int> (structure.keyCenterRegions().size()))
                continue;

            runCommand (std::make_unique<core::commands::DeleteKeyCenterRegionCommand> (
                structure.keyCenterRegions()[static_cast<std::size_t> (selection.index)]));
            continue;
        }

        if (selection.kind == StructureRegionKind::scaleMode)
        {
            if (selection.index < 0 || selection.index >= static_cast<int> (structure.scaleModeRegions().size()))
                continue;

            runCommand (std::make_unique<core::commands::DeleteScaleModeRegionCommand> (
                structure.scaleModeRegions()[static_cast<std::size_t> (selection.index)]));
            continue;
        }

        if (selection.index < 0 || selection.index >= static_cast<int> (structure.chordRegions().size()))
            continue;

        runCommand (std::make_unique<core::commands::DeleteChordRegionCommand> (
            structure.chordRegions()[static_cast<std::size_t> (selection.index)]));
    }
}

void TimelineComponent::deleteSelectedAutomationPoint()
{
    if (! selectedAutomationPoint_.has_value())
        return;

    const auto selection = *selectedAutomationPoint_;
    selectedAutomationPoint_.reset();

    const auto* track = appServices_.project().findTrackById (selection.trackId);
    const auto* lane = track == nullptr ? nullptr : track->findAutomationLane (selection.target);
    if (lane == nullptr)
        return;

    auto curve = lane->curve();
    try
    {
        curve.removePointAt (selection.position);
        auto replacement = core::sequencing::AutomationLane { selection.target, curve };
        replacement.setVisible (lane->visible());
        setAutomationLaneForTrack (selection.trackId, std::move (replacement));
    }
    catch (const std::exception& exception)
    {
        feedbackText_ = exception.what();
        repaint();
    }
}

void TimelineComponent::copySelection()
{
    if (focusedField_ == FocusedField::automation)
    {
        feedbackText_ = "Automation point copy is not implemented yet";
        repaint();
    }
    else if (focusedField_ == FocusedField::structure)
        copySelectedStructureRegions();
    else
        copySelectedClips();
}

void TimelineComponent::pasteSelection()
{
    if (focusedField_ == FocusedField::automation)
    {
        feedbackText_ = "Automation point paste is not implemented yet";
        repaint();
    }
    else if (focusedField_ == FocusedField::structure)
        pasteCopiedStructureRegions();
    else
        pasteCopiedClips();
}

void TimelineComponent::copySelectedClips()
{
    if (selectedClips_.empty() && selectedClip_.has_value())
        selectedClips_.push_back (*selectedClip_);

    clipClipboard_.clear();
    for (const auto& selection : selectedClips_)
    {
        if (selection.kind == ClipKind::audio)
        {
            if (const auto* clip = findAudioClip (selection))
                clipClipboard_.push_back (CopiedClip { selection.trackId, ClipKind::audio, *clip });
        }
        else if (const auto* clip = findClip (selection))
        {
            clipClipboard_.push_back (CopiedClip { selection.trackId, ClipKind::midi, *clip });
        }
    }

    feedbackText_ = clipClipboard_.empty() ? "No clips copied" : std::to_string (clipClipboard_.size()) + " clip(s) copied";
    repaint();
}

void TimelineComponent::pasteCopiedClips()
{
    if (clipClipboard_.empty())
        return;

    const auto minStart = std::min_element (clipClipboard_.begin(), clipClipboard_.end(), [] (const auto& lhs, const auto& rhs) {
        return std::visit ([] (const auto& clip) { return clip.startInProject(); }, lhs.clip)
            < std::visit ([] (const auto& clip) { return clip.startInProject(); }, rhs.clip);
    });
    const auto maxEnd = std::max_element (clipClipboard_.begin(), clipClipboard_.end(), [] (const auto& lhs, const auto& rhs) {
        return std::visit ([] (const auto& clip) { return clip.endInProject(); }, lhs.clip)
            < std::visit ([] (const auto& clip) { return clip.endInProject(); }, rhs.clip);
    });
    const auto minStartPosition = clipStart (*minStart);
    const auto maxEndPosition = clipEnd (*maxEnd);
    auto pasteStart = playheadTick_ > core::time::TickPosition {} ? playheadTick_ : maxEndPosition + core::time::TickDuration::fromTicks (ticksPerBeat);
    pasteStart = snappedPosition (pasteStart.ticks());

    std::optional<std::string> midiDestinationTrackId;
    const auto allCopiedClipsAreMidi = std::all_of (clipClipboard_.begin(), clipClipboard_.end(), [] (const auto& copied)
    {
        return copied.kind == ClipKind::midi;
    });
    if (allCopiedClipsAreMidi)
    {
        const auto& selectedTrackId = appServices_.selectedTrackId();
        const auto* selectedTrack = selectedTrackId.has_value() ? appServices_.project().findTrackById (*selectedTrackId) : nullptr;
        if (selectedTrack != nullptr && core::sequencing::trackTypeCanOwnMidiClips (selectedTrack->type()))
            midiDestinationTrackId = selectedTrack->id();
    }

    selectedClips_.clear();
    selectedClip_.reset();

    for (const auto& copied : clipClipboard_)
    {
        const auto targetTrackId = copied.kind == ClipKind::midi && midiDestinationTrackId.has_value()
            ? *midiDestinationTrackId
            : copied.sourceTrackId;
        const auto* targetTrack = appServices_.project().findTrackById (targetTrackId);
        if (targetTrack == nullptr)
            continue;

        auto pastedStart = pasteStart + (clipStart (copied) - minStartPosition);
        while (wouldOverlap (targetTrackId, {}, pastedStart, clipLength (copied)))
            pastedStart = pastedStart + core::time::TickDuration::fromTicks (ticksPerBeat);

        const auto newClipId = nextClipId();
        if (copied.kind == ClipKind::audio)
        {
            const auto& sourceClip = std::get<core::sequencing::AudioClip> (copied.clip);
            core::sequencing::AudioClip pasted {
                newClipId,
                sourceClip.name(),
                sourceClip.source(),
                pastedStart,
                sourceClip.length(),
                sourceClip.sourceOffset()
            };
            pasted.setLoopEnabled (sourceClip.loopEnabled());
            pasted.setStretchToTempo (sourceClip.stretchToTempo());
            pasted.setGainDb (sourceClip.gainDb());
            runCommand (std::make_unique<core::commands::AddClipCommand> (targetTrackId, pasted));
            selectedClips_.push_back (ClipSelection { targetTrackId, newClipId, ClipKind::audio });
        }
        else
        {
            const auto& sourceClip = std::get<core::sequencing::MidiClip> (copied.clip);
            core::sequencing::MidiClip pasted { newClipId, sourceClip.name(), pastedStart, sourceClip.length(), sourceClip.loop() };

            for (const auto& note : sourceClip.notes())
                pasted.addNote (note);

            pasted.setHarmonicMetadata (sourceClip.harmonicMetadata());
            pasted.setExpressionState (sourceClip.expressionState());
            runCommand (std::make_unique<core::commands::AddClipCommand> (targetTrackId, pasted));
            selectedClips_.push_back (ClipSelection { targetTrackId, newClipId, ClipKind::midi });
        }
    }

    if (! selectedClips_.empty())
        selectedClip_ = selectedClips_.front();
}

void TimelineComponent::copySelectedStructureRegions()
{
    if (selectedStructureRegions_.empty() && selectedStructureRegion_.has_value())
        selectedStructureRegions_.push_back (*selectedStructureRegion_);

    structureRegionClipboard_.clear();
    for (const auto& selection : selectedStructureRegions_)
        if (auto copied = copiedStructureRegionForSelection (selection))
            structureRegionClipboard_.push_back (*copied);

    feedbackText_ = structureRegionClipboard_.empty() ? "No structure regions copied"
                                                      : std::to_string (structureRegionClipboard_.size()) + " structure region(s) copied";
    repaint();
}

void TimelineComponent::pasteCopiedStructureRegions()
{
    if (structureRegionClipboard_.empty())
        return;

    const auto minStart = std::min_element (structureRegionClipboard_.begin(), structureRegionClipboard_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.start < rhs.start;
    })->start;
    const auto maxEnd = std::max_element (structureRegionClipboard_.begin(), structureRegionClipboard_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.end < rhs.end;
    })->end;
    auto pasteStart = playheadTick_ > core::time::TickPosition {} ? playheadTick_ : maxEnd + core::time::TickDuration::fromTicks (ticksPerBeat);
    pasteStart = snappedPosition (pasteStart.ticks());

    selectedStructureRegions_.clear();
    selectedStructureRegion_.reset();

    for (const auto& copied : structureRegionClipboard_)
    {
        const auto start = pasteStart + (copied.start - minStart);
        const auto end = start + (copied.end - copied.start);

        if (copied.kind == StructureRegionKind::keyCenter)
        {
            runCommand (std::make_unique<core::commands::AddKeyCenterRegionCommand> (
                core::sequencing::KeyCenterRegion { core::sequencing::Region { start, end }, copied.keyCenter }));
            continue;
        }

        if (copied.kind == StructureRegionKind::scaleMode)
        {
            runCommand (std::make_unique<core::commands::AddScaleModeRegionCommand> (
                core::sequencing::ScaleModeRegion { core::sequencing::Region { start, end }, copied.scaleDefinitionName }));
            continue;
        }

        if (copied.chordRegion.has_value())
            runCommand (std::make_unique<core::commands::AddChordRegionCommand> (
                chordRegionWithBounds (*copied.chordRegion, start, end)));
    }
}

void TimelineComponent::globalizeSelectedClipChordProgression()
{
    if (! selectedClip_.has_value())
    {
        feedbackText_ = "Select a MIDI clip before globalizing chord notes";
        repaint();
        return;
    }

    if (selectedClip_->kind != ClipKind::midi)
    {
        feedbackText_ = "Select a MIDI clip before globalizing chord notes";
        repaint();
        return;
    }

    if (! selectedNoteIdsForClip)
    {
        feedbackText_ = "Open the clip and select chord notes in the piano roll first";
        repaint();
        return;
    }

    auto noteIds = selectedNoteIdsForClip (selectedClip_->trackId, selectedClip_->clipId);
    if (noteIds.empty())
    {
        feedbackText_ = "Select chord notes in the open piano roll before globalizing";
        repaint();
        return;
    }

    selectedStructureRegion_.reset();
    runCommand (std::make_unique<core::commands::GlobalizeChordProgressionCommand> (
        selectedClip_->trackId,
        selectedClip_->clipId,
        std::move (noteIds)));
}

void TimelineComponent::handleStructureLaneDoubleClick (StructureLane lane,
                                                        core::time::TickPosition position,
                                                        std::optional<StructureRegionSelection> selection)
{
    switch (lane)
    {
        case StructureLane::tempo:
            addOrEditTempoNode (position);
            break;
        case StructureLane::timeSignature:
            addOrEditTimeSignatureMarker (position);
            break;
        case StructureLane::keyCenter:
            addOrEditKeyCenterRegion (
                position,
                selection.has_value() && selection->kind == StructureRegionKind::keyCenter ? std::optional<int> { selection->index } : std::nullopt);
            break;
        case StructureLane::scaleMode:
            addOrEditScaleModeRegion (
                position,
                selection.has_value() && selection->kind == StructureRegionKind::scaleMode ? std::optional<int> { selection->index } : std::nullopt);
            break;
        case StructureLane::chordProgression:
            feedbackText_ = "Manual chord-name editing is deferred until chord-symbol parsing is available";
            repaint();
            break;
        case StructureLane::none:
            break;
    }
}

void TimelineComponent::addOrEditTempoNode (core::time::TickPosition position)
{
    const auto existingTempo = appServices_.project().tempoMap().tempoAt (position);
    auto* window = new juce::AlertWindow { "Tempo Node", "Set the tempo at " + formatBarBeat (position), juce::AlertWindow::NoIcon };
    window->addTextEditor ("bpm", juce::String (existingTempo.bpm(), 0), "BPM");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    showModalAlert (window, *this, [this, position] (juce::AlertWindow& alert)
    {
        double bpm = 0.0;
        if (! parsePositiveDouble (alert.getTextEditorContents ("bpm"), bpm))
        {
            feedbackText_ = "Tempo must be a positive BPM value";
            repaint();
            return;
        }

        runCommand (std::make_unique<core::commands::AddTempoNodeCommand> (position, core::time::Tempo { bpm }));
    });
}

void TimelineComponent::addOrEditTimeSignatureMarker (core::time::TickPosition position)
{
    const auto existing = appServices_.project().timeSignatureMap().timeSignatureAt (position);
    auto* window = new juce::AlertWindow { "Time Signature", "Set the meter at " + formatBarBeat (position), juce::AlertWindow::NoIcon };
    window->addTextEditor ("numerator", juce::String (existing.numerator()), "Beats per bar");
    window->addComboBox ("denominator", { "2", "4", "8", "16" }, "Beat unit");
    if (auto* denominator = window->getComboBoxComponent ("denominator"))
    {
        const auto selected = existing.denominator() == 2 ? 1 : existing.denominator() == 4 ? 2 : existing.denominator() == 8 ? 3 : 4;
        denominator->setSelectedId (selected, juce::dontSendNotification);
    }
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    showModalAlert (window, *this, [this, position] (juce::AlertWindow& alert)
    {
        int numerator = 0;
        if (! parsePositiveInt (alert.getTextEditorContents ("numerator"), numerator))
        {
            feedbackText_ = "Time signature numerator must be positive";
            repaint();
            return;
        }

        const auto denominatorId = alert.getComboBoxComponent ("denominator")->getSelectedId();
        const auto denominator = denominatorId == 1 ? 2 : denominatorId == 2 ? 4 : denominatorId == 3 ? 8 : 16;
        runCommand (std::make_unique<core::commands::AddTimeSignatureMarkerCommand> (
            position,
            core::time::TimeSignature { numerator, denominator }));
    });
}

void TimelineComponent::addOrEditKeyCenterRegion (core::time::TickPosition position, std::optional<int> regionIndex)
{
    const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto selectedSemitone = appServices_.project().musicalStructure().keyCenterAt (position).semitonesFromC();
    auto start = position;
    auto end = position + barSpanDuration (appServices_.project().timeSignatureMap(), position, defaultRegionBars);

    if (regionIndex.has_value())
    {
        const auto& region = appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (*regionIndex)];
        selectedSemitone = region.pitchClass().semitonesFromC();
        start = region.start();
        end = region.end();
    }

    auto* window = new juce::AlertWindow { "Key Center", "Choose the key center", juce::AlertWindow::NoIcon };
    window->addComboBox ("key", names, "Key");
    window->getComboBoxComponent ("key")->setSelectedId (selectedSemitone + 1, juce::dontSendNotification);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    showModalAlert (window, *this, [this, start, end, regionIndex] (juce::AlertWindow& alert)
    {
        const auto newRegion = core::sequencing::KeyCenterRegion {
            core::sequencing::Region { start, end },
            pitchClassFromSemitone (alert.getComboBoxComponent ("key")->getSelectedId() - 1)
        };

        if (regionIndex.has_value())
        {
            runCommand (std::make_unique<core::commands::ReplaceKeyCenterRegionCommand> (
                appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (*regionIndex)],
                newRegion));
            return;
        }

        runCommand (std::make_unique<core::commands::AddKeyCenterRegionCommand> (newRegion));
    });
}

void TimelineComponent::addOrEditScaleModeRegion (core::time::TickPosition position, std::optional<int> regionIndex)
{
    auto scaleName = appServices_.project().musicalStructure().scaleDefinitionNameAt (position);
    auto start = position;
    auto end = position + barSpanDuration (appServices_.project().timeSignatureMap(), position, defaultRegionBars);

    if (regionIndex.has_value())
    {
        const auto& region = appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (*regionIndex)];
        scaleName = region.scaleDefinitionName();
        start = region.start();
        end = region.end();
    }

    std::set<std::string> uniqueNames;
    for (const auto& definition : core::music_theory::ScaleLibrary::createBuiltInLibrary().definitions())
        uniqueNames.insert (definition.name());
    for (const auto& definition : appServices_.project().customScales())
        uniqueNames.insert (definition.name());

    juce::StringArray items;
    auto selectedId = 1;
    auto id = 1;
    for (const auto& name : uniqueNames)
    {
        items.add (name);
        if (name == scaleName)
            selectedId = id;
        ++id;
    }

    auto* window = new juce::AlertWindow { "Scale / Mode", "Choose the active scale or mode", juce::AlertWindow::NoIcon };
    window->addComboBox ("scale", items, "Scale");
    window->getComboBoxComponent ("scale")->setSelectedId (selectedId, juce::dontSendNotification);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    showModalAlert (window, *this, [this, start, end, regionIndex] (juce::AlertWindow& alert)
    {
        const auto newRegion = core::sequencing::ScaleModeRegion {
            core::sequencing::Region { start, end },
            alert.getComboBoxComponent ("scale")->getText().toStdString()
        };

        if (regionIndex.has_value())
        {
            runCommand (std::make_unique<core::commands::ReplaceScaleModeRegionCommand> (
                appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (*regionIndex)],
                newRegion));
            return;
        }

        runCommand (std::make_unique<core::commands::AddScaleModeRegionCommand> (newRegion));
    });
}

void TimelineComponent::addOrReplaceScaleModeRegionFromPalette (const std::string& scaleName, core::time::TickPosition position)
{
    const auto point = juce::Point<float> {
        static_cast<float> (tickToX (position)),
        static_cast<float> (structureLaneBounds (StructureLane::scaleMode).getCentreY())
    };

    if (const auto selection = structureRegionAt (point);
        selection.has_value() && selection->kind == StructureRegionKind::scaleMode)
    {
        const auto& previous = appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (selection->index)];
        runCommand (std::make_unique<core::commands::ReplaceScaleModeRegionCommand> (
            previous,
            core::sequencing::ScaleModeRegion {
                previous.region(),
                scaleName
            }));
        return;
    }

    runCommand (std::make_unique<core::commands::AddScaleModeRegionCommand> (
        core::sequencing::ScaleModeRegion {
            core::sequencing::Region {
                position,
                position + barSpanDuration (appServices_.project().timeSignatureMap(), position, defaultRegionBars)
            },
            scaleName
        }));
}

void TimelineComponent::showChordRegionPopup (int chordRegionIndex)
{
    const auto& regions = appServices_.project().musicalStructure().chordRegions();
    if (chordRegionIndex < 0 || chordRegionIndex >= static_cast<int> (regions.size()))
        return;

    const auto& region = regions[static_cast<std::size_t> (chordRegionIndex)];
    const auto scaleLibrary = scaleLibraryForProject();
    if (! core::sequencing::isBorrowedChordRegion (appServices_.project().musicalStructure(), region, scaleLibrary))
    {
        feedbackText_ = "Chord is diatonic in the active scale";
        repaint();
        return;
    }

    const auto suggestions = core::sequencing::compatibleScaleSuggestionsFor (
        appServices_.project().musicalStructure(),
        region,
        scaleLibrary);

    juce::PopupMenu suggestionMenu;
    if (suggestions.empty())
    {
        suggestionMenu.addItem (1, "No compatible same-key modes found", false);
    }
    else
    {
        for (int index = 0; index < static_cast<int> (suggestions.size()); ++index)
            suggestionMenu.addItem (index + 1, suggestions[static_cast<std::size_t> (index)].displayName);
    }

    juce::PopupMenu menu;
    menu.addSubMenu ("Find Compatible Modes", suggestionMenu, ! suggestions.empty());

    const auto safeThis = juce::Component::SafePointer<TimelineComponent> { this };
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (this),
        [safeThis, suggestions, chordRegionIndex] (int result)
        {
            if (safeThis == nullptr || result <= 0)
                return;

            const auto suggestionIndex = static_cast<std::size_t> (result - 1);
            if (suggestionIndex >= suggestions.size())
                return;

            safeThis->applyCompatibleScaleSuggestion (chordRegionIndex, suggestions[suggestionIndex]);
        });
}

void TimelineComponent::applyCompatibleScaleSuggestion (int chordRegionIndex, core::sequencing::CompatibleScaleSuggestion suggestion)
{
    const auto& regions = appServices_.project().musicalStructure().chordRegions();
    if (chordRegionIndex < 0 || chordRegionIndex >= static_cast<int> (regions.size()))
        return;

    const auto& chordRegion = regions[static_cast<std::size_t> (chordRegionIndex)];
    runCommand (std::make_unique<core::commands::AddScaleModeRegionCommand> (
        core::sequencing::ScaleModeRegion {
            chordRegion.region(),
            suggestion.context.scaleDefinitionName()
        }));
}

void TimelineComponent::commitStructureDrag (const StructureDragState& drag)
{
    if (drag.previewStart == drag.originalStart && drag.previewEnd == drag.originalEnd)
    {
        repaint();
        return;
    }

    try
    {
        if (drag.selection.kind == StructureRegionKind::keyCenter)
        {
            if (drag.selection.index < 0)
            {
                runCommand (std::make_unique<core::commands::AddKeyCenterRegionCommand> (
                    core::sequencing::KeyCenterRegion {
                        core::sequencing::Region { drag.previewStart, drag.previewEnd },
                        appServices_.project().musicalStructure().defaultKeyCenter()
                    }));
                return;
            }

            const auto& previous = appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (drag.selection.index)];
            runCommand (std::make_unique<core::commands::ReplaceKeyCenterRegionCommand> (
                previous,
                core::sequencing::KeyCenterRegion {
                    core::sequencing::Region { drag.previewStart, drag.previewEnd },
                    previous.pitchClass()
                }));
            return;
        }

        if (drag.selection.kind == StructureRegionKind::scaleMode)
        {
            if (drag.selection.index < 0)
            {
                runCommand (std::make_unique<core::commands::AddScaleModeRegionCommand> (
                    core::sequencing::ScaleModeRegion {
                        core::sequencing::Region { drag.previewStart, drag.previewEnd },
                        appServices_.project().musicalStructure().defaultScaleDefinitionName()
                    }));
                return;
            }

            const auto& previous = appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (drag.selection.index)];
            runCommand (std::make_unique<core::commands::ReplaceScaleModeRegionCommand> (
                previous,
                core::sequencing::ScaleModeRegion {
                    core::sequencing::Region { drag.previewStart, drag.previewEnd },
                    previous.scaleDefinitionName()
                }));
            return;
        }

        const auto& previous = appServices_.project().musicalStructure().chordRegions()[static_cast<std::size_t> (drag.selection.index)];
        runCommand (std::make_unique<core::commands::ReplaceChordRegionCommand> (
            previous,
            chordRegionWithBounds (previous, drag.previewStart, drag.previewEnd)));
    }
    catch (const std::exception& exception)
    {
        feedbackText_ = exception.what();
        repaint();
    }
}

void TimelineComponent::commitAutomationPointDrag (const AutomationDragState& drag)
{
    const auto* track = appServices_.project().findTrackById (drag.selection.trackId);
    const auto* lane = track == nullptr ? nullptr : track->findAutomationLane (drag.selection.target);
    if (lane == nullptr)
    {
        repaint();
        return;
    }

    try
    {
        auto curve = lane->curve();
        const auto interpolation = [&curve, &drag]
        {
            const auto match = std::find_if (curve.points().begin(), curve.points().end(), [&drag] (const auto& point) {
                return point.position == drag.selection.position;
            });

            return match == curve.points().end() ? core::sequencing::AutomationInterpolation::linear
                                                 : match->interpolationToNext;
        }();

        curve.removePointAt (drag.selection.position);
        const auto duplicate = std::find_if (curve.points().begin(), curve.points().end(), [&drag] (const auto& point) {
            return point.position == drag.previewPosition;
        });
        if (duplicate != curve.points().end())
            curve.removePointAt (drag.previewPosition);

        curve.addPoint (core::sequencing::AutomationPoint { drag.previewPosition, drag.previewValue, interpolation });
        auto replacement = core::sequencing::AutomationLane { drag.selection.target, curve };
        replacement.setVisible (lane->visible());
        selectedAutomationPoint_ = AutomationPointSelection { drag.selection.trackId, drag.selection.target, drag.previewPosition };
        setAutomationLaneForTrack (drag.selection.trackId, std::move (replacement));
    }
    catch (const std::exception& exception)
    {
        feedbackText_ = exception.what();
        repaint();
    }
}

void TimelineComponent::showTrackAutomationMenu (const std::string& trackId)
{
    const auto* track = appServices_.project().findTrackById (trackId);
    if (track == nullptr)
        return;

    struct TargetMenuItem
    {
        core::sequencing::AutomationTarget target;
        bool currentlyVisible = false;
    };

    std::vector<TargetMenuItem> targets;
    juce::PopupMenu menu;

    const auto addTarget = [&] (juce::PopupMenu& targetMenu,
                                juce::String label,
                                core::sequencing::AutomationTarget target)
    {
        const auto* lane = track->findAutomationLane (target);
        const auto visible = lane != nullptr && lane->visible();
        const auto id = static_cast<int> (targets.size()) + 1;
        targets.push_back (TargetMenuItem { target, visible });
        targetMenu.addItem (id, (visible ? "Hide " : "Show ") + label);
    };

    addTarget (menu, "Volume", core::sequencing::AutomationTarget::trackVolume (trackId));
    addTarget (menu, "Pan", core::sequencing::AutomationTarget::trackPan (trackId));

    juce::PopupMenu sendMenu;
    for (const auto& candidate : appServices_.project().tracks())
    {
        if (candidate.type() != core::sequencing::TrackType::returnTrack || candidate.id() == trackId)
            continue;

        addTarget (sendMenu,
                   juce::String::fromUTF8 (candidate.name().c_str()),
                   core::sequencing::AutomationTarget::sendLevel (trackId, candidate.id()));
    }

    if (sendMenu.getNumItems() > 0)
        menu.addSubMenu ("Sends", sendMenu);

    juce::PopupMenu parameterMenu;
    for (const auto& slot : track->deviceChain().slots())
    {
        const auto stableId = stableIdForPluginReference (slot.plugin());
        const auto plugin = stableId.empty() ? std::nullopt : appServices_.pluginRegistry().findByStableId (stableId);
        if (! plugin.has_value() || plugin->parameters.empty())
            continue;

        juce::PopupMenu slotMenu;
        auto addedCount = 0;
        for (const auto& parameter : plugin->parameters)
        {
            if (! parameter.automatable || parameter.stableId.empty())
                continue;

            addTarget (slotMenu,
                       juce::String::fromUTF8 ((parameter.name.empty() ? parameter.stableId : parameter.name).c_str()),
                       core::sequencing::AutomationTarget::pluginParameter (trackId, slot.id(), parameter.stableId));
            if (++addedCount >= 32)
                break;
        }

        if (slotMenu.getNumItems() > 0)
            parameterMenu.addSubMenu (juce::String::fromUTF8 (slot.plugin().pluginName.c_str()), slotMenu);
    }

    if (parameterMenu.getNumItems() > 0)
        menu.addSubMenu ("Plugin Parameters", parameterMenu);

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this),
                        [this, trackId, targets = std::move (targets)] (int result)
                        {
                            if (result <= 0 || result > static_cast<int> (targets.size()))
                                return;

                            const auto& item = targets[static_cast<std::size_t> (result - 1)];
                            if (item.currentlyVisible)
                                hideAutomationLaneForTarget (trackId, item.target);
                            else
                                showAutomationLaneForTarget (trackId, item.target);
                        });
}

void TimelineComponent::showAutomationLaneForTarget (const std::string& trackId, core::sequencing::AutomationTarget target)
{
    auto* track = appServices_.project().findTrackById (trackId);
    if (track == nullptr)
        return;

    if (target.kind == core::sequencing::AutomationTargetKind::sendLevel)
    {
        const auto hasSend = std::any_of (track->routing().sends().begin(),
                                          track->routing().sends().end(),
                                          [&target] (const auto& send)
                                          {
                                              return send.targetReturnTrackId == target.sendTargetTrackId;
                                          });

        if (! hasSend)
        {
            auto routing = track->routing();
            routing.addOrReplaceSend (core::sequencing::ReturnSend { target.sendTargetTrackId, 0.0 });
            runCommand (std::make_unique<core::commands::SetTrackRoutingCommand> (trackId, routing));
            track = appServices_.project().findTrackById (trackId);
            if (track == nullptr)
                return;
        }
    }

    if (const auto* existing = track->findAutomationLane (target))
    {
        auto lane = *existing;
        lane.setVisible (true);
        setAutomationLaneForTrack (trackId, std::move (lane));
        return;
    }

    auto lane = core::sequencing::AutomationLane { target };
    lane.setVisible (true);
    setAutomationLaneForTrack (trackId, std::move (lane));
}

void TimelineComponent::hideAutomationLaneForTarget (const std::string& trackId, core::sequencing::AutomationTarget target)
{
    const auto* track = appServices_.project().findTrackById (trackId);
    const auto* existing = track == nullptr ? nullptr : track->findAutomationLane (target);
    if (existing == nullptr)
        return;

    auto lane = *existing;
    lane.setVisible (false);
    setAutomationLaneForTrack (trackId, std::move (lane));
}

void TimelineComponent::setAutomationLaneForTrack (const std::string& trackId, core::sequencing::AutomationLane lane)
{
    runCommand (std::make_unique<core::commands::SetTrackAutomationLaneCommand> (trackId, std::move (lane)));
}

core::sequencing::ChordRegion TimelineComponent::chordRegionWithBounds (const core::sequencing::ChordRegion& region,
                                                                        core::time::TickPosition start,
                                                                        core::time::TickPosition end) const
{
    return core::sequencing::ChordRegion {
        core::sequencing::Region { start, end },
        region.root(),
        region.quality(),
        region.chordTones(),
        region.chordName()
    };
}

void TimelineComponent::runCommand (std::unique_ptr<core::commands::Command> command)
{
    const auto result = appServices_.commandStack().execute (std::move (command));
    feedbackText_ = result.failed() ? result.error() : std::string {};
    if (result.failed())
        appServices_.reportWarning ("Timeline edit failed: " + result.error());
    refreshAfterEdit();
}

void TimelineComponent::refreshAfterEdit()
{
    ensureTimelineFitsAllClips();
    repaint();

    if (auto* parent = getParentComponent())
        parent->repaint();
}

void TimelineComponent::zoomHorizontally (float wheelDeltaY, int)
{
    visibleTimelineBars_ = std::clamp (visibleTimelineBars_ + (wheelDeltaY > 0.0f ? -timelineExtensionBars : timelineExtensionBars),
                                       beatsPerBar,
                                       512);
    playheadPixelValid_ = false;
    ensureTimelineFitsAllClips();
    repaint();
}

std::string TimelineComponent::nextClipId() const
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

juce::Rectangle<int> TimelineComponent::structureLaneBounds (StructureLane lane) const
{
    if (lane == StructureLane::none)
        return {};

    const auto laneHeight = 28;
    const auto laneGap = 5;
    const auto top = rulerHeight + 8;
    auto index = 0;

    switch (lane)
    {
        case StructureLane::tempo: index = 0; break;
        case StructureLane::timeSignature: index = 1; break;
        case StructureLane::keyCenter: index = 2; break;
        case StructureLane::scaleMode: index = 3; break;
        case StructureLane::chordProgression: index = 4; break;
        case StructureLane::none: break;
    }

    return juce::Rectangle<int> {
        horizontalPadding,
        top + (index * (laneHeight + laneGap)),
        std::max (0, getWidth() - (horizontalPadding * 2)),
        laneHeight
    };
}

TimelineComponent::StructureLane TimelineComponent::structureLaneAt (juce::Point<float> point) const
{
    const auto pixel = point.roundToInt();
    for (const auto lane : { StructureLane::tempo, StructureLane::timeSignature, StructureLane::keyCenter, StructureLane::scaleMode, StructureLane::chordProgression })
    {
        if (structureLaneBounds (lane).contains (pixel))
            return lane;
    }

    return StructureLane::none;
}

std::optional<TimelineComponent::StructureRegionSelection> TimelineComponent::structureRegionAt (juce::Point<float> point) const
{
    const auto pixel = point.roundToInt();
    if (structureLaneBounds (StructureLane::keyCenter).contains (pixel))
    {
        const auto& regions = appServices_.project().musicalStructure().keyCenterRegions();
        if (regions.empty())
        {
            auto bounds = juce::Rectangle<int> {
                tickToX (core::time::TickPosition {}),
                structureLaneBounds (StructureLane::keyCenter).getY() + 3,
                std::max (12, durationToWidth (barSpanDuration (appServices_.project().timeSignatureMap(), core::time::TickPosition {}, 8))),
                structureLaneBounds (StructureLane::keyCenter).getHeight() - 6
            }.getIntersection (structureLaneBounds (StructureLane::keyCenter).reduced (2, 2));

            if (bounds.contains (pixel))
                return StructureRegionSelection { StructureRegionKind::keyCenter, -1 };
        }

        for (auto index = static_cast<int> (regions.size()) - 1; index >= 0; --index)
        {
            const auto& region = regions[static_cast<std::size_t> (index)];
            auto bounds = juce::Rectangle<int> {
                tickToX (region.start()),
                structureLaneBounds (StructureLane::keyCenter).getY() + 3,
                std::max (12, durationToWidth (region.end() - region.start())),
                structureLaneBounds (StructureLane::keyCenter).getHeight() - 6
            }.getIntersection (structureLaneBounds (StructureLane::keyCenter).reduced (2, 2));

            if (bounds.contains (pixel))
                return StructureRegionSelection { StructureRegionKind::keyCenter, index };
        }
    }

    if (structureLaneBounds (StructureLane::scaleMode).contains (pixel))
    {
        const auto& regions = appServices_.project().musicalStructure().scaleModeRegions();
        if (regions.empty())
        {
            auto bounds = juce::Rectangle<int> {
                tickToX (core::time::TickPosition {}),
                structureLaneBounds (StructureLane::scaleMode).getY() + 3,
                std::max (12, durationToWidth (barSpanDuration (appServices_.project().timeSignatureMap(), core::time::TickPosition {}, 8))),
                structureLaneBounds (StructureLane::scaleMode).getHeight() - 6
            }.getIntersection (structureLaneBounds (StructureLane::scaleMode).reduced (2, 2));

            if (bounds.contains (pixel))
                return StructureRegionSelection { StructureRegionKind::scaleMode, -1 };
        }

        for (auto index = static_cast<int> (regions.size()) - 1; index >= 0; --index)
        {
            const auto& region = regions[static_cast<std::size_t> (index)];
            auto bounds = juce::Rectangle<int> {
                tickToX (region.start()),
                structureLaneBounds (StructureLane::scaleMode).getY() + 3,
                std::max (12, durationToWidth (region.end() - region.start())),
                structureLaneBounds (StructureLane::scaleMode).getHeight() - 6
            }.getIntersection (structureLaneBounds (StructureLane::scaleMode).reduced (2, 2));

            if (bounds.contains (pixel))
                return StructureRegionSelection { StructureRegionKind::scaleMode, index };
        }
    }

    if (structureLaneBounds (StructureLane::chordProgression).contains (pixel))
    {
        const auto& regions = appServices_.project().musicalStructure().chordRegions();
        for (auto index = static_cast<int> (regions.size()) - 1; index >= 0; --index)
        {
            const auto& region = regions[static_cast<std::size_t> (index)];
            auto bounds = juce::Rectangle<int> {
                tickToX (region.start()),
                structureLaneBounds (StructureLane::chordProgression).getY() + 3,
                std::max (12, durationToWidth (region.end() - region.start())),
                structureLaneBounds (StructureLane::chordProgression).getHeight() - 6
            }.getIntersection (structureLaneBounds (StructureLane::chordProgression).reduced (2, 2));

            if (bounds.contains (pixel))
                return StructureRegionSelection { StructureRegionKind::chordProgression, index };
        }
    }

    return std::nullopt;
}

std::optional<TimelineComponent::CopiedStructureRegion> TimelineComponent::copiedStructureRegionForSelection (StructureRegionSelection selection) const
{
    const auto& structure = appServices_.project().musicalStructure();
    if (selection.kind == StructureRegionKind::keyCenter)
    {
        if (selection.index < 0)
        {
            const auto start = core::time::TickPosition {};
            return CopiedStructureRegion {
                StructureRegionKind::keyCenter,
                start,
                start + barSpanDuration (appServices_.project().timeSignatureMap(), start, 8),
                structure.defaultKeyCenter(),
                {},
                std::nullopt
            };
        }

        if (selection.index >= static_cast<int> (structure.keyCenterRegions().size()))
            return std::nullopt;

        const auto& region = structure.keyCenterRegions()[static_cast<std::size_t> (selection.index)];
        return CopiedStructureRegion {
            StructureRegionKind::keyCenter,
            region.start(),
            region.end(),
            region.pitchClass(),
            {},
            std::nullopt
        };
    }

    if (selection.kind == StructureRegionKind::scaleMode)
    {
        if (selection.index < 0)
        {
            const auto start = core::time::TickPosition {};
            return CopiedStructureRegion {
                StructureRegionKind::scaleMode,
                start,
                start + barSpanDuration (appServices_.project().timeSignatureMap(), start, 8),
                {},
                structure.defaultScaleDefinitionName(),
                std::nullopt
            };
        }

        if (selection.index >= static_cast<int> (structure.scaleModeRegions().size()))
            return std::nullopt;

        const auto& region = structure.scaleModeRegions()[static_cast<std::size_t> (selection.index)];
        return CopiedStructureRegion {
            StructureRegionKind::scaleMode,
            region.start(),
            region.end(),
            {},
            region.scaleDefinitionName(),
            std::nullopt
        };
    }

    if (selection.index < 0 || selection.index >= static_cast<int> (structure.chordRegions().size()))
        return std::nullopt;

    const auto& region = structure.chordRegions()[static_cast<std::size_t> (selection.index)];
    return CopiedStructureRegion {
        StructureRegionKind::chordProgression,
        region.start(),
        region.end(),
        {},
        {},
        region
    };
}

bool TimelineComponent::isStructureRegionSelected (StructureRegionSelection selection) const
{
    return std::any_of (selectedStructureRegions_.begin(), selectedStructureRegions_.end(), [selection] (const auto& existing) {
        return existing.kind == selection.kind && existing.index == selection.index;
    });
}

bool TimelineComponent::isClipSelected (const ClipSelection& selection) const
{
    return std::any_of (selectedClips_.begin(), selectedClips_.end(), [&selection] (const auto& existing) {
        return existing.trackId == selection.trackId && existing.clipId == selection.clipId && existing.kind == selection.kind;
    });
}

std::optional<TimelineComponent::DragMode> TimelineComponent::resizeModeAtStructureRegionEdge (juce::Point<float> point) const
{
    const auto selection = structureRegionAt (point);
    if (! selection.has_value())
        return std::nullopt;

    auto startX = 0;
    auto endX = 0;

    if (selection->kind == StructureRegionKind::keyCenter)
    {
        if (selection->index < 0)
        {
            startX = tickToX (core::time::TickPosition {});
            endX = tickToX (core::time::TickPosition {} + barSpanDuration (appServices_.project().timeSignatureMap(), core::time::TickPosition {}, 8));
        }
        else
        {
            const auto& region = appServices_.project().musicalStructure().keyCenterRegions()[static_cast<std::size_t> (selection->index)];
            startX = tickToX (region.start());
            endX = tickToX (region.end());
        }
    }
    else if (selection->kind == StructureRegionKind::scaleMode)
    {
        if (selection->index < 0)
        {
            startX = tickToX (core::time::TickPosition {});
            endX = tickToX (core::time::TickPosition {} + barSpanDuration (appServices_.project().timeSignatureMap(), core::time::TickPosition {}, 8));
        }
        else
        {
            const auto& region = appServices_.project().musicalStructure().scaleModeRegions()[static_cast<std::size_t> (selection->index)];
            startX = tickToX (region.start());
            endX = tickToX (region.end());
        }
    }
    else
    {
        const auto& region = appServices_.project().musicalStructure().chordRegions()[static_cast<std::size_t> (selection->index)];
        startX = tickToX (region.start());
        endX = tickToX (region.end());
    }

    if (std::abs (point.x - static_cast<float> (startX)) <= resizeHandleWidth + 2)
        return DragMode::resizeLeft;

    if (std::abs (point.x - static_cast<float> (endX)) <= resizeHandleWidth + 2)
        return DragMode::resizeRight;

    return std::nullopt;
}

std::optional<TimelineComponent::DragMode> TimelineComponent::resizeModeAtClipEdge (juce::Point<float> point) const
{
    const auto selection = clipAt (point);
    if (! selection.has_value())
        return std::nullopt;

    auto start = core::time::TickPosition {};
    auto length = core::time::TickDuration {};
    auto hasClip = false;
    if (selection->kind == ClipKind::audio)
    {
        if (const auto* clip = findAudioClip (*selection))
        {
            start = clip->startInProject();
            length = clip->length();
            hasClip = true;
        }
    }
    else if (const auto* clip = findClip (*selection))
    {
        start = clip->startInProject();
        length = clip->length();
        hasClip = true;
    }

    if (! hasClip)
        return std::nullopt;

    const auto startX = tickToX (start);
    const auto endX = startX + durationToWidth (length);

    if (std::abs (point.x - static_cast<float> (startX)) <= resizeHandleWidth + 2)
        return DragMode::resizeLeft;

    if (std::abs (point.x - static_cast<float> (endX)) <= resizeHandleWidth + 2)
        return DragMode::resizeRight;

    return std::nullopt;
}

juce::Rectangle<int> TimelineComponent::trackRowForIndex (int index) const
{
    const auto& tracks = appServices_.project().tracks();
    auto y = rulerHeight + structureHeight + trackTopPadding;
    for (int previous = 0; previous < index && previous < static_cast<int> (tracks.size()); ++previous)
        y += trackHeightFor (tracks[static_cast<std::size_t> (previous)]) + rowGap;

    const auto height = index >= 0 && index < static_cast<int> (tracks.size())
        ? trackHeightFor (tracks[static_cast<std::size_t> (index)])
        : rowHeight;

    return juce::Rectangle<int> {
        horizontalPadding,
        y,
        std::max (0, getWidth() - (horizontalPadding * 2)),
        height
    };
}

juce::Rectangle<int> TimelineComponent::trackHeaderBoundsForIndex (int index) const
{
    return trackRowForIndex (index).withHeight (rowHeight);
}

juce::Rectangle<int> TimelineComponent::automationLaneBoundsForHit (AutomationLaneHit hit) const
{
    if (hit.trackIndex < 0 || hit.laneIndex < 0)
        return {};

    const auto header = trackHeaderBoundsForIndex (hit.trackIndex);
    return juce::Rectangle<int> {
        header.getX() + 4,
        header.getBottom() + automationLaneGap + (hit.laneIndex * (automationLaneHeight + automationLaneGap)),
        std::max (0, header.getWidth() - 8),
        automationLaneHeight
    };
}

int TimelineComponent::trackIndexAt (juce::Point<float> point) const
{
    const auto& tracks = appServices_.project().tracks();
    for (int index = 0; index < static_cast<int> (tracks.size()); ++index)
    {
        if (trackRowForIndex (index).contains (point.roundToInt()))
            return index;
    }

    return -1;
}

std::optional<TimelineComponent::AutomationLaneHit> TimelineComponent::automationLaneAt (juce::Point<float> point) const
{
    const auto pixel = point.roundToInt();
    const auto& tracks = appServices_.project().tracks();
    for (int trackIndex = 0; trackIndex < static_cast<int> (tracks.size()); ++trackIndex)
    {
        const auto& track = tracks[static_cast<std::size_t> (trackIndex)];
        auto visibleLaneIndex = 0;
        for (const auto& lane : track.automationLanes())
        {
            if (! lane.visible())
                continue;

            auto hit = AutomationLaneHit { trackIndex, visibleLaneIndex++, lane.target() };
            if (automationLaneBoundsForHit (hit).contains (pixel))
                return hit;
        }
    }

    return std::nullopt;
}

std::optional<TimelineComponent::AutomationPointSelection> TimelineComponent::automationPointAt (juce::Point<float> point) const
{
    const auto laneHit = automationLaneAt (point);
    if (! laneHit.has_value())
        return std::nullopt;

    const auto& tracks = appServices_.project().tracks();
    if (laneHit->trackIndex < 0 || laneHit->trackIndex >= static_cast<int> (tracks.size()))
        return std::nullopt;

    const auto& track = tracks[static_cast<std::size_t> (laneHit->trackIndex)];
    const auto* lane = track.findAutomationLane (laneHit->target);
    if (lane == nullptr)
        return std::nullopt;

    auto laneBody = automationLaneBoundsForHit (*laneHit);
    laneBody.removeFromLeft (96);
    const auto graphBounds = laneBody.reduced (5, 6);
    const auto pixel = point.roundToInt();

    for (const auto& automationPoint : lane->curve().points())
    {
        const auto pointX = tickToX (automationPoint.position);
        const auto pointY = yForAutomationValue (graphBounds, automationPoint.normalizedValue);
        if (std::abs (pixel.x - pointX) <= 6 && std::abs (pixel.y - pointY) <= 6)
            return AutomationPointSelection { track.id(), laneHit->target, automationPoint.position };
    }

    return std::nullopt;
}

std::optional<TimelineComponent::ClipSelection> TimelineComponent::clipAt (juce::Point<float> point) const
{
    const auto& tracks = appServices_.project().tracks();
    for (int trackIndex = static_cast<int> (tracks.size()) - 1; trackIndex >= 0; --trackIndex)
    {
        const auto& track = tracks[static_cast<std::size_t> (trackIndex)];
        const auto row = trackHeaderBoundsForIndex (trackIndex);

        for (auto clipIterator = track.clips().rbegin(); clipIterator != track.clips().rend(); ++clipIterator)
        {
            auto clipBounds = juce::Rectangle<int> {
                tickToX (clipIterator->startInProject()),
                row.getY() + 22,
                std::max (18, durationToWidth (clipIterator->length())),
                rowHeight - 32
            }.getIntersection (row.reduced (3, 0));

            if (clipBounds.contains (point.roundToInt()))
                return ClipSelection { track.id(), clipIterator->id(), ClipKind::midi };
        }

        for (auto clipIterator = track.audioClips().rbegin(); clipIterator != track.audioClips().rend(); ++clipIterator)
        {
            auto clipBounds = juce::Rectangle<int> {
                tickToX (clipIterator->startInProject()),
                row.getY() + 22,
                std::max (18, durationToWidth (clipIterator->length())),
                rowHeight - 32
            }.getIntersection (row.reduced (3, 0));

            if (clipBounds.contains (point.roundToInt()))
                return ClipSelection { track.id(), clipIterator->id(), ClipKind::audio };
        }
    }

    return std::nullopt;
}

const core::sequencing::MidiClip* TimelineComponent::findClip (const ClipSelection& selection) const noexcept
{
    if (selection.kind != ClipKind::midi)
        return nullptr;

    const auto* track = appServices_.project().findTrackById (selection.trackId);
    if (track == nullptr)
        return nullptr;

    return track->findClipById (selection.clipId);
}

const core::sequencing::AudioClip* TimelineComponent::findAudioClip (const ClipSelection& selection) const noexcept
{
    if (selection.kind != ClipKind::audio)
        return nullptr;

    const auto* track = appServices_.project().findTrackById (selection.trackId);
    if (track == nullptr)
        return nullptr;

    return track->findAudioClipById (selection.clipId);
}

core::time::TickPosition TimelineComponent::clipStart (const CopiedClip& copied) const noexcept
{
    return std::visit ([] (const auto& clip) { return clip.startInProject(); }, copied.clip);
}

core::time::TickPosition TimelineComponent::clipEnd (const CopiedClip& copied) const noexcept
{
    return std::visit ([] (const auto& clip) { return clip.endInProject(); }, copied.clip);
}

core::time::TickDuration TimelineComponent::clipLength (const CopiedClip& copied) const noexcept
{
    return std::visit ([] (const auto& clip) { return clip.length(); }, copied.clip);
}

juce::Rectangle<int> TimelineComponent::marqueeBounds() const
{
    if (! marqueeState_.has_value())
        return {};

    return juce::Rectangle<int>::leftTopRightBottom (
        std::min (marqueeState_->start.x, marqueeState_->current.x),
        std::min (marqueeState_->start.y, marqueeState_->current.y),
        std::max (marqueeState_->start.x, marqueeState_->current.x),
        std::max (marqueeState_->start.y, marqueeState_->current.y));
}

void TimelineComponent::selectClipsInMarquee (juce::Rectangle<int> bounds)
{
    if (bounds.getWidth() < 3 || bounds.getHeight() < 3)
        return;

    selectedClip_.reset();
    selectedClips_.clear();

    const auto& tracks = appServices_.project().tracks();
    for (int trackIndex = 0; trackIndex < static_cast<int> (tracks.size()); ++trackIndex)
    {
        const auto& track = tracks[static_cast<std::size_t> (trackIndex)];
        const auto row = trackHeaderBoundsForIndex (trackIndex);

        for (const auto& clip : track.clips())
        {
            const auto clipBounds = juce::Rectangle<int> {
                tickToX (clip.startInProject()),
                row.getY() + 22,
                std::max (18, durationToWidth (clip.length())),
                rowHeight - 32
            }.getIntersection (row.reduced (3, 0));

            if (bounds.intersects (clipBounds))
                selectedClips_.push_back (ClipSelection { track.id(), clip.id(), ClipKind::midi });
        }

        for (const auto& clip : track.audioClips())
        {
            const auto clipBounds = juce::Rectangle<int> {
                tickToX (clip.startInProject()),
                row.getY() + 22,
                std::max (18, durationToWidth (clip.length())),
                rowHeight - 32
            }.getIntersection (row.reduced (3, 0));

            if (bounds.intersects (clipBounds))
                selectedClips_.push_back (ClipSelection { track.id(), clip.id(), ClipKind::audio });
        }
    }

    if (! selectedClips_.empty())
        selectedClip_ = selectedClips_.front();
}

void TimelineComponent::selectStructureRegionsInMarquee (juce::Rectangle<int> bounds)
{
    if (bounds.getWidth() < 3 || bounds.getHeight() < 3)
        return;

    selectedStructureRegion_.reset();
    selectedStructureRegions_.clear();

    const auto& structure = appServices_.project().musicalStructure();
    const auto& timeSignatureMap = appServices_.project().timeSignatureMap();

    const auto addIfIntersects = [this, bounds] (StructureRegionSelection selection,
                                                 StructureLane lane,
                                                 core::time::TickPosition start,
                                                 core::time::TickPosition end)
    {
        const auto regionBounds = juce::Rectangle<int> {
            tickToX (start),
            structureLaneBounds (lane).getY() + 3,
            std::max (12, durationToWidth (end - start)),
            structureLaneBounds (lane).getHeight() - 6
        }.getIntersection (structureLaneBounds (lane).reduced (2, 2));

        if (bounds.intersects (regionBounds))
            selectedStructureRegions_.push_back (selection);
    };

    if (structure.keyCenterRegions().empty())
    {
        addIfIntersects (StructureRegionSelection { StructureRegionKind::keyCenter, -1 },
                         StructureLane::keyCenter,
                         core::time::TickPosition {},
                         core::time::TickPosition {} + barSpanDuration (timeSignatureMap, core::time::TickPosition {}, 8));
    }
    else
    {
        for (int index = 0; index < static_cast<int> (structure.keyCenterRegions().size()); ++index)
        {
            const auto& region = structure.keyCenterRegions()[static_cast<std::size_t> (index)];
            addIfIntersects (StructureRegionSelection { StructureRegionKind::keyCenter, index },
                             StructureLane::keyCenter,
                             region.start(),
                             region.end());
        }
    }

    if (structure.scaleModeRegions().empty())
    {
        addIfIntersects (StructureRegionSelection { StructureRegionKind::scaleMode, -1 },
                         StructureLane::scaleMode,
                         core::time::TickPosition {},
                         core::time::TickPosition {} + barSpanDuration (timeSignatureMap, core::time::TickPosition {}, 8));
    }
    else
    {
        for (int index = 0; index < static_cast<int> (structure.scaleModeRegions().size()); ++index)
        {
            const auto& region = structure.scaleModeRegions()[static_cast<std::size_t> (index)];
            addIfIntersects (StructureRegionSelection { StructureRegionKind::scaleMode, index },
                             StructureLane::scaleMode,
                             region.start(),
                             region.end());
        }
    }

    for (int index = 0; index < static_cast<int> (structure.chordRegions().size()); ++index)
    {
        const auto& region = structure.chordRegions()[static_cast<std::size_t> (index)];
        addIfIntersects (StructureRegionSelection { StructureRegionKind::chordProgression, index },
                         StructureLane::chordProgression,
                         region.start(),
                         region.end());
    }

    if (! selectedStructureRegions_.empty())
        selectedStructureRegion_ = selectedStructureRegions_.front();
}

bool TimelineComponent::wouldOverlap (const std::string& trackId,
                                      const std::string& ignoredClipId,
                                      core::time::TickPosition startInProject,
                                      core::time::TickDuration length) const
{
    const auto* track = appServices_.project().findTrackById (trackId);
    if (track == nullptr)
        return false;

    const auto candidate = core::sequencing::Region { startInProject, startInProject + length };
    const auto overlapsMidi = std::any_of (track->clips().begin(), track->clips().end(), [&] (const auto& clip) {
        return clip.id() != ignoredClipId && clip.projectRegion().intersects (candidate);
    });

    if (overlapsMidi)
        return true;

    return std::any_of (track->audioClips().begin(), track->audioClips().end(), [&] (const auto& clip) {
        return clip.id() != ignoredClipId
            && core::sequencing::Region { clip.startInProject(), clip.endInProject() }.intersects (candidate);
    });
}

const juce::String& TimelineComponent::automationTargetLabelFor (const core::sequencing::AutomationTarget& target)
{
    auto returnTrackName = std::string {};
    if (target.kind == core::sequencing::AutomationTargetKind::sendLevel)
        if (const auto* returnTrack = appServices_.project().findTrackById (target.sendTargetTrackId))
            returnTrackName = returnTrack->name();

    const auto match = std::find_if (automationLabelCache_.begin(), automationLabelCache_.end(), [&target, &returnTrackName] (const auto& entry) {
        return entry.target == target && entry.returnTrackName == returnTrackName;
    });
    if (match != automationLabelCache_.end())
        return match->label;

    automationLabelCache_.push_back (AutomationLabelCacheEntry {
        target,
        returnTrackName,
        automationTargetLabel (appServices_.project(), target)
    });
    return automationLabelCache_.back().label;
}

int TimelineComponent::tickToX (core::time::TickPosition position) const noexcept
{
    const auto pixels = (static_cast<double> (position.ticks()) / ticksPerBeat) * pixelsPerQuarter();
    return horizontalPadding + static_cast<int> (std::llround (pixels));
}

int TimelineComponent::durationToWidth (core::time::TickDuration duration) const noexcept
{
    const auto pixels = (static_cast<double> (duration.ticks()) / ticksPerBeat) * pixelsPerQuarter();
    return static_cast<int> (std::llround (pixels));
}

core::time::TickPosition TimelineComponent::xToSnappedTick (int x) const noexcept
{
    const auto timelineX = std::max (0, x - horizontalPadding);
    const auto rawTicks = static_cast<std::int64_t> (std::llround ((static_cast<double> (timelineX) / pixelsPerQuarter()) * ticksPerBeat));
    return snappedPosition (rawTicks);
}

core::time::TickPosition TimelineComponent::snappedPosition (std::int64_t ticks) const noexcept
{
    return core::time::TickPosition::fromTicks (roundToGrid (ticks));
}

core::time::TickDuration TimelineComponent::snappedDuration (std::int64_t ticks) const noexcept
{
    return core::time::TickDuration::fromTicks (std::max<std::int64_t> (ticksPerBeat, roundToGrid (ticks)));
}

core::time::TickPosition TimelineComponent::timelineEndTick() const
{
    return appServices_.project().timeSignatureMap().tickAtBar (visibleTimelineBars_ + 1);
}

double TimelineComponent::pixelsPerQuarter() const noexcept
{
    const auto timelineBeats = std::max (1.0, static_cast<double> (timelineEndTick().ticks()) / ticksPerBeat);
    const auto availableWidth = std::max (1, getWidth() - (horizontalPadding * 2));
    return static_cast<double> (availableWidth) / timelineBeats;
}

void TimelineComponent::ensureTimelineFits (core::time::TickPosition endPosition)
{
    auto changed = false;
    while (endPosition > timelineEndTick())
    {
        visibleTimelineBars_ += timelineExtensionBars;
        changed = true;
    }

    if (changed)
        playheadPixelValid_ = false;
}

void TimelineComponent::ensureTimelineFitsAllClips()
{
    for (const auto& track : appServices_.project().tracks())
    {
        for (const auto& clip : track.clips())
            ensureTimelineFits (clip.endInProject());

        for (const auto& clip : track.audioClips())
            ensureTimelineFits (clip.endInProject());
    }
}

core::music_theory::ScaleLibrary TimelineComponent::scaleLibraryForProject() const
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

juce::String TimelineComponent::formatBarBeat (core::time::TickPosition position) const
{
    const auto safePosition = core::time::TickPosition::fromTicks (std::max<std::int64_t> (0, position.ticks()));
    const auto barBeat = appServices_.project().timeSignatureMap().fromTicks (safePosition);

    std::ostringstream stream;
    stream << barBeat.bar << "." << barBeat.beat;
    if (barBeat.tickOffset.ticks() != 0)
        stream << "." << barBeat.tickOffset.ticks();

    return stream.str();
}
}
