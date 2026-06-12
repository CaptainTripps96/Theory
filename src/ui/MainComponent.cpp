#include "ui/MainComponent.h"

#include "app/AppServices.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/midi/MidiExporter.h"
#include "engine/PlaybackEngine.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace tsq::ui
{
namespace
{
const auto backgroundColour = juce::Colour { 0xff0f1318 };
constexpr int minimumDetailEditorHeight = 118;
constexpr int minimumUpperWorkspaceHeight = 96;
constexpr int detailResizeHandleHeight = 8;
constexpr int idleRefreshTickInterval = 12;

constexpr auto newProjectMenuId = 1;
constexpr auto openProjectMenuId = 2;
constexpr auto saveProjectMenuId = 3;
constexpr auto saveProjectAsMenuId = 4;
constexpr auto exportMidiMenuId = 5;

std::filesystem::path projectPackagePathFromFile (juce::File file)
{
    if (file == juce::File {})
        return {};

    if (file.getFileExtension().compareIgnoreCase (".tseq") != 0)
        file = file.withFileExtension (".tseq");

    return std::filesystem::path { file.getFullPathName().toStdString() };
}

std::filesystem::path midiExportPathFromFile (juce::File file)
{
    if (file == juce::File {})
        return {};

    const auto extension = file.getFileExtension();
    if (extension.compareIgnoreCase (".mid") != 0 && extension.compareIgnoreCase (".midi") != 0)
        file = file.withFileExtension (".mid");

    return std::filesystem::path { file.getFullPathName().toStdString() };
}

juce::File defaultMidiExportFile (const app::AppServices& appServices, const core::sequencing::MidiClip& clip)
{
    const auto clipName = clip.name().empty() ? clip.id() : clip.name();
    const auto fileName = juce::File::createLegalFileName (juce::String::fromUTF8 (clipName.c_str())) + ".mid";

    if (appServices.currentProjectPackagePath().has_value())
        return juce::File { appServices.currentProjectPackagePath()->string() }.getChildFile ("exports").getChildFile (fileName);

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (fileName);
}

juce::String toJuceString (std::string_view text)
{
    return juce::String::fromUTF8 (std::string { text }.c_str());
}

juce::String userMessageOr (const app::AppServices& appServices, const char* fallback)
{
    return appServices.lastUserMessage().empty() ? juce::String { fallback } : toJuceString (appServices.lastUserMessage());
}

void showProjectOperationFailed (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                            "Project",
                                            message);
}

void showMidiExportMessage (juce::MessageBoxIconType icon, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (icon,
                                            "MIDI Export",
                                            message);
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
}

MainComponent::MainComponent (app::AppServices& appServices)
    : appServices_ (appServices),
      transportComponent_ (appServices),
      trackListComponent_ (appServices),
      timelineComponent_ (appServices),
      browserPanelComponent_ (appServices),
      detailEditorComponent_ (appServices),
      statusBarComponent_ (appServices),
      audioSettingsComponent_ (appServices),
      pluginBrowserComponent_ (appServices),
      diagnosticsComponent_ (appServices),
      tooltipWindow_ (this, 650)
{
    core::diagnostics::ScopedPerformanceTimer timer { "MainComponent constructor" };

    setSize (1280, 800);
    setWantsKeyboardFocus (true);
    setTitle ("TheorySequencer Workspace");
    setDescription ("Main project workspace with transport, mixer track headers, timeline, browser, and lower editor.");

    transportComponent_.onAudioSettingsRequested = [this] { showAudioSettings(); };
    transportComponent_.onPluginBrowserRequested = [this] { showPluginBrowser(); };
    transportComponent_.onProjectMenuRequested = [this] { showProjectMenu(); };
    statusBarComponent_.onDiagnosticsRequested = [this] { showDiagnostics(); };
    timelineComponent_.onPlayheadMoved = [this] (core::time::TickPosition tick)
    {
        appServices_.setPlaybackPlayheadPosition (tick);
        applyPlayheadTick (tick);
    };
    timelineComponent_.onClipOpened = [this] (std::string trackId, std::string clipId)
    {
        appServices_.setSelectedRecordingClip (trackId, clipId);
        detailEditorComponent_.openClip (std::move (trackId), std::move (clipId));
    };
    timelineComponent_.selectedNoteIdsForClip = [this] (const std::string& trackId, const std::string& clipId)
    {
        return detailEditorComponent_.selectedNoteIdsForClip (trackId, clipId);
    };
    addAndMakeVisible (transportComponent_);
    addAndMakeVisible (trackListComponent_);
    addAndMakeVisible (timelineComponent_);
    addAndMakeVisible (browserPanelComponent_);
    addAndMakeVisible (detailEditorComponent_);
    addAndMakeVisible (statusBarComponent_);

    audioSettingsComponent_.setVisible (false);
    audioSettingsComponent_.onClose = [this] { hideOverlayPanels(); };
    addChildComponent (audioSettingsComponent_);

    pluginBrowserComponent_.setVisible (false);
    pluginBrowserComponent_.onClose = [this] { hideOverlayPanels(); };
    addChildComponent (pluginBrowserComponent_);

    diagnosticsComponent_.setVisible (false);
    diagnosticsComponent_.onClose = [this] { hideOverlayPanels(); };
    addChildComponent (diagnosticsComponent_);

    applyPlayheadTick (playheadTick_);
    updateWindowTitle();
    startTimerHz (24);
}

void MainComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (backgroundColour);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    statusBarComponent_.setBounds (bounds.removeFromBottom (28));
    transportComponent_.setBounds (bounds.removeFromTop (54));

    auto body = bounds.reduced (14);
    const auto maximumDetailEditorHeight = std::max (minimumDetailEditorHeight, body.getHeight() - minimumUpperWorkspaceHeight);
    detailEditorHeight_ = std::clamp (detailEditorHeight_, minimumDetailEditorHeight, maximumDetailEditorHeight);

    auto lowerEditor = body.removeFromBottom (detailEditorHeight_);
    detailResizeHandleBounds_ = juce::Rectangle<int> {
        lowerEditor.getX(),
        lowerEditor.getY() - detailResizeHandleHeight - 2,
        lowerEditor.getWidth(),
        detailResizeHandleHeight + 4
    };
    body.removeFromBottom (12);

    auto inspector = body.removeFromRight (300);
    body.removeFromRight (12);
    auto trackList = body.removeFromLeft (520);
    body.removeFromLeft (12);

    trackListComponent_.setBounds (trackList);
    timelineComponent_.setBounds (body);
    browserPanelComponent_.setBounds (inspector);
    detailEditorComponent_.setBounds (lowerEditor);

    if (audioSettingsComponent_.isVisible())
        audioSettingsComponent_.setBounds (overlayBounds());

    if (pluginBrowserComponent_.isVisible())
        pluginBrowserComponent_.setBounds (overlayBounds().withHeight (std::min (overlayBounds().getHeight(), 390)));

    if (diagnosticsComponent_.isVisible())
        diagnosticsComponent_.setBounds (overlayBounds());
}

