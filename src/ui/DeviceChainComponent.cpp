#include "ui/DeviceChainComponent.h"

#include "app/AppServices.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserDragPayload.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>
#include <utility>
#include <vector>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff121820 };
const auto panelColour = juce::Colour { 0xff19212b };
const auto raisedColour = juce::Colour { 0xff202b37 };
const auto cardColour = juce::Colour { 0xff243140 };
const auto mutedCardColour = juce::Colour { 0xff1b222b };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff93a1b0 };
const auto accentColour = juce::Colour { 0xff5bbad5 };
const auto warningColour = juce::Colour { 0xffe1a75c };
const auto insertPreviewColour = juce::Colour { 0xff7fdba6 };

constexpr int sourceWidth = 104;
constexpr int deviceCardWidth = 218;
constexpr int firstPartyDeviceCardWidth = 1020;
constexpr int firstPartyEffectCardWidth = 396;
constexpr int firstPartyTapeCardWidth = 428;
constexpr int deviceCardHeight = 78;
constexpr int firstPartyDeviceCardHeight = 250;
constexpr int arrowWidth = 28;
constexpr int chainPadding = 10;
constexpr auto deviceDragPayloadTypeProperty = "tsqPayloadType";
constexpr auto deviceDragPayloadType = "deviceSlot";

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

std::string displayNameForPlugin (const core::sequencing::PluginReference& plugin)
{
    if (! plugin.pluginName.empty())
        return plugin.pluginName;

    if (! plugin.fileOrIdentifier.empty())
        return plugin.fileOrIdentifier;

    return "Plugin";
}

std::string displayNameForFirstPartyDevice (const core::sequencing::FirstPartyDeviceState& device)
{
    if (const auto* definition = core::devices::findFirstPartyDeviceDefinition (device.typeId))
        return definition->name;

    return device.typeId.empty() ? "Native Device" : device.typeId;
}

std::string kindText (core::sequencing::PluginKind kind)
{
    switch (kind)
    {
        case core::sequencing::PluginKind::instrument: return "Instrument";
        case core::sequencing::PluginKind::audioEffect: return "Audio FX";
        case core::sequencing::PluginKind::midiEffect: return "MIDI FX";
        case core::sequencing::PluginKind::unknown: break;
    }

    return "Device";
}

std::string trackTypeText (core::sequencing::TrackType type)
{
    switch (type)
    {
        case core::sequencing::TrackType::midi: return "MIDI";
        case core::sequencing::TrackType::audio: return "Audio";
        case core::sequencing::TrackType::returnTrack: return "Return";
        case core::sequencing::TrackType::master: return "Master";
    }

    return "Track";
}

std::string sourceText (core::sequencing::TrackType type)
{
    switch (type)
    {
        case core::sequencing::TrackType::midi: return "MIDI";
        case core::sequencing::TrackType::audio: return "Audio";
        case core::sequencing::TrackType::returnTrack: return "Return Input";
        case core::sequencing::TrackType::master: return "Mix";
    }

    return "Input";
}

std::string flowText (core::sequencing::TrackType type)
{
    switch (type)
    {
        case core::sequencing::TrackType::midi: return "MIDI -> Instrument -> Audio FX";
        case core::sequencing::TrackType::audio: return "Audio -> Audio FX";
        case core::sequencing::TrackType::returnTrack: return "Return Input -> FX";
        case core::sequencing::TrackType::master: return "Mix -> Master FX";
    }

    return {};
}

std::string emptyText (core::sequencing::TrackType type)
{
    switch (type)
    {
        case core::sequencing::TrackType::midi: return "Drop an instrument or audio effect here";
        case core::sequencing::TrackType::audio: return "Drop an audio effect here";
        case core::sequencing::TrackType::returnTrack: return "Drop a return effect here";
        case core::sequencing::TrackType::master: return "Drop a master effect here";
    }

    return "Drop a plugin here";
}

std::string channelSummary (const core::sequencing::PluginReference& plugin)
{
    if (plugin.numInputChannels <= 0 && plugin.numOutputChannels <= 0)
        return {};

    if (plugin.numInputChannels <= 0)
        return std::to_string (plugin.numOutputChannels) + " out";

    if (plugin.numOutputChannels <= 0)
        return std::to_string (plugin.numInputChannels) + " in";

    return std::to_string (plugin.numInputChannels) + " in / " + std::to_string (plugin.numOutputChannels) + " out";
}

std::string detailText (const core::sequencing::DeviceSlot& slot, bool legacy)
{
    if (slot.isFirstPartyDevice())
    {
        std::vector<std::string> parts {
            "TheorySequencer",
            "Native " + kindText (slot.kind())
        };

        if (slot.firstPartyDevice().has_value())
            parts.push_back ("Patch v" + std::to_string (slot.firstPartyDevice()->patchVersion));

        std::ostringstream text;
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            if (index > 0)
                text << " - ";

            text << parts[index];
        }

        return text.str();
    }

    std::vector<std::string> parts;
    if (! slot.plugin().manufacturer.empty())
        parts.push_back (slot.plugin().manufacturer);

    parts.push_back (kindText (slot.kind()));

    if (const auto channels = channelSummary (slot.plugin()); ! channels.empty())
        parts.push_back (channels);

    if (! slot.pluginStateFile().empty())
        parts.push_back ("State saved");

    if (legacy)
        parts.push_back ("Legacy");

    std::ostringstream text;
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        if (index > 0)
            text << " - ";

        text << parts[index];
    }

    return text.str();
}

double actualValueForParameter (const core::devices::FirstPartyParameterDefinition& parameter, double normalizedValue)
{
    const auto actual = parameter.minimumValue + (std::clamp (normalizedValue, 0.0, 1.0) * (parameter.maximumValue - parameter.minimumValue));
    if (parameter.valueType == core::devices::FirstPartyParameterValueType::discrete)
        return std::round (actual);

    return actual;
}

std::string parameterValueText (const core::devices::FirstPartyParameterDefinition& parameter, double normalizedValue)
{
    const auto actual = actualValueForParameter (parameter, normalizedValue);
    if (parameter.id == "osc.mod.ratio")
    {
        static const std::vector<std::string> ratios { "1:4", "1:3", "1:2", "1:1", "2:1", "3:1", "4:1", "5:1" };
        const auto index = std::clamp (static_cast<int> (actual), 0, static_cast<int> (ratios.size()) - 1);
        return ratios[static_cast<std::size_t> (index)];
    }

    const auto isUnitRange = parameter.minimumValue == 0.0 && parameter.maximumValue == 1.0;
    const auto isNativeEffectPercent = isUnitRange
        && (parameter.id.rfind ("phaser.", 0) == 0
            || parameter.id.rfind ("reverb.", 0) == 0
            || parameter.id.rfind ("tape.", 0) == 0);
    if (isNativeEffectPercent)
        return juce::String { actual * 100.0, 0 }.toStdString() + "%";

    if (parameter.valueType == core::devices::FirstPartyParameterValueType::discrete)
        return std::to_string (static_cast<int> (actual)) + (parameter.units.empty() ? "" : " " + parameter.units);

    const auto decimals = parameter.units == "s" || parameter.units == "st" || parameter.units == "Hz" ? 2 : 0;
    juce::String value { actual, decimals };
    return value.toStdString() + (parameter.units.empty() ? "" : " " + parameter.units);
}

