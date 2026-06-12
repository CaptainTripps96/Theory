#include "ui/ScalePaletteComponent.h"

#include "app/AppServices.h"
#include "core/commands/AddCustomScaleCommand.h"
#include "core/music_theory/CustomScaleBuilder.h"
#include "core/music_theory/PitchClass.h"
#include "core/music_theory/ScaleLibrary.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff151a22 };
const auto laneColour = juce::Colour { 0xff1d2630 };
const auto rowColour = juce::Colour { 0xff202a36 };
const auto rowHoverColour = juce::Colour { 0xff263344 };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff9aa7b7 };
const auto accentColour = juce::Colour { 0xffa58fe8 };

std::string lower (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) {
        return static_cast<char> (std::tolower (c));
    });
    return text;
}

bool containsText (const std::string& text, const std::string& query)
{
    if (query.empty())
        return true;

    return lower (text).find (query) != std::string::npos;
}

std::string formulaFor (const core::music_theory::ScaleDefinition& scale)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < scale.pitchClassOffsetsFromRoot().size(); ++index)
    {
        if (index > 0)
            stream << " ";

        stream << scale.pitchClassOffsetsFromRoot()[index];
    }

    return stream.str();
}

std::string groupFor (const core::music_theory::ScaleDefinition& scale)
{
    const auto name = scale.name();
    const auto category = scale.category();

    if (name == "Major" || name == "Ionian")
        return "Major Scales";
    if (name == "Dorian" || name == "Phrygian" || name == "Lydian" || name == "Mixolydian" || name == "Locrian")
        return "Church Modes";
    if (name.find ("Minor") != std::string::npos)
        return "Minor Scales";
    if (category == "Pentatonic")
        return "Pentatonic";
    if (category == "Blues")
        return "Blues";
    if (category == "Custom")
        return "Custom";
    if (category == "Symmetric")
        return "Synthetic";
    if (name == "Chromatic" || category == "Utility")
        return "Synthetic";

    return "Jazz";
}

std::vector<core::music_theory::PitchClass> selectedPitchClasses (const std::array<juce::ToggleButton*, 12>& buttons)
{
    std::vector<core::music_theory::PitchClass> selected;
    for (auto index = 0; index < static_cast<int> (buttons.size()); ++index)
    {
        if (buttons[static_cast<std::size_t> (index)] != nullptr && buttons[static_cast<std::size_t> (index)]->getToggleState())
            selected.push_back (core::music_theory::PitchClass::fromSemitonesFromC (index));
    }

    return selected;
}

}

class ScalePaletteComponent::CustomScaleNotesComponent final : public juce::Component
{
public:
    CustomScaleNotesComponent()
    {
        const std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (auto index = 0; index < static_cast<int> (buttons_.size()); ++index)
        {
            auto& button = buttons_[static_cast<std::size_t> (index)];
            button = std::make_unique<juce::ToggleButton> (names[static_cast<std::size_t> (index)]);
            button->setToggleState (index == 0, juce::dontSendNotification);
            button->setEnabled (index != 0);
            addAndMakeVisible (*button);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        const auto columnWidth = std::max (1, bounds.getWidth() / 4);
        const auto rowHeight = 28;
        for (auto index = 0; index < static_cast<int> (buttons_.size()); ++index)
        {
            buttons_[static_cast<std::size_t> (index)]->setBounds (
                (index % 4) * columnWidth,
                (index / 4) * rowHeight,
                columnWidth,
                rowHeight);
        }
    }

    std::vector<core::music_theory::PitchClass> selectedPitchClassesFromC() const
    {
        std::array<juce::ToggleButton*, 12> raw {};
        for (auto index = 0; index < static_cast<int> (buttons_.size()); ++index)
            raw[static_cast<std::size_t> (index)] = buttons_[static_cast<std::size_t> (index)].get();

        return selectedPitchClasses (raw);
    }

private:
    std::array<std::unique_ptr<juce::ToggleButton>, 12> buttons_;
};

class ScalePaletteComponent::RowComponent final : public juce::Component
{
public:
    RowComponent (PaletteItem item)
        : item_ (std::move (item))
    {
    }

    void paint (juce::Graphics& graphics) override
    {
        if (item_.isHeader)
        {
            graphics.fillAll (laneColour);
            graphics.setColour (mutedTextColour);
            graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
            graphics.drawText (item_.group, getLocalBounds().reduced (8, 0), juce::Justification::centredLeft);
            return;
        }

        graphics.fillAll (isMouseOver() ? rowHoverColour : rowColour);
        graphics.setColour (outlineColour);
        graphics.drawRect (getLocalBounds());
        auto bounds = getLocalBounds().reduced (8, 5);

        graphics.setColour (textColour);
        graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        graphics.drawText (item_.name, bounds.removeFromTop (18), juce::Justification::centredLeft);

        graphics.setColour (mutedTextColour);
        graphics.setFont (juce::FontOptions { 11.0f });
        graphics.drawText (item_.description.empty() ? "Formula: " + item_.formula : item_.description,
                           bounds.removeFromTop (28),
                           juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragStarted_ = false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (item_.isHeader || dragStarted_ || event.getDistanceFromDragStart() < 5)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragStarted_ = true;
            container->startDragging (juce::var { juce::String ("tsq-scale:") + item_.name }, this);
        }
    }

private:
    PaletteItem item_;
    bool dragStarted_ = false;
};

ScalePaletteComponent::ScalePaletteComponent (app::AppServices& appServices)
    : appServices_ (appServices),
      newCustomButton_ ("New")
{
    searchEditor_.setTextToShowWhenEmpty ("Search scales", mutedTextColour);
    searchEditor_.onTextChange = [this] { refresh(); };
    addAndMakeVisible (searchEditor_);

    newCustomButton_.onClick = [this] { showCustomScaleEditor(); };
    newCustomButton_.setTooltip ("Create custom scale");
    addAndMakeVisible (newCustomButton_);

    viewport_.setViewedComponent (&listContent_, false);
    addAndMakeVisible (viewport_);
    refresh();
}

ScalePaletteComponent::~ScalePaletteComponent() = default;

void ScalePaletteComponent::resized()
{
    auto bounds = getLocalBounds().reduced (10);
    auto top = bounds.removeFromTop (30);
    newCustomButton_.setBounds (top.removeFromRight (58));
    top.removeFromRight (8);
    searchEditor_.setBounds (top);
    bounds.removeFromTop (10);
    viewport_.setBounds (bounds);
    rebuildRows();
}

void ScalePaletteComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (surfaceColour);
    graphics.setColour (outlineColour);
    graphics.drawRect (getLocalBounds());
}

