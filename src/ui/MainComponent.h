#pragma once

#include "core/time/Tick.h"
#include "ui/AudioSettingsComponent.h"
#include "ui/BrowserPanelComponent.h"
#include "ui/DetailEditorComponent.h"
#include "ui/DiagnosticsComponent.h"
#include "ui/InspectorComponent.h"
#include "ui/PluginBrowserComponent.h"
#include "ui/StatusBarComponent.h"
#include "ui/TimelineComponent.h"
#include "ui/TrackListComponent.h"
#include "ui/TransportComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::Timer
{
public:
    explicit MainComponent (app::AppServices& appServices);

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void showAudioSettings();
    void showPluginBrowser();
    void showDiagnostics();
    void showProjectMenu();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportOpenClipAsMidi();
    void refreshProjectView();
    bool handleGlobalShortcut (const juce::KeyPress& key);
    bool undoLastAction();
    bool redoLastAction();
    bool selectAllInFocusedField();
    bool copySelection();
    bool pasteSelection();
    bool togglePlayback();
    bool handleEscapeKey();
    void updateWindowTitle();
    void hideOverlayPanels();
    void applyPlayheadTick (core::time::TickPosition playheadTick);
    juce::Rectangle<int> overlayBounds() const;

    app::AppServices& appServices_;
    core::time::TickPosition playheadTick_ {};
    int mainTimerTick_ = 0;
    int detailEditorHeight_ = 156;
    bool resizingDetailEditor_ = false;
    int resizeStartY_ = 0;
    int resizeStartDetailHeight_ = 156;
    juce::Rectangle<int> detailResizeHandleBounds_;
    TransportComponent transportComponent_;
    TrackListComponent trackListComponent_;
    TimelineComponent timelineComponent_;
    BrowserPanelComponent browserPanelComponent_;
    DetailEditorComponent detailEditorComponent_;
    StatusBarComponent statusBarComponent_;
    AudioSettingsComponent audioSettingsComponent_;
    PluginBrowserComponent pluginBrowserComponent_;
    DiagnosticsComponent diagnosticsComponent_;
    std::unique_ptr<juce::FileChooser> projectFileChooser_;
    juce::TooltipWindow tooltipWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