std::string compactParameterName (const core::devices::FirstPartyParameterDefinition& parameter)
{
    if (parameter.id == "amp.level") return "Level";
    if (parameter.id == "osc.pm.amount") return "PM";
    if (parameter.id == "osc.mod.ratio") return "Ratio";
    if (parameter.id == "wavefolder.amount") return "Amount";
    if (parameter.id == "wavefolder.stages") return "Stages";
    if (parameter.id == "amp.attack") return "Attack";
    if (parameter.id == "amp.decay") return "Decay";
    if (parameter.id == "amp.sustain") return "Sustain";
    if (parameter.id == "amp.release") return "Release";
    if (parameter.id == "phaser.amount") return "Amount";
    if (parameter.id == "phaser.speed") return "Speed";
    if (parameter.id == "reverb.mix") return "Dry/Wet";
    if (parameter.id == "reverb.decay") return "Decay";
    if (parameter.id == "tape.drive") return "Drive";
    if (parameter.id == "tape.instability") return "Instability";
    if (parameter.id == "tape.wear") return "Wear";
    if (parameter.id == "tape.noise") return "Noise";
    if (parameter.id == "tape.mix") return "Mix";
    return parameter.name;
}

int widthForSlotView (const std::string& firstPartyTypeId)
{
    if (firstPartyTypeId == core::devices::simpleOscComplexTypeId())
        return firstPartyDeviceCardWidth;

    if (firstPartyTypeId == core::devices::nativeTapeSimulatorTypeId())
        return firstPartyTapeCardWidth;

    return firstPartyEffectCardWidth;
}

void styleModeLabel (juce::Label& label, float size, juce::Colour colour, bool bold = false)
{
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, colour);
    label.setFont (juce::FontOptions { size, bold ? juce::Font::bold : 0 });
}

void drawArrow (juce::Graphics& graphics, juce::Point<float> start, juce::Point<float> end)
{
    const auto midY = start.y;
    const auto arrowTipX = end.x - 5.0f;

    graphics.setColour (outlineColour.brighter (0.18f));
    graphics.drawLine (start.x + 5.0f, midY, arrowTipX, midY, 1.5f);

    juce::Path arrow;
    arrow.startNewSubPath (arrowTipX, midY - 4.0f);
    arrow.lineTo (end.x, midY);
    arrow.lineTo (arrowTipX, midY + 4.0f);
    arrow.closeSubPath();
    graphics.fillPath (arrow);
}

juce::var makeDeviceDragPayload (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    juce::var payload { new juce::DynamicObject {} };
    auto* object = payload.getDynamicObject();
    object->setProperty (deviceDragPayloadTypeProperty, deviceDragPayloadType);
    object->setProperty ("trackId", toJuceString (trackId));
    object->setProperty ("slotId", toJuceString (slotId.value));
    return payload;
}

std::optional<std::pair<std::string, core::sequencing::DeviceSlotId>> deviceDragPayloadFromVar (const juce::var& payload)
{
    const auto* object = payload.getDynamicObject();
    if (object == nullptr || object->getProperty (deviceDragPayloadTypeProperty).toString() != deviceDragPayloadType)
        return std::nullopt;

    const auto trackId = object->getProperty ("trackId").toString().toStdString();
    const auto slotId = object->getProperty ("slotId").toString().toStdString();
    if (trackId.empty() || slotId.empty())
        return std::nullopt;

    return std::pair<std::string, core::sequencing::DeviceSlotId> { trackId, core::sequencing::DeviceSlotId { slotId } };
}
}

