#pragma once

#include "core/music_theory/ScaleDefinition.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class ScalePaletteComponent final : public juce::Component
{
public:
    explicit ScalePaletteComponent (app::AppServices& appServices);
    ~ScalePaletteComponent() override;

    void resized() override;
    void paint (juce::Graphics& graphics) override;
    void refresh();

private:
    struct PaletteItem
    {
        bool isHeader = false;
        std::string name;
        std::string group;
        std::string description;
        std::string formula;
    };

    class RowComponent;
    class CustomScaleNotesComponent;

    app::AppServices& appServices_;
    juce::TextEditor searchEditor_;
    juce::TextButton newCustomButton_;
    juce::Viewport viewport_;
    juce::Component listContent_;
    std::vector<PaletteItem> visibleItems_;
    std::vector<std::unique_ptr<RowComponent>> rows_;

    void rebuildRows();
    void showCustomScaleEditor();
    std::vector<core::music_theory::ScaleDefinition> allScales() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScalePaletteComponent)
};
}
