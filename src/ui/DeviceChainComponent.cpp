#include "ui/DeviceChainComponent.h"

#include "app/AppServices.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserDragPayload.h"

#include <algorithm>
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
constexpr int deviceCardHeight = 78;
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
public:
    explicit ChainContentComponent (DeviceChainComponent& owner)
        : owner_ (owner)
    {
    }

    void refreshModel()
    {
        const auto* track = selectedTrack();
        trackId_.clear();
        sourceLabel_.clear();
        emptyLabel_.clear();
        slots_.clear();
        dropPreviewEmpty_ = false;
        dropPreviewInsertIndex_ = -1;
        dropPreviewReplaceIndex_ = -1;

        if (track != nullptr)
        {
            trackId_ = track->id();
            sourceLabel_ = sourceText (track->type());
            emptyLabel_ = emptyText (track->type());

            for (const auto& slot : track->deviceChain().slots())
                slots_.push_back (slotViewFor (*track, slot, true, slots_.size()));

            if (slots_.empty() && track->instrument().has_value())
            {
                core::sequencing::DeviceSlot legacySlot {
                    core::sequencing::DeviceSlotId { "instrument" },
                    core::sequencing::PluginReference::fromTrackInstrumentReference (*track->instrument()),
                    core::sequencing::PluginKind::instrument
                };
                legacySlot.setPluginStateFile (track->instrument()->pluginStateFile);
                slots_.push_back (slotViewFor (*track, legacySlot, false, slots_.size()));
            }
        }

        rebuildCardsIfNeeded();
        for (std::size_t index = 0; index < slots_.size(); ++index)
            deviceCards_[index]->update (slots_[index], dropPreviewReplaceIndex_ == static_cast<int> (index));

        resized();
        repaint();
    }

    int requiredWidth() const
    {
        if (trackId_.empty())
            return 320;

        const auto deviceCount = std::max (1, static_cast<int> (slots_.size()));
        return (chainPadding * 2)
            + sourceWidth
            + arrowWidth
            + (deviceCount * deviceCardWidth)
            + ((deviceCount - 1) * arrowWidth);
    }

    void paint (juce::Graphics& graphics) override
    {
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

        for (auto& card : deviceCards_)
        {
            card->setBounds (x, y, deviceCardWidth, height);
            x += deviceCardWidth + arrowWidth;
        }
    }

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const auto payload = pluginDragPayloadFromVar (details.description);
        if (payload.has_value())
            return owner_.pluginDropKindForSelectedTrack (*payload).has_value();

        return deviceDragPayloadFromVar (details.description).has_value();
    }

    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateDropPreview (details);
        repaint();
    }

    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateDropPreview (details);
    }

    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        clearDropPreview();
        repaint();
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
    };

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

        void update (DeviceSlotView view, bool replacePreview)
        {
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
            removeButton_.setTitle (toJuceString ("Remove " + view_.name));
            removeButton_.setEnabled (view_.commandBacked);
            repaint();
        }

        void paint (juce::Graphics& graphics) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            graphics.setColour (view_.bypassed ? mutedCardColour : cardColour);
            graphics.fillRoundedRectangle (bounds, 6.0f);
            graphics.setColour (replacePreview_ ? insertPreviewColour : (view_.bypassed ? warningColour.withAlpha (0.7f) : outlineColour));
            graphics.drawRoundedRectangle (bounds, 6.0f, replacePreview_ ? 2.0f : 1.0f);

            auto textBounds = getLocalBounds().reduced (10, 8);
            textBounds.removeFromRight (116);

            graphics.setColour (view_.bypassed ? mutedTextColour : textColour);
            graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
            graphics.drawFittedText (toJuceString (view_.name), textBounds.removeFromTop (20), juce::Justification::centredLeft, 1);

            graphics.setColour (accentColour);
            graphics.setFont (juce::FontOptions { 11.5f, juce::Font::bold });
            graphics.drawFittedText (toJuceString (view_.kind), textBounds.removeFromTop (18), juce::Justification::centredLeft, 1);

            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 11.0f });
            graphics.drawFittedText (toJuceString (view_.detail), textBounds, juce::Justification::centredLeft, 2);
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
        ChainContentComponent& owner_;
        DeviceSlotView view_;
        bool replacePreview_ = false;
        bool dragStarted_ = false;
        juce::TextButton enableButton_;
        juce::TextButton openEditorButton_;
        juce::TextButton removeButton_;

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
        return DeviceSlotView {
            track.id(),
            slot.id(),
            index,
            displayNameForPlugin (slot.plugin()),
            kindText (slot.kind()),
            detailText (slot, ! commandBacked),
            slot.bypassed(),
            commandBacked,
            slot.plugin().isValid()
        };
    }

    void rebuildCardsIfNeeded()
    {
        if (deviceCards_.size() == slots_.size())
            return;

        deviceCards_.clear();
        for (std::size_t index = 0; index < slots_.size(); ++index)
        {
            auto card = std::make_unique<DeviceCardComponent> (*this);
            addAndMakeVisible (*card);
            deviceCards_.push_back (std::move (card));
        }
    }

    juce::Rectangle<int> chainArea() const
    {
        return getLocalBounds().reduced (chainPadding, 6);
    }

    int cardHeight() const
    {
        return std::clamp (chainArea().getHeight() - 2, 58, deviceCardHeight);
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
            const auto bounds = deviceCards_[index]->getBounds().reduced (deviceCardWidth / 5, 0);
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

    void updateDropPreview (const juce::DragAndDropTarget::SourceDetails& details)
    {
        const auto pluginPayload = pluginDragPayloadFromVar (details.description);
        const auto devicePayload = deviceDragPayloadFromVar (details.description);

        if (! pluginPayload.has_value() && ! devicePayload.has_value())
        {
            clearDropPreview();
            return;
        }

        dropPreviewEmpty_ = deviceCards_.empty();
        dropPreviewInsertIndex_ = -1;
        dropPreviewReplaceIndex_ = -1;

        if (pluginPayload.has_value())
        {
            if (const auto replaceIndex = replacementIndexForPosition (details.localPosition);
                replaceIndex.has_value() && *replaceIndex < slots_.size() && slots_[*replaceIndex].commandBacked)
            {
                dropPreviewReplaceIndex_ = static_cast<int> (*replaceIndex);
            }
            else
            {
                dropPreviewInsertIndex_ = static_cast<int> (insertIndexForPosition (details.localPosition));
            }
        }
        else if (devicePayload.has_value() && devicePayload->first == trackId_)
        {
            dropPreviewInsertIndex_ = static_cast<int> (insertIndexForPosition (details.localPosition));
        }

        for (std::size_t index = 0; index < deviceCards_.size(); ++index)
            deviceCards_[index]->update (slots_[index], dropPreviewReplaceIndex_ == static_cast<int> (index));

        repaint();
    }

    void clearDropPreview()
    {
        dropPreviewEmpty_ = false;
        dropPreviewInsertIndex_ = -1;
        dropPreviewReplaceIndex_ = -1;

        for (std::size_t index = 0; index < deviceCards_.size(); ++index)
            deviceCards_[index]->update (slots_[index], false);
    }

    void setDeviceBypassed (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId, bool bypassed)
    {
        owner_.setDeviceBypassed (trackId, slotId, bypassed);
        owner_.refresh();
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
    updateLabels();
    chainContent_->refreshModel();
    resized();
    repaint();
}

void DeviceChainComponent::paint (juce::Graphics& graphics)
{
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

bool DeviceChainComponent::openDeviceEditor (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    return appServices_.openTrackPluginEditor (trackId, slotId);
}

void DeviceChainComponent::updateLabels()
{
    const auto& selectedTrackId = appServices_.selectedTrackId();
    const auto* track = selectedTrackId.has_value() ? appServices_.project().findTrackById (*selectedTrackId) : nullptr;

    if (track == nullptr)
    {
        trackLabel_.setText ("Device Chain", juce::dontSendNotification);
        flowLabel_.setText ("Select a track", juce::dontSendNotification);
        return;
    }

    trackLabel_.setText (toJuceString (track->name() + " / " + trackTypeText (track->type())), juce::dontSendNotification);
    flowLabel_.setText (toJuceString (flowText (track->type())), juce::dontSendNotification);
}
}