class DeviceChainComponent::ChainContentComponent final : public juce::Component,
                                                          public juce::DragAndDropTarget
{
private:
    struct DeviceSlotView
    {
        std::string trackId;
        core::sequencing::DeviceSlotId slotId;
        std::size_t index = 0;
        std::string name;
        std::string kind;
        std::string detail;
        bool bypassed = false;
        bool commandBacked = true;
        bool canOpenEditor = false;
        bool isFirstParty = false;
        std::string firstPartyTypeId;
        std::vector<core::sequencing::FirstPartyDeviceParameterValue> firstPartyParameters;
    };

    static bool slotViewsEqual (const DeviceSlotView& lhs, const DeviceSlotView& rhs)
    {
        return lhs.trackId == rhs.trackId
            && lhs.slotId == rhs.slotId
            && lhs.index == rhs.index
            && lhs.name == rhs.name
            && lhs.kind == rhs.kind
            && lhs.detail == rhs.detail
            && lhs.bypassed == rhs.bypassed
            && lhs.commandBacked == rhs.commandBacked
            && lhs.canOpenEditor == rhs.canOpenEditor
            && lhs.isFirstParty == rhs.isFirstParty
            && lhs.firstPartyTypeId == rhs.firstPartyTypeId
            && lhs.firstPartyParameters == rhs.firstPartyParameters;
    }

    static bool slotViewsEqual (const std::vector<DeviceSlotView>& lhs, const std::vector<DeviceSlotView>& rhs)
    {
        return lhs.size() == rhs.size()
            && std::equal (lhs.begin(), lhs.end(), rhs.begin(), [] (const auto& left, const auto& right)
            {
                return slotViewsEqual (left, right);
            });
    }

public:
    explicit ChainContentComponent (DeviceChainComponent& owner)
        : owner_ (owner)
    {
    }

    bool refreshModel()
    {
        core::diagnostics::ScopedPerformanceTimer timer { "DeviceChainContent::refreshModel" };

        const auto* track = selectedTrack();
        std::string nextTrackId;
        std::string nextSourceLabel;
        std::string nextEmptyLabel;
        std::vector<DeviceSlotView> nextSlots;

        if (track != nullptr)
        {
            nextTrackId = track->id();
            nextSourceLabel = sourceText (track->type());
            nextEmptyLabel = emptyText (track->type());

            for (const auto& slot : track->deviceChain().slots())
                nextSlots.push_back (slotViewFor (*track, slot, true, nextSlots.size()));

            if (nextSlots.empty() && track->instrument().has_value())
            {
                core::sequencing::DeviceSlot legacySlot {
                    core::sequencing::DeviceSlotId { "instrument" },
                    core::sequencing::PluginReference::fromTrackInstrumentReference (*track->instrument()),
                    core::sequencing::PluginKind::instrument
                };
                legacySlot.setPluginStateFile (track->instrument()->pluginStateFile);
                nextSlots.push_back (slotViewFor (*track, legacySlot, false, nextSlots.size()));
            }
        }

        const auto hadDropPreview = dropPreviewEmpty_ || dropPreviewInsertIndex_ >= 0 || dropPreviewReplaceIndex_ >= 0;
        const auto modelUnchanged = trackId_ == nextTrackId
            && sourceLabel_ == nextSourceLabel
            && emptyLabel_ == nextEmptyLabel
            && slotViewsEqual (slots_, nextSlots);

        if (modelUnchanged && ! hadDropPreview)
            return false;

        trackId_ = std::move (nextTrackId);
        sourceLabel_ = std::move (nextSourceLabel);
        emptyLabel_ = std::move (nextEmptyLabel);
        slots_ = std::move (nextSlots);
        dropPreviewEmpty_ = false;
        dropPreviewInsertIndex_ = -1;
        dropPreviewReplaceIndex_ = -1;

        auto changed = rebuildCardsIfNeeded();
        for (std::size_t index = 0; index < slots_.size(); ++index)
            changed = deviceCards_[index]->update (slots_[index], false) || changed;

        resized();
        if (changed || hadDropPreview)
            repaint();

        return changed || hadDropPreview;
    }

    int requiredWidth() const
    {
        if (trackId_.empty())
            return 320;

        auto width = (chainPadding * 2) + sourceWidth + arrowWidth;
        if (slots_.empty())
            return width + deviceCardWidth;

        for (const auto& slot : slots_)
            width += (slot.isFirstParty ? widthForSlotView (slot.firstPartyTypeId) : deviceCardWidth) + arrowWidth;

        return width - arrowWidth;
    }

    void paint (juce::Graphics& graphics) override
    {
        core::diagnostics::ScopedPerformanceTimer timer { "DeviceChainContent::paint" };

        graphics.fillAll (panelColour);

        if (trackId_.empty())
        {
            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 13.0f });
            graphics.drawFittedText ("Select a track to view its device chain",
                                     getLocalBounds().reduced (12),
                                     juce::Justification::centred,
                                     2);
            return;
        }

        const auto sourceBounds = sourceBlockBounds();
        graphics.setColour (raisedColour);
        graphics.fillRoundedRectangle (sourceBounds.toFloat(), 6.0f);
        graphics.setColour (outlineColour);
        graphics.drawRoundedRectangle (sourceBounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
        graphics.setColour (textColour);
        graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        graphics.drawFittedText (toJuceString (sourceLabel_), sourceBounds.reduced (8), juce::Justification::centred, 2);

        if (deviceCards_.empty())
        {
            const auto emptyBounds = emptyBlockBounds();
            drawArrow (graphics,
                       juce::Point<float> { static_cast<float> (sourceBounds.getRight()), static_cast<float> (sourceBounds.getCentreY()) },
                       juce::Point<float> { static_cast<float> (emptyBounds.getX()), static_cast<float> (emptyBounds.getCentreY()) });

            graphics.setColour (dropPreviewEmpty_ ? accentColour.withAlpha (0.16f) : surfaceColour);
            graphics.fillRoundedRectangle (emptyBounds.toFloat(), 6.0f);
            graphics.setColour (dropPreviewEmpty_ ? accentColour : outlineColour);
            graphics.drawRoundedRectangle (emptyBounds.toFloat().reduced (0.5f), 6.0f, dropPreviewEmpty_ ? 2.0f : 1.0f);
            graphics.setColour (dropPreviewEmpty_ ? textColour : mutedTextColour);
            graphics.setFont (juce::FontOptions { 12.5f, juce::Font::bold });
            graphics.drawFittedText (toJuceString (emptyLabel_), emptyBounds.reduced (10), juce::Justification::centred, 2);
            return;
        }

        auto previous = sourceBounds;
        for (const auto& card : deviceCards_)
        {
            if (card != nullptr)
            {
                drawArrow (graphics,
                           juce::Point<float> { static_cast<float> (previous.getRight()), static_cast<float> (previous.getCentreY()) },
                           juce::Point<float> { static_cast<float> (card->getX()), static_cast<float> (card->getBounds().getCentreY()) });
                previous = card->getBounds();
            }
        }

        if (dropPreviewInsertIndex_ >= 0)
        {
            const auto x = insertionXForIndex (static_cast<std::size_t> (dropPreviewInsertIndex_));
            const auto top = static_cast<float> (cardTop() + 3);
            const auto bottom = static_cast<float> (cardTop() + cardHeight() - 3);
            graphics.setColour (insertPreviewColour);
            graphics.drawLine (static_cast<float> (x), top, static_cast<float> (x), bottom, 3.0f);
            graphics.fillEllipse (static_cast<float> (x - 4), top - 4.0f, 8.0f, 8.0f);
            graphics.fillEllipse (static_cast<float> (x - 4), bottom - 4.0f, 8.0f, 8.0f);
        }
    }

    void resized() override
    {
        if (trackId_.empty())
            return;

        auto x = sourceBlockBounds().getRight() + arrowWidth;
        const auto y = cardTop();
        const auto height = cardHeight();

        for (std::size_t index = 0; index < deviceCards_.size(); ++index)
        {
            const auto width = index < slots_.size() && slots_[index].isFirstParty ? widthForSlotView (slots_[index].firstPartyTypeId)
                                                                                   : deviceCardWidth;
            deviceCards_[index]->setBounds (x, y, width, height);
            x += width + arrowWidth;
        }
    }

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const auto payload = pluginDragPayloadFromVar (details.description);
        if (payload.has_value())
            return owner_.pluginDropKindForSelectedTrack (*payload).has_value();

        const auto firstPartyPayload = firstPartyDeviceDragPayloadFromVar (details.description);
        if (firstPartyPayload.has_value())
            return owner_.firstPartyDeviceDropKindForSelectedTrack (*firstPartyPayload).has_value();

        return deviceDragPayloadFromVar (details.description).has_value();
    }

    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateDropPreview (details);
    }

    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateDropPreview (details);
    }

    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        clearDropPreview();
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const auto insertIndex = insertIndexForPosition (details.localPosition);
        const auto replaceIndex = replacementIndexForPosition (details.localPosition);

        if (const auto payload = pluginDragPayloadFromVar (details.description))
        {
            if (replaceIndex.has_value() && *replaceIndex < slots_.size() && slots_[*replaceIndex].commandBacked)
                owner_.replaceDeviceWithPluginPayload (slots_[*replaceIndex].trackId, slots_[*replaceIndex].slotId, *payload);
            else
                owner_.insertPluginPayloadIntoSelectedTrack (*payload, insertIndex);
        }
        else if (const auto payload = firstPartyDeviceDragPayloadFromVar (details.description))
        {
            owner_.insertFirstPartyDevicePayloadIntoSelectedTrack (*payload, insertIndex);
        }
        else if (const auto devicePayload = deviceDragPayloadFromVar (details.description))
        {
            if (devicePayload->first == trackId_)
                owner_.moveDevice (devicePayload->first, devicePayload->second, insertIndexForReorder (devicePayload->second, insertIndex));
            else
                owner_.appServices_.reportWarning ("Device reorder failed: drag devices within the same track");
        }

        clearDropPreview();
        owner_.refresh();
    }