void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    if (detailResizeHandleBounds_.contains (event.position.roundToInt()))
    {
        resizingDetailEditor_ = true;
        resizeStartY_ = event.y;
        resizeStartDetailHeight_ = detailEditorHeight_;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
}

void MainComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! resizingDetailEditor_)
        return;

    const auto bodyHeight = getHeight() - 54 - 28 - 28;
    const auto maximumDetailEditorHeight = std::max (minimumDetailEditorHeight, bodyHeight - minimumUpperWorkspaceHeight);
    detailEditorHeight_ = std::clamp (resizeStartDetailHeight_ + (resizeStartY_ - event.y),
                                      minimumDetailEditorHeight,
                                      maximumDetailEditorHeight);
    resized();
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    resizingDetailEditor_ = false;
}

void MainComponent::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (detailResizeHandleBounds_.contains (event.position.roundToInt()) ? juce::MouseCursor::UpDownResizeCursor
                                                                                     : juce::MouseCursor::NormalCursor);
}

void MainComponent::timerCallback()
{
    core::diagnostics::ScopedPerformanceTimer timer { "MainComponent::timerCallback" };

    ++mainTimerTick_;
    const auto playbackActive = appServices_.playbackEngine().isPlaying();
    const auto recordingActive = appServices_.midiRecordingEnabled();
    const auto slowIdleRefresh = (mainTimerTick_ % idleRefreshTickInterval) == 0;

    appServices_.observeLivePluginParameterState();
    appServices_.processMidiRecordingEvents();

    if (playbackActive || recordingActive)
    {
        const auto enginePlayhead = appServices_.playbackPlayheadPosition();
        if (enginePlayhead != playheadTick_)
            applyPlayheadTick (enginePlayhead);
    }

    statusBarComponent_.refresh();
    if (diagnosticsComponent_.isVisible())
        diagnosticsComponent_.refresh();

    if (playbackActive)
        trackListComponent_.refresh();
    else if (slowIdleRefresh)
    {
        timelineComponent_.repaint();
        trackListComponent_.refresh();
        detailEditorComponent_.refresh();
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    return handleGlobalShortcut (key);
}

void MainComponent::showAudioSettings()
{
    pluginBrowserComponent_.setVisible (false);
    diagnosticsComponent_.setVisible (false);
    audioSettingsComponent_.setVisible (! audioSettingsComponent_.isVisible());

    if (audioSettingsComponent_.isVisible())
        audioSettingsComponent_.refresh();

    resized();
}

void MainComponent::showPluginBrowser()
{
    audioSettingsComponent_.setVisible (false);
    diagnosticsComponent_.setVisible (false);
    pluginBrowserComponent_.setVisible (! pluginBrowserComponent_.isVisible());

    if (pluginBrowserComponent_.isVisible())
        pluginBrowserComponent_.refresh();

    resized();
}

void MainComponent::showDiagnostics()
{
    audioSettingsComponent_.setVisible (false);
    pluginBrowserComponent_.setVisible (false);
    diagnosticsComponent_.setVisible (! diagnosticsComponent_.isVisible());

    if (diagnosticsComponent_.isVisible())
        diagnosticsComponent_.refresh();

    resized();
}

void MainComponent::showProjectMenu()
{
    juce::PopupMenu menu;
    menu.addItem (newProjectMenuId, "New Project");
    menu.addItem (openProjectMenuId, "Open Project...");
    menu.addSeparator();
    menu.addItem (saveProjectMenuId, "Save Project");
    menu.addItem (saveProjectAsMenuId, "Save Project As...");
    menu.addSeparator();
    menu.addItem (exportMidiMenuId, "Export Open Clip as MIDI...");

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (&transportComponent_),
                        [this] (int result)
                        {
                            switch (result)
                            {
                                case newProjectMenuId: newProject(); break;
                                case openProjectMenuId: openProject(); break;
                                case saveProjectMenuId: saveProject(); break;
                                case saveProjectAsMenuId: saveProjectAs(); break;
                                case exportMidiMenuId: exportOpenClipAsMidi(); break;
                                default: break;
                            }
                        });
}