void ScalePaletteComponent::refresh()
{
    visibleItems_.clear();

    const auto query = lower (searchEditor_.getText().toStdString());
    const std::vector<std::string> groupOrder {
        "Church Modes",
        "Major Scales",
        "Minor Scales",
        "Pentatonic",
        "Blues",
        "Jazz",
        "Synthetic",
        "Custom"
    };

    std::map<std::string, std::vector<PaletteItem>> grouped;
    for (const auto& scale : allScales())
    {
        const auto haystack = scale.name() + " " + scale.category() + " " + scale.description();
        const auto formula = formulaFor (scale);
        if (! containsText (haystack + " " + formula, query))
            continue;

        const auto group = groupFor (scale);
        grouped[group].push_back (PaletteItem { false, scale.name(), group, scale.description(), formula });
    }

    for (const auto& group : groupOrder)
    {
        auto match = grouped.find (group);
        if (match == grouped.end() || match->second.empty())
            continue;

        visibleItems_.push_back (PaletteItem { true, {}, group, {}, {} });
        std::sort (match->second.begin(), match->second.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.name < rhs.name;
        });

        for (const auto& item : match->second)
            visibleItems_.push_back (item);
    }

    rebuildRows();
}

void ScalePaletteComponent::rebuildRows()
{
    listContent_.removeAllChildren();
    rows_.clear();

    auto y = 0;
    const auto width = std::max (1, viewport_.getWidth() - 2);
    for (const auto& item : visibleItems_)
    {
        const auto height = item.isHeader ? 24 : 58;
        auto row = std::make_unique<RowComponent> (item);
        row->setBounds (0, y, width, height);
        listContent_.addAndMakeVisible (*row);
        rows_.push_back (std::move (row));
        y += height + 4;
    }

    listContent_.setSize (width, std::max (viewport_.getHeight(), y));
    listContent_.repaint();
}

void ScalePaletteComponent::showCustomScaleEditor()
{
    auto notes = std::make_unique<CustomScaleNotesComponent>();
    notes->setSize (380, 92);

    auto* window = new juce::AlertWindow {
        "Custom Scale",
        "Enter the notes for your scale when you play it in the Key of C.",
        juce::AlertWindow::NoIcon
    };
    window->addTextEditor ("name", "Custom Scale", "Name");
    window->addTextEditor ("category", "Custom", "Category");
    window->addTextEditor ("tags", "custom,user", "Tags");
    window->addTextEditor ("description", "", "Description");
    window->addCustomComponent (notes.get());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->setBounds ({ 0, 0, 440, 420 });
    window->centreAroundComponent (this, window->getWidth(), window->getHeight());
    window->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowHasDropShadow);
    window->setVisible (true);
    window->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [this, window, notes = std::move (notes)] (int result) mutable
            {
                std::unique_ptr<juce::AlertWindow> ownedWindow { window };
                if (result != 1)
                    return;

                const auto name = window->getTextEditorContents ("name").trim().toStdString();
                const auto category = window->getTextEditorContents ("category").trim().isEmpty()
                    ? std::string { "Custom" }
                    : window->getTextEditorContents ("category").trim().toStdString();

                std::vector<std::string> tags;
                for (auto token : juce::StringArray::fromTokens (window->getTextEditorContents ("tags"), ",", ""))
                {
                    token = token.trim();
                    if (token.isNotEmpty())
                        tags.push_back (token.toStdString());
                }

                try
                {
                    auto scale = core::music_theory::CustomScaleBuilder::build (
                        core::music_theory::ScaleMetadata {
                            name,
                            category,
                            std::move (tags),
                            window->getTextEditorContents ("description").trim().toStdString()
                        },
                        notes->selectedPitchClassesFromC());

                    const auto commandResult = appServices_.commandStack().execute (
                        std::make_unique<core::commands::AddCustomScaleCommand> (std::move (scale)));

                    if (commandResult.failed())
                    {
                        appServices_.reportWarning ("Custom scale add failed: " + commandResult.error());
                        juce::NativeMessageBox::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Custom Scale", commandResult.error());
                        return;
                    }

                    refresh();
                }
                catch (const std::exception& exception)
                {
                    appServices_.reportWarning ("Custom scale creation failed: " + std::string { exception.what() });
                    juce::NativeMessageBox::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Custom Scale", exception.what());
                }
            }),
        false);
}

std::vector<core::music_theory::ScaleDefinition> ScalePaletteComponent::allScales() const
{
    auto library = core::music_theory::ScaleLibrary::createBuiltInLibrary();
    for (const auto& scale : appServices_.project().customScales())
    {
        try
        {
            library.addDefinition (scale);
        }
        catch (const std::exception&)
        {
        }
    }

    return library.definitions();
}
}