private:
    class DeviceCardComponent final : public juce::Component
    {
    public:
        explicit DeviceCardComponent (ChainContentComponent& owner)
            : owner_ (owner)
        {
            enableButton_.setClickingTogglesState (false);
            enableButton_.setColour (juce::TextButton::buttonColourId, accentColour.withAlpha (0.22f));
            enableButton_.setColour (juce::TextButton::textColourOffId, textColour);
            enableButton_.setColour (juce::TextButton::textColourOnId, textColour);
            enableButton_.onClick = [this]
            {
                if (view_.commandBacked)
                    owner_.setDeviceBypassed (view_.trackId, view_.slotId, ! view_.bypassed);
            };
            addAndMakeVisible (enableButton_);

            openEditorButton_.setButtonText ("Edit");
            openEditorButton_.setTooltip ("Open plugin editor");
            openEditorButton_.setColour (juce::TextButton::buttonColourId, outlineColour);
            openEditorButton_.setColour (juce::TextButton::textColourOffId, textColour);
            openEditorButton_.onClick = [this]
            {
                if (view_.canOpenEditor)
                    owner_.openDeviceEditor (view_.trackId, view_.slotId);
            };
            addAndMakeVisible (openEditorButton_);

            removeButton_.setButtonText ("X");
            removeButton_.setTooltip ("Remove device");
            removeButton_.setColour (juce::TextButton::buttonColourId, outlineColour);
            removeButton_.setColour (juce::TextButton::textColourOffId, textColour);
            removeButton_.onClick = [this]
            {
                if (view_.commandBacked)
                    owner_.removeDevice (view_.trackId, view_.slotId);
            };
            addAndMakeVisible (removeButton_);
        }

        bool update (DeviceSlotView view, bool replacePreview)
        {
            const auto controlsNeedRebuild = view_.isFirstParty != view.isFirstParty
                || view_.firstPartyTypeId != view.firstPartyTypeId;

            if (slotViewsEqual (view_, view) && replacePreview_ == replacePreview && ! controlsNeedRebuild)
                return false;

            const auto previousFirstParty = view_.isFirstParty;
            view_ = std::move (view);
            replacePreview_ = replacePreview;
            setTitle (toJuceString (view_.name + " device"));
            setDescription (toJuceString (view_.kind + ". " + view_.detail));
            enableButton_.setButtonText (view_.bypassed ? "Off" : "On");
            enableButton_.setTitle (toJuceString ((view_.bypassed ? "Enable " : "Bypass ") + view_.name));
            enableButton_.setTooltip (view_.commandBacked ? juce::String { "Enable or bypass " } + toJuceString (view_.name)
                                                          : juce::String { "Legacy instrument bypass is not command-backed" });
            enableButton_.setEnabled (view_.commandBacked);
            openEditorButton_.setTitle (toJuceString ("Open " + view_.name + " editor"));
            openEditorButton_.setEnabled (view_.canOpenEditor);
            openEditorButton_.setVisible (! view_.isFirstParty);
            removeButton_.setTitle (toJuceString ("Remove " + view_.name));
            removeButton_.setEnabled (view_.commandBacked);
            if (controlsNeedRebuild || previousFirstParty != view_.isFirstParty)
                rebuildFirstPartyControls();
            syncFirstPartyControls();
            resized();
            repaint();
            return true;
        }

        void paint (juce::Graphics& graphics) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            graphics.setColour (view_.bypassed ? mutedCardColour : cardColour);
            graphics.fillRoundedRectangle (bounds, 6.0f);
            graphics.setColour (replacePreview_ ? insertPreviewColour : (view_.bypassed ? warningColour.withAlpha (0.7f) : outlineColour));
            graphics.drawRoundedRectangle (bounds, 6.0f, replacePreview_ ? 2.0f : 1.0f);

            auto textBounds = getLocalBounds().reduced (10, 8);
            textBounds.removeFromRight (view_.isFirstParty ? 88 : 116);

            graphics.setColour (view_.bypassed ? mutedTextColour : textColour);
            graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
            graphics.drawFittedText (toJuceString (view_.name), textBounds.removeFromTop (20), juce::Justification::centredLeft, 1);

            graphics.setColour (accentColour);
            graphics.setFont (juce::FontOptions { 11.5f, juce::Font::bold });
            graphics.drawFittedText (toJuceString (view_.kind), textBounds.removeFromTop (18), juce::Justification::centredLeft, 1);

            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 11.0f });
            graphics.drawFittedText (toJuceString (view_.detail), textBounds.removeFromTop (view_.isFirstParty ? 16 : textBounds.getHeight()), juce::Justification::centredLeft, 2);

            if (! view_.isFirstParty)
                return;

            if (view_.firstPartyTypeId == core::devices::simpleOscComplexTypeId())
            {
                drawEnvelopePreview (graphics);
                drawSectionLabel (graphics, oscillatorBounds_, "Oscillator");
                drawSectionLabel (graphics, folderBounds_, "Wavefolder");
                drawSectionLabel (graphics, envelopeBounds_, "Amp Envelope");
            }
            else
            {
                drawSectionLabel (graphics, effectBounds_, "Controls");
            }
        }

        void resized() override
        {
            auto controls = getLocalBounds().reduced (8);
            auto right = controls.removeFromRight (96);
            auto top = right.removeFromTop (25);
            removeButton_.setBounds (top.removeFromRight (28));
            top.removeFromRight (4);
            enableButton_.setBounds (top);
            right.removeFromTop (6);
            openEditorButton_.setBounds (right.removeFromTop (27));

            if (! view_.isFirstParty)
                return;

            oscillatorBounds_ = {};
            folderBounds_ = {};
            envelopeBounds_ = {};
            envelopePreviewBounds_ = {};
            effectBounds_ = {};

            auto body = getLocalBounds().reduced (10, 8);
            body.removeFromTop (50);
            body.removeFromRight (86);

            if (view_.firstPartyTypeId != core::devices::simpleOscComplexTypeId())
            {
                effectBounds_ = body;
                const auto* definition = core::devices::findFirstPartyDeviceDefinition (view_.firstPartyTypeId);
                if (definition == nullptr)
                    return;

                std::vector<std::string> parameterIds;
                parameterIds.reserve (definition->parameters.size());
                for (const auto& parameter : definition->parameters)
                    parameterIds.push_back (parameter.id);

                layoutControls (effectBounds_.withTrimmedTop (18),
                                parameterIds,
                                86,
                                58,
                                24,
                                7);
                return;
            }

            oscillatorBounds_ = body.removeFromLeft (320);
            body.removeFromLeft (16);
            folderBounds_ = body.removeFromLeft (250);
            body.removeFromLeft (16);
            envelopeBounds_ = body;

            layoutControls (oscillatorBounds_.withTrimmedTop (18),
                            { "pitch", "amp.level", "osc.mod.ratio", "osc.pm.amount" },
                            66,
                            54);
            layoutControls (folderBounds_.withTrimmedTop (18),
                            { "wavefolder.stages", "wavefolder.amount" },
                            64,
                            58);

            auto envelopeControls = envelopeBounds_.withTrimmedTop (18);
            envelopePreviewBounds_ = envelopeControls.removeFromTop (std::clamp (envelopeControls.getHeight() - 106, 46, 68));
            envelopeControls.removeFromTop (8);
            layoutControls (envelopeControls,
                            { "amp.attack", "amp.decay", "amp.sustain", "amp.release" },
                            58,
                            54,
                            20,
                            2);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            dragStarted_ = false;
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (dragStarted_ || ! view_.commandBacked || event.getDistanceFromDragStart() < 5)
                return;

            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                dragStarted_ = true;
                container->startDragging (makeDeviceDragPayload (view_.trackId, view_.slotId), this);
            }
        }

    private:
        struct NativeParameterControl
        {
            std::string parameterId;
            juce::Label label;
            juce::Slider slider;
            juce::Label valueLabel;
            bool dragActive = false;
        };

        const core::devices::FirstPartyParameterDefinition* definitionForParameter (const std::string& parameterId) const
        {
            const auto* definition = core::devices::findFirstPartyDeviceDefinition (view_.firstPartyTypeId);
            if (definition == nullptr)
                return nullptr;

            const auto match = std::find_if (definition->parameters.begin(), definition->parameters.end(), [&parameterId] (const auto& parameter)
            {
                return parameter.id == parameterId;
            });

            return match == definition->parameters.end() ? nullptr : &*match;
        }

        double currentNormalizedValue (const std::string& parameterId) const
        {
            const auto match = std::find_if (view_.firstPartyParameters.begin(), view_.firstPartyParameters.end(), [&parameterId] (const auto& parameter)
            {
                return parameter.parameterId == parameterId;
            });

            if (match != view_.firstPartyParameters.end())
                return std::clamp (match->normalizedValue, 0.0, 1.0);

            if (const auto* parameter = definitionForParameter (parameterId))
                return std::clamp (parameter->defaultNormalizedValue, 0.0, 1.0);

            return 0.0;
        }

        double displayedNormalizedValue (const std::string& parameterId) const
        {
            if (auto* control = controlForParameter (parameterId))
                return std::clamp (control->slider.getValue(), 0.0, 1.0);

            return currentNormalizedValue (parameterId);
        }

        void rebuildFirstPartyControls()
        {
            for (auto& control : parameterControls_)
            {
                removeChildComponent (&control->label);
                removeChildComponent (&control->slider);
                removeChildComponent (&control->valueLabel);
            }
            parameterControls_.clear();

            if (! view_.isFirstParty)
                return;

            const auto* definition = core::devices::findFirstPartyDeviceDefinition (view_.firstPartyTypeId);
            if (definition == nullptr)
                return;

            for (const auto& parameter : definition->parameters)
            {
                auto control = std::make_unique<NativeParameterControl>();
                control->parameterId = parameter.id;
                control->label.setText (toJuceString (compactParameterName (parameter)), juce::dontSendNotification);
                control->label.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
                control->label.setColour (juce::Label::textColourId, mutedTextColour);
                control->label.setJustificationType (juce::Justification::centredLeft);
                control->label.setTooltip (toJuceString (parameter.name));
                addAndMakeVisible (control->label);

                control->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                control->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                control->slider.setRange (0.0, 1.0, parameter.valueType == core::devices::FirstPartyParameterValueType::discrete ? 1.0 / std::max (1.0, parameter.maximumValue - parameter.minimumValue) : 0.001);
                control->slider.setDoubleClickReturnValue (true, std::clamp (parameter.defaultNormalizedValue, 0.0, 1.0));
                control->slider.setColour (juce::Slider::trackColourId, accentColour.withAlpha (0.48f));
                control->slider.setColour (juce::Slider::thumbColourId, textColour);
                control->slider.setTooltip (toJuceString (parameter.name));
                control->slider.setTitle (toJuceString (parameter.name));
                auto* rawControl = control.get();
                control->slider.onDragStart = [rawControl] { rawControl->dragActive = true; };
                control->slider.onDragEnd = [this, rawControl]
                {
                    rawControl->dragActive = false;
                    commitFirstPartyParameter (*rawControl);
                };
                control->slider.onValueChange = [this, rawControl]
                {
                    updateControlValueLabel (*rawControl);
                    repaint();
                    if (! updatingControls_ && ! rawControl->dragActive)
                        commitFirstPartyParameter (*rawControl);
                };
                addAndMakeVisible (control->slider);

                control->valueLabel.setFont (juce::FontOptions { 10.5f });
                control->valueLabel.setColour (juce::Label::textColourId, textColour);
                control->valueLabel.setJustificationType (juce::Justification::centredRight);
                addAndMakeVisible (control->valueLabel);

                parameterControls_.push_back (std::move (control));
            }
        }

        void syncFirstPartyControls()
        {
            if (! view_.isFirstParty)
                return;

            updatingControls_ = true;
            for (auto& control : parameterControls_)
            {
                control->slider.setValue (currentNormalizedValue (control->parameterId), juce::dontSendNotification);
                updateControlValueLabel (*control);
            }
            updatingControls_ = false;
        }

        void updateControlValueLabel (NativeParameterControl& control)
        {
            const auto* parameter = definitionForParameter (control.parameterId);
            if (parameter == nullptr)
            {
                control.valueLabel.setText ({}, juce::dontSendNotification);
                return;
            }

            control.valueLabel.setText (toJuceString (parameterValueText (*parameter, control.slider.getValue())),
                                        juce::dontSendNotification);
        }

        void commitFirstPartyParameter (NativeParameterControl& control)
        {
            if (updatingControls_ || ! view_.commandBacked || ! view_.isFirstParty)
                return;

            owner_.commitFirstPartyDeviceParameter (view_.trackId,
                                                    view_.slotId,
                                                    control.parameterId,
                                                    std::clamp (control.slider.getValue(), 0.0, 1.0));
        }

        void layoutControls (juce::Rectangle<int> bounds,
                             std::initializer_list<const char*> parameterIds,
                             int labelWidth,
                             int valueWidth,
                             int rowHeight = 22,
                             int rowGap = 6)
        {
            for (const auto* parameterId : parameterIds)
            {
                auto* control = controlForParameter (parameterId);
                if (control == nullptr)
                    continue;

                auto row = bounds.removeFromTop (rowHeight);
                control->label.setBounds (row.removeFromLeft (labelWidth));
                row.removeFromLeft (4);
                control->valueLabel.setBounds (row.removeFromRight (valueWidth));
                row.removeFromRight (6);
                control->slider.setBounds (row.reduced (0, 3));
                bounds.removeFromTop (rowGap);
            }
        }

        void layoutControls (juce::Rectangle<int> bounds,
                             const std::vector<std::string>& parameterIds,
                             int labelWidth,
                             int valueWidth,
                             int rowHeight = 22,
                             int rowGap = 6)
        {
            for (const auto& parameterId : parameterIds)
            {
                auto* control = controlForParameter (parameterId);
                if (control == nullptr)
                    continue;

                auto row = bounds.removeFromTop (rowHeight);
                control->label.setBounds (row.removeFromLeft (labelWidth));
                row.removeFromLeft (4);
                control->valueLabel.setBounds (row.removeFromRight (valueWidth));
                row.removeFromRight (6);
                control->slider.setBounds (row.reduced (0, 3));
                bounds.removeFromTop (rowGap);
            }
        }

        NativeParameterControl* controlForParameter (const std::string& parameterId) const
        {
            const auto match = std::find_if (parameterControls_.begin(), parameterControls_.end(), [&parameterId] (const auto& control)
            {
                return control->parameterId == parameterId;
            });

            return match == parameterControls_.end() ? nullptr : match->get();
        }

        void drawSectionLabel (juce::Graphics& graphics, juce::Rectangle<int> bounds, const char* label)
        {
            if (bounds.isEmpty())
                return;

            graphics.setColour (accentColour);
            graphics.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
            graphics.drawFittedText (label, bounds.removeFromTop (15), juce::Justification::centredLeft, 1);
        }

        void drawEnvelopePreview (juce::Graphics& graphics)
        {
            if (envelopePreviewBounds_.isEmpty())
                return;

            auto preview = envelopePreviewBounds_;
            if (preview.getWidth() < 20 || preview.getHeight() < 20)
                return;

            graphics.setColour (juce::Colour { 0xff111720 });
            graphics.fillRoundedRectangle (preview.toFloat(), 4.0f);
            graphics.setColour (outlineColour);
            graphics.drawRoundedRectangle (preview.toFloat().reduced (0.5f), 4.0f, 1.0f);

            const auto attack = static_cast<float> (displayedNormalizedValue ("amp.attack"));
            const auto decay = static_cast<float> (displayedNormalizedValue ("amp.decay"));
            const auto sustain = static_cast<float> (displayedNormalizedValue ("amp.sustain"));
            const auto release = static_cast<float> (displayedNormalizedValue ("amp.release"));
            const auto left = static_cast<float> (preview.getX() + 8);
            const auto right = static_cast<float> (preview.getRight() - 8);
            const auto top = static_cast<float> (preview.getY() + 6);
            const auto bottom = static_cast<float> (preview.getBottom() - 6);
            const auto width = right - left;
            const auto sustainY = bottom - (sustain * (bottom - top));

            auto attackWidth = attack * 0.38f;
            auto decayWidth = 0.10f + (decay * 0.24f);
            auto releaseWidth = release * 0.40f;
            const auto stageWidth = attackWidth + decayWidth + releaseWidth;
            if (stageWidth > 0.88f)
            {
                const auto scale = 0.88f / stageWidth;
                attackWidth *= scale;
                decayWidth *= scale;
                releaseWidth *= scale;
            }

            const auto attackX = left + (width * attackWidth);
            const auto decayX = attackX + (width * decayWidth);
            const auto releaseX = right - (width * releaseWidth);
            const auto sustainEndX = std::max (decayX + 4.0f, releaseX);

            juce::Path envelope;
            envelope.startNewSubPath (left, bottom);
            envelope.lineTo (attackX, top);
            envelope.lineTo (decayX, sustainY);
            envelope.lineTo (sustainEndX, sustainY);
            envelope.lineTo (right, bottom);

            graphics.setColour (insertPreviewColour);
            graphics.strokePath (envelope, juce::PathStrokeType { 1.8f });
        }

        ChainContentComponent& owner_;
        DeviceSlotView view_;
        bool replacePreview_ = false;
        bool dragStarted_ = false;
        bool updatingControls_ = false;
        juce::TextButton enableButton_;
        juce::TextButton openEditorButton_;
        juce::TextButton removeButton_;
        juce::Rectangle<int> oscillatorBounds_;
        juce::Rectangle<int> folderBounds_;
        juce::Rectangle<int> envelopeBounds_;
        juce::Rectangle<int> envelopePreviewBounds_;
        juce::Rectangle<int> effectBounds_;
        std::vector<std::unique_ptr<NativeParameterControl>> parameterControls_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceCardComponent)
    };

    const core::sequencing::Track* selectedTrack() const
    {
        const auto& selectedTrackId = owner_.appServices_.selectedTrackId();
        if (! selectedTrackId.has_value())
            return nullptr;

        return owner_.appServices_.project().findTrackById (*selectedTrackId);
    }

    DeviceSlotView slotViewFor (const core::sequencing::Track& track,
                                const core::sequencing::DeviceSlot& slot,
                                bool commandBacked,
                                std::size_t index) const
    {
        const auto isFirstParty = slot.isFirstPartyDevice() && slot.firstPartyDevice().has_value();
        return DeviceSlotView {
            track.id(),
            slot.id(),
            index,
            isFirstParty
                ? displayNameForFirstPartyDevice (*slot.firstPartyDevice())
                : displayNameForPlugin (slot.plugin()),
            kindText (slot.kind()),
            detailText (slot, ! commandBacked),
            slot.bypassed(),
            commandBacked,
            slot.isPluginDevice() && slot.plugin().isValid(),
            isFirstParty,
            isFirstParty ? slot.firstPartyDevice()->typeId : std::string {},
            isFirstParty ? slot.firstPartyDevice()->parameterValues : std::vector<core::sequencing::FirstPartyDeviceParameterValue> {}
        };
    }

    bool rebuildCardsIfNeeded()
    {
        if (deviceCards_.size() == slots_.size())
            return false;

        deviceCards_.clear();
        for (std::size_t index = 0; index < slots_.size(); ++index)
        {
            auto card = std::make_unique<DeviceCardComponent> (*this);
            addAndMakeVisible (*card);
            deviceCards_.push_back (std::move (card));
        }

        return true;
    }

    juce::Rectangle<int> chainArea() const
    {
        return getLocalBounds().reduced (chainPadding, 6);
    }

    int cardHeight() const
    {
        const auto desiredHeight = std::any_of (slots_.begin(), slots_.end(), [] (const auto& slot) { return slot.isFirstParty; })
            ? firstPartyDeviceCardHeight
            : deviceCardHeight;
        return std::clamp (chainArea().getHeight() - 2, 58, desiredHeight);
    }

    int cardTop() const
    {
        const auto area = chainArea();
        return area.getY() + std::max (0, (area.getHeight() - cardHeight()) / 2);
    }

    juce::Rectangle<int> sourceBlockBounds() const
    {
        return { chainPadding, cardTop(), sourceWidth, cardHeight() };
    }

    juce::Rectangle<int> emptyBlockBounds() const
    {
        return { sourceBlockBounds().getRight() + arrowWidth, cardTop(), deviceCardWidth, cardHeight() };
    }

    int insertionXForIndex (std::size_t index) const
    {
        if (deviceCards_.empty())
            return emptyBlockBounds().getX();

        if (index == 0)
            return deviceCards_.front()->getX() - (arrowWidth / 2);

        if (index >= deviceCards_.size())
            return deviceCards_.back()->getRight() + (arrowWidth / 2);

        return deviceCards_[index]->getX() - (arrowWidth / 2);
    }

    std::size_t insertIndexForPosition (juce::Point<int> position) const
    {
        if (deviceCards_.empty())
            return 0;

        for (std::size_t index = 0; index < deviceCards_.size(); ++index)
        {
            const auto bounds = deviceCards_[index]->getBounds();
            if (position.x < bounds.getCentreX())
                return index;
        }

        return deviceCards_.size();
    }

    std::optional<std::size_t> replacementIndexForPosition (juce::Point<int> position) const
    {
        for (std::size_t index = 0; index < deviceCards_.size(); ++index)
        {
            const auto bounds = deviceCards_[index]->getBounds().reduced (deviceCards_[index]->getWidth() / 5, 0);
            if (bounds.contains (position))
                return index;
        }

        return std::nullopt;
    }

    std::size_t insertIndexForReorder (const core::sequencing::DeviceSlotId& draggedSlotId, std::size_t insertIndex) const
    {
        const auto source = std::find_if (slots_.begin(), slots_.end(), [&draggedSlotId] (const auto& slot)
        {
            return slot.slotId == draggedSlotId;
        });

        if (source == slots_.end())
            return insertIndex;

        const auto sourceIndex = static_cast<std::size_t> (std::distance (slots_.begin(), source));
        if (insertIndex > sourceIndex)
            --insertIndex;

        return std::min (insertIndex, slots_.empty() ? std::size_t { 0 } : slots_.size() - 1);
    }

    bool updateDropPreview (const juce::DragAndDropTarget::SourceDetails& details)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "DeviceChainContent::updateDropPreview" };

        const auto pluginPayload = pluginDragPayloadFromVar (details.description);
        const auto firstPartyPayload = firstPartyDeviceDragPayloadFromVar (details.description);
        const auto devicePayload = deviceDragPayloadFromVar (details.description);

        if (! pluginPayload.has_value() && ! firstPartyPayload.has_value() && ! devicePayload.has_value())
        {
            return clearDropPreview();
        }

        auto nextDropPreviewEmpty = deviceCards_.empty();
        auto nextDropPreviewInsertIndex = -1;
        auto nextDropPreviewReplaceIndex = -1;

        if (pluginPayload.has_value())
        {
            if (const auto replaceIndex = replacementIndexForPosition (details.localPosition);
                replaceIndex.has_value() && *replaceIndex < slots_.size() && slots_[*replaceIndex].commandBacked)
            {
                nextDropPreviewReplaceIndex = static_cast<int> (*replaceIndex);
            }
            else
            {
                nextDropPreviewInsertIndex = static_cast<int> (insertIndexForPosition (details.localPosition));
            }
        }
        else if (firstPartyPayload.has_value())
        {
            nextDropPreviewInsertIndex = static_cast<int> (insertIndexForPosition (details.localPosition));
        }
        else if (devicePayload.has_value() && devicePayload->first == trackId_)
        {
            nextDropPreviewInsertIndex = static_cast<int> (insertIndexForPosition (details.localPosition));
        }

        if (dropPreviewEmpty_ == nextDropPreviewEmpty
            && dropPreviewInsertIndex_ == nextDropPreviewInsertIndex
            && dropPreviewReplaceIndex_ == nextDropPreviewReplaceIndex)
            return false;

        const auto previousReplaceIndex = dropPreviewReplaceIndex_;
        dropPreviewEmpty_ = nextDropPreviewEmpty;
        dropPreviewInsertIndex_ = nextDropPreviewInsertIndex;
        dropPreviewReplaceIndex_ = nextDropPreviewReplaceIndex;

        if (previousReplaceIndex >= 0 && previousReplaceIndex < static_cast<int> (deviceCards_.size()))
            deviceCards_[static_cast<std::size_t> (previousReplaceIndex)]->update (
                slots_[static_cast<std::size_t> (previousReplaceIndex)],
                false);

        if (dropPreviewReplaceIndex_ >= 0 && dropPreviewReplaceIndex_ < static_cast<int> (deviceCards_.size()))
            deviceCards_[static_cast<std::size_t> (dropPreviewReplaceIndex_)]->update (
                slots_[static_cast<std::size_t> (dropPreviewReplaceIndex_)],
                true);

        repaint();
        return true;
    }

    bool clearDropPreview()
    {
        if (! dropPreviewEmpty_ && dropPreviewInsertIndex_ < 0 && dropPreviewReplaceIndex_ < 0)
            return false;

        const auto previousReplaceIndex = dropPreviewReplaceIndex_;
        dropPreviewEmpty_ = false;
        dropPreviewInsertIndex_ = -1;
        dropPreviewReplaceIndex_ = -1;

        if (previousReplaceIndex >= 0 && previousReplaceIndex < static_cast<int> (deviceCards_.size()))
            deviceCards_[static_cast<std::size_t> (previousReplaceIndex)]->update (
                slots_[static_cast<std::size_t> (previousReplaceIndex)],
                false);

        repaint();
        return true;
    }

    void setDeviceBypassed (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId, bool bypassed)
    {
        owner_.setDeviceBypassed (trackId, slotId, bypassed);
        owner_.refresh();
    }

    void commitFirstPartyDeviceParameter (const std::string& trackId,
                                          const core::sequencing::DeviceSlotId& slotId,
                                          const std::string& parameterId,
                                          double normalizedValue)
    {
        owner_.setFirstPartyDeviceParameter (trackId, slotId, parameterId, normalizedValue);
    }

    void openDeviceEditor (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
    {
        owner_.openDeviceEditor (trackId, slotId);
    }

    void removeDevice (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
    {
        owner_.removeDevice (trackId, slotId);
        owner_.refresh();
    }

    DeviceChainComponent& owner_;
    std::string trackId_;
    std::string sourceLabel_;
    std::string emptyLabel_;
    bool dropPreviewEmpty_ = false;
    int dropPreviewInsertIndex_ = -1;
    int dropPreviewReplaceIndex_ = -1;
    std::vector<DeviceSlotView> slots_;
    std::vector<std::unique_ptr<DeviceCardComponent>> deviceCards_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainContentComponent)
};