void MainComponent::newProject()
{
    if (! appServices_.newProject())
    {
        showProjectOperationFailed (userMessageOr (appServices_, "New project failed."));
        return;
    }

    refreshProjectView();
    updateWindowTitle();
}

void MainComponent::openProject()
{
    projectFileChooser_ = std::make_unique<juce::FileChooser> ("Open TheorySequencer Project",
                                                               juce::File {},
                                                               "*.tseq");
    projectFileChooser_->launchAsync (juce::FileBrowserComponent::openMode
                                          | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::canSelectDirectories,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          const auto path = projectPackagePathFromFile (chooser.getResult());
                                          if (path.empty())
                                              return;

                                          if (! appServices_.loadProject (path))
                                          {
                                              showProjectOperationFailed (userMessageOr (appServices_, "Open project failed."));
                                              return;
                                          }

                                          refreshProjectView();
                                          updateWindowTitle();
                                      });
}

void MainComponent::exportOpenClipAsMidi()
{
    const auto openClipIds = detailEditorComponent_.openClipIds();
    if (! openClipIds.has_value())
    {
        appServices_.reportWarning ("MIDI export failed: open a clip in the piano roll first");
        showMidiExportMessage (juce::AlertWindow::WarningIcon, toJuceString (appServices_.lastUserMessage()));
        return;
    }

    const auto* track = appServices_.project().findTrackById (openClipIds->first);
    const auto* clip = track == nullptr ? nullptr : track->findClipById (openClipIds->second);
    if (clip == nullptr)
    {
        appServices_.reportWarning ("MIDI export failed: the open clip no longer exists");
        showMidiExportMessage (juce::AlertWindow::WarningIcon, toJuceString (appServices_.lastUserMessage()));
        return;
    }

    projectFileChooser_ = std::make_unique<juce::FileChooser> ("Export MIDI Clip",
                                                               defaultMidiExportFile (appServices_, *clip),
                                                               "*.mid;*.midi");
    projectFileChooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this, trackId = openClipIds->first, clipId = openClipIds->second] (const juce::FileChooser& chooser)
                                      {
                                          const auto filePath = midiExportPathFromFile (chooser.getResult());
                                          if (filePath.empty())
                                              return;

                                          const auto* selectedTrack = appServices_.project().findTrackById (trackId);
                                          const auto* selectedClip = selectedTrack == nullptr ? nullptr : selectedTrack->findClipById (clipId);
                                          if (selectedClip == nullptr)
                                          {
                                              appServices_.reportWarning ("MIDI export failed: the selected clip no longer exists");
                                              showMidiExportMessage (juce::AlertWindow::WarningIcon, toJuceString (appServices_.lastUserMessage()));
                                              return;
                                          }

                                          try
                                          {
                                              if (filePath.has_parent_path())
                                                  std::filesystem::create_directories (filePath.parent_path());
                                          }
                                          catch (const std::exception& error)
                                          {
                                              appServices_.reportWarning ("MIDI export failed: " + std::string { error.what() });
                                              showMidiExportMessage (juce::AlertWindow::WarningIcon, toJuceString (appServices_.lastUserMessage()));
                                              return;
                                          }

                                          core::midi::MidiExportOptions options;
                                          options.tempo = appServices_.project().tempoMap().tempoAt (selectedClip->startInProject());
                                          options.timeSignature = appServices_.project().timeSignatureMap().timeSignatureAt (selectedClip->startInProject());

                                          const auto result = core::midi::MidiExporter::tryExportClipToFile (*selectedClip, filePath, options);
                                          if (result.failed())
                                          {
                                              appServices_.reportWarning (result.error());
                                              showMidiExportMessage (juce::AlertWindow::WarningIcon, toJuceString (appServices_.lastUserMessage()));
                                              return;
                                          }

                                          appServices_.logger().info ("MIDI exported: " + filePath.string());
                                          appServices_.clearUserMessage();
                                          statusBarComponent_.refresh();
                                          showMidiExportMessage (juce::AlertWindow::InfoIcon,
                                                                 juce::String { "Exported MIDI to:\n" } + toJuceString (filePath.string()));
                                      });
}