DeviceChainComponent::DeviceChainComponent (app::AppServices& appServices)
    : appServices_ (appServices),
      chainContent_ (std::make_unique<ChainContentComponent> (*this))
{
    setTitle ("Device Chain");
    setDescription ("Selected track device chain with drag-and-drop plugin insertion, bypass, editor, reorder, and remove controls.");

    styleModeLabel (trackLabel_, 13.0f, textColour, true);
    trackLabel_.setTitle ("Selected track device chain");
    addAndMakeVisible (trackLabel_);

    styleModeLabel (flowLabel_, 12.0f, mutedTextColour);
    flowLabel_.setTitle ("Device chain signal flow");
    addAndMakeVisible (flowLabel_);

    chainViewport_.setViewedComponent (chainContent_.get(), false);
    chainViewport_.setTitle ("Device chain scroll area");
    chainViewport_.setDescription ("Horizontally scrollable list of devices for the selected track.");
    chainViewport_.setScrollBarsShown (false, true);
    addAndMakeVisible (chainViewport_);

    refresh();
}

DeviceChainComponent::~DeviceChainComponent() = default;

void DeviceChainComponent::refresh()
{
    core::diagnostics::ScopedPerformanceTimer timer { "DeviceChainComponent::refresh" };

    const auto labelsChanged = updateLabels();
    const auto contentChanged = chainContent_->refreshModel();
    resized();
    if (labelsChanged || contentChanged)
        repaint();
}

void DeviceChainComponent::paint (juce::Graphics& graphics)
{
    core::diagnostics::ScopedPerformanceTimer timer { "DeviceChainComponent::paint" };

    graphics.fillAll (surfaceColour);
}

void DeviceChainComponent::resized()
{
    auto bounds = getLocalBounds().reduced (10, 6);
    auto header = bounds.removeFromTop (22);
    trackLabel_.setBounds (header.removeFromLeft (std::min (260, std::max (130, header.getWidth() / 3))));
    header.removeFromLeft (10);
    flowLabel_.setBounds (header);

    bounds.removeFromTop (4);
    chainViewport_.setBounds (bounds);
    chainContent_->setSize (std::max (bounds.getWidth(), chainContent_->requiredWidth()),
                            std::max (1, bounds.getHeight()));
}

std::optional<core::sequencing::PluginKind> DeviceChainComponent::pluginDropKindForSelectedTrack (const BrowserPluginDragPayload& payload) const
{
    const auto& selectedTrackId = appServices_.selectedTrackId();
    const auto* track = selectedTrackId.has_value() ? appServices_.project().findTrackById (*selectedTrackId) : nullptr;
    if (track == nullptr)
        return std::nullopt;

    if (payload.isInstrument && core::sequencing::trackTypeCanHostInstrument (track->type()))
        return core::sequencing::PluginKind::instrument;

    if (payload.isAudioEffect && core::sequencing::trackTypeCanHostAudioEffects (track->type()))
        return core::sequencing::PluginKind::audioEffect;

    return std::nullopt;
}