void MainComponent::saveProject()
{
    if (! appServices_.currentProjectPackagePath().has_value())
    {
        saveProjectAs();
        return;
    }

    if (! appServices_.saveProject())
    {
        showProjectOperationFailed (userMessageOr (appServices_, "Save project failed."));
        return;
    }

    updateWindowTitle();
}

void MainComponent::saveProjectAs()
{
    const auto initialFile = appServices_.currentProjectPackagePath().has_value()
        ? juce::File { appServices_.currentProjectPackagePath()->string() }
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Untitled.tseq");

    projectFileChooser_ = std::make_unique<juce::FileChooser> ("Save TheorySequencer Project",
                                                               initialFile,
                                                               "*.tseq");
    projectFileChooser_->launchAsync (juce::FileBrowserComponent::saveMode
                                          | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::canSelectDirectories,
                                      [this] (const juce::FileChooser& chooser)
                                      {
                                          const auto path = projectPackagePathFromFile (chooser.getResult());
                                          if (path.empty())
                                              return;

                                          if (! appServices_.saveProjectAs (path))
                                          {
                                              showProjectOperationFailed (userMessageOr (appServices_, "Save project failed."));
                                              return;
                                          }

                                          refreshProjectView();
                                          updateWindowTitle();
                                      });
}

void MainComponent::refreshProjectView()
{
    hideOverlayPanels();
    appServices_.clearSelectedRecordingClip();
    detailEditorComponent_.clearClip();
    applyPlayheadTick ({});
    browserPanelComponent_.refresh();
    repaint();
}

bool MainComponent::handleGlobalShortcut (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto commandDown = modifiers.isCommandDown() || modifiers.isCtrlDown();

    if (! commandDown && key.getKeyCode() == juce::KeyPress::escapeKey)
        return handleEscapeKey();

    if (commandDown && keyMatchesCharacter (key, 'z'))
        return modifiers.isShiftDown() ? redoLastAction() : undoLastAction();

    if (commandDown && keyMatchesCharacter (key, 'y'))
        return redoLastAction();

    if (commandDown && keyMatchesCharacter (key, 'a'))
        return selectAllInFocusedField();

    if (commandDown && keyMatchesCharacter (key, 'c'))
        return copySelection();

    if (commandDown && keyMatchesCharacter (key, 'v'))
        return pasteSelection();

    if (! commandDown && modifiers.isShiftDown() && key.getKeyCode() == juce::KeyPress::tabKey)
        return detailEditorComponent_.toggleMode();

    if (! commandDown && key.getKeyCode() == juce::KeyPress::spaceKey)
        return togglePlayback();

    return false;
}