std::optional<core::sequencing::PluginKind> DeviceChainComponent::firstPartyDeviceDropKindForSelectedTrack (const BrowserFirstPartyDeviceDragPayload& payload) const
{
    const auto& selectedTrackId = appServices_.selectedTrackId();
    const auto* track = selectedTrackId.has_value() ? appServices_.project().findTrackById (*selectedTrackId) : nullptr;
    if (track == nullptr)
        return std::nullopt;

    if (payload.kind == core::sequencing::PluginKind::instrument && core::sequencing::trackTypeCanHostInstrument (track->type()))
        return core::sequencing::PluginKind::instrument;

    if (payload.kind == core::sequencing::PluginKind::audioEffect && core::sequencing::trackTypeCanHostAudioEffects (track->type()))
        return core::sequencing::PluginKind::audioEffect;

    if (payload.kind == core::sequencing::PluginKind::midiEffect && track->type() == core::sequencing::TrackType::midi)
        return core::sequencing::PluginKind::midiEffect;

    return std::nullopt;
}

bool DeviceChainComponent::insertPluginPayloadIntoSelectedTrack (const BrowserPluginDragPayload& payload, std::size_t insertIndex)
{
    const auto& selectedTrackId = appServices_.selectedTrackId();
    if (! selectedTrackId.has_value())
    {
        appServices_.reportWarning ("Device drop failed: select a track first");
        return false;
    }

    const auto kind = pluginDropKindForSelectedTrack (payload);
    if (! kind.has_value())
    {
        appServices_.reportWarning ("Device drop failed: selected track cannot host that plugin");
        return false;
    }

    return appServices_.insertPluginDeviceToTrackByStableId (*selectedTrackId, payload.stableId, *kind, insertIndex);
}

bool DeviceChainComponent::insertFirstPartyDevicePayloadIntoSelectedTrack (const BrowserFirstPartyDeviceDragPayload& payload, std::size_t insertIndex)
{
    const auto& selectedTrackId = appServices_.selectedTrackId();
    if (! selectedTrackId.has_value())
    {
        appServices_.reportWarning ("Device drop failed: select a track first");
        return false;
    }

    if (! firstPartyDeviceDropKindForSelectedTrack (payload).has_value())
    {
        appServices_.reportWarning ("Device drop failed: selected track cannot host that device");
        return false;
    }

    return appServices_.insertFirstPartyDeviceToTrack (*selectedTrackId, payload.typeId, insertIndex);
}

bool DeviceChainComponent::replaceDeviceWithPluginPayload (const std::string& trackId,
                                                           const core::sequencing::DeviceSlotId& slotId,
                                                           const BrowserPluginDragPayload& payload)
{
    const auto kind = pluginDropKindForSelectedTrack (payload);
    if (! kind.has_value())
    {
        appServices_.reportWarning ("Device replacement failed: selected track cannot host that plugin");
        return false;
    }

    return appServices_.replaceTrackDeviceByStableId (trackId, slotId, payload.stableId, *kind);
}

bool DeviceChainComponent::moveDevice (const std::string& trackId,
                                       const core::sequencing::DeviceSlotId& slotId,
                                       std::size_t targetIndex)
{
    return appServices_.moveTrackDevice (trackId, slotId, targetIndex);
}

bool DeviceChainComponent::removeDevice (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    return appServices_.removeTrackDevice (trackId, slotId);
}

bool DeviceChainComponent::setDeviceBypassed (const std::string& trackId,
                                              const core::sequencing::DeviceSlotId& slotId,
                                              bool bypassed)
{
    return appServices_.setTrackDeviceBypassed (trackId, slotId, bypassed);
}

bool DeviceChainComponent::setFirstPartyDeviceParameter (const std::string& trackId,
                                                         const core::sequencing::DeviceSlotId& slotId,
                                                         const std::string& parameterId,
                                                         double normalizedValue)
{
    return appServices_.setFirstPartyDeviceParameterNormalized (trackId, slotId, parameterId, normalizedValue);
}

bool DeviceChainComponent::openDeviceEditor (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    return appServices_.openTrackPluginEditor (trackId, slotId);
}

bool DeviceChainComponent::updateLabels()
{
    const auto& selectedTrackId = appServices_.selectedTrackId();
    const auto* track = selectedTrackId.has_value() ? appServices_.project().findTrackById (*selectedTrackId) : nullptr;

    juce::String nextTrackText;
    juce::String nextFlowText;
    if (track == nullptr)
    {
        nextTrackText = "Device Chain";
        nextFlowText = "Select a track";
    }
    else
    {
        nextTrackText = toJuceString (track->name() + " / " + trackTypeText (track->type()));
        nextFlowText = toJuceString (flowText (track->type()));
    }

    const auto changed = trackLabel_.getText() != nextTrackText || flowLabel_.getText() != nextFlowText;
    if (changed)
    {
        trackLabel_.setText (nextTrackText, juce::dontSendNotification);
        flowLabel_.setText (nextFlowText, juce::dontSendNotification);
    }

    return changed;
}
}