bool MainComponent::undoLastAction()
{
    const auto result = appServices_.commandStack().undo();
    juce::ignoreUnused (result);
    browserPanelComponent_.refresh();
    timerCallback();
    timelineComponent_.repaint();
    trackListComponent_.refresh();
    detailEditorComponent_.refresh();
    repaint();
    return true;
}

bool MainComponent::redoLastAction()
{
    const auto result = appServices_.commandStack().redo();
    juce::ignoreUnused (result);
    browserPanelComponent_.refresh();
    timerCallback();
    timelineComponent_.repaint();
    trackListComponent_.refresh();
    detailEditorComponent_.refresh();
    repaint();
    return true;
}

bool MainComponent::selectAllInFocusedField()
{
    if (detailEditorComponent_.hasKeyboardFocus (true) && detailEditorComponent_.selectAllNotes())
        return true;

    return timelineComponent_.selectAllInFocusedField();
}

bool MainComponent::copySelection()
{
    if (detailEditorComponent_.hasKeyboardFocus (true) && detailEditorComponent_.copySelectedNotes())
        return true;

    if (timelineComponent_.hasKeyboardFocus (true))
        return timelineComponent_.copySelectionToClipboard();

    if (detailEditorComponent_.hasSelectedNotes() && detailEditorComponent_.copySelectedNotes())
        return true;

    timelineComponent_.copySelectionToClipboard();
    return true;
}

bool MainComponent::pasteSelection()
{
    if (detailEditorComponent_.hasKeyboardFocus (true)
        && detailEditorComponent_.hasOpenClip()
        && detailEditorComponent_.pasteCopiedNotes())
    {
        return true;
    }

    if (timelineComponent_.hasKeyboardFocus (true))
        return timelineComponent_.pasteSelectionFromClipboard();

    if (detailEditorComponent_.hasOpenClip() && detailEditorComponent_.pasteCopiedNotes())
        return true;

    timelineComponent_.pasteSelectionFromClipboard();
    return true;
}

bool MainComponent::togglePlayback()
{
    if (appServices_.playbackEngine().isPlaying())
        appServices_.stopProjectPlayback();
    else
        appServices_.startProjectPlayback();

    timerCallback();
    timelineComponent_.repaint();
    trackListComponent_.refresh();
    detailEditorComponent_.refresh();
    return true;
}

bool MainComponent::handleEscapeKey()
{
    if (audioSettingsComponent_.isVisible() || pluginBrowserComponent_.isVisible() || diagnosticsComponent_.isVisible())
    {
        hideOverlayPanels();
        grabKeyboardFocus();
        return true;
    }

    if (auto* focused = juce::Component::getCurrentlyFocusedComponent();
        focused != nullptr && focused != this)
    {
        juce::Component::unfocusAllComponents();
        grabKeyboardFocus();
        return true;
    }

    return false;
}

void MainComponent::updateWindowTitle()
{
    auto title = juce::String { "TheorySequencer" };
    if (appServices_.currentProjectPackagePath().has_value())
        title = juce::File { appServices_.currentProjectPackagePath()->string() }.getFileNameWithoutExtension() + " - TheorySequencer";

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (title);
}

void MainComponent::hideOverlayPanels()
{
    audioSettingsComponent_.setVisible (false);
    pluginBrowserComponent_.setVisible (false);
    diagnosticsComponent_.setVisible (false);
    resized();
}

void MainComponent::applyPlayheadTick (core::time::TickPosition playheadTick)
{
    playheadTick_ = playheadTick;
    timelineComponent_.setPlayheadTick (playheadTick_);
    detailEditorComponent_.setPlayheadTick (playheadTick_);
}

juce::Rectangle<int> MainComponent::overlayBounds() const
{
    const auto width = std::min (760, std::max (420, getWidth() - 96));
    const auto height = std::min (520, std::max (260, getHeight() - 180));
    return juce::Rectangle<int> { width, height }.withCentre (getLocalBounds().getCentre());
}
}
