#include "app/AppServices.h"
#include "core/commands/AddClipCommand.h"
#include "core/commands/AddKeyCenterRegionCommand.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/AddScaleModeRegionCommand.h"
#include "core/music_theory/PitchClass.h"
#include "core/music_theory/MidiPitch.h"
#include "core/sequencing/KeyCenterRegion.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/ScaleModeRegion.h"
#include "core/time/Tick.h"
#include "engine/TracktionPlaybackEngine.h"
#include "engine/plugins/PluginRegistry.h"
#include "MacNativeEditorDriver.h"
#include "ui/PianoRollComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_MAC
#include <ApplicationServices/ApplicationServices.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace
{
using namespace tsq;

void pumpMessagesFor (int milliseconds)
{
    auto* manager = juce::MessageManager::getInstance();
    REQUIRE (manager != nullptr);
    manager->runDispatchLoopUntil (milliseconds);
}

std::optional<engine::plugins::PluginDescription> findSynthesizerPlugin (const engine::plugins::PluginRegistry& registry)
{
    auto plugins = registry.plugins();
    const auto plugin = std::find_if (plugins.begin(), plugins.end(), [] (const auto& candidate)
    {
        return candidate.name == "Synthesizer" && candidate.isInstrument;
    });

    if (plugin != plugins.end())
        return *plugin;

    const auto installedPath = std::filesystem::path {
        std::string { std::getenv ("HOME") == nullptr ? "" : std::getenv ("HOME") }
    } / "Library/Audio/Plug-Ins/VST3/Synthesizer.vst3";

    if (! std::filesystem::exists (installedPath))
        return std::nullopt;

    engine::plugins::PluginDescription fallback;
    fallback.name = "Synthesizer";
    fallback.manufacturer = "YourCompany";
    fallback.format = "VST3";
    fallback.category = "Instrument|Synth";
    fallback.fileOrIdentifier = installedPath.string();
    fallback.isInstrument = true;
    return fallback;
}

std::vector<engine::PluginParameterDebugValue> waitForPluginParameters (engine::TracktionPlaybackEngine& engine)
{
    for (auto attempt = 0; attempt < 50; ++attempt)
    {
        auto parameters = engine.debugLoadedPluginParameters();
        if (! parameters.empty())
            return parameters;

        pumpMessagesFor (20);
    }

    return {};
}

struct EditedParameter
{
    engine::PluginParameterDebugValue parameter;
    float editedValue = 0.0f;
};

float parameterValue (engine::TracktionPlaybackEngine& engine, int parameterIndex);
void requireParameterNear (engine::TracktionPlaybackEngine& engine, int parameterIndex, float expected);

std::optional<engine::PluginParameterDebugValue> findParameter (const std::vector<engine::PluginParameterDebugValue>& parameters,
                                                               const std::string& parameterId,
                                                               const std::string& parameterName)
{
    const auto exactId = std::find_if (parameters.begin(), parameters.end(), [&parameterId] (const auto& parameter)
    {
        return parameter.parameterId == parameterId;
    });

    if (exactId != parameters.end())
        return *exactId;

    const auto exactName = std::find_if (parameters.begin(), parameters.end(), [&parameterName] (const auto& parameter)
    {
        return parameter.name == parameterName;
    });

    if (exactName != parameters.end())
        return *exactName;

    return std::nullopt;
}

std::vector<engine::PluginParameterDebugValue> watchedSynthParameters (const std::vector<engine::PluginParameterDebugValue>& parameters)
{
    const auto modAmount = findParameter (parameters, "osc1ModAmount", "Osc1 Mod Amount");
    const auto wavefoldAmount = findParameter (parameters, "osc1WavefoldAmount", "Osc1 Wavefold Amount");
    const auto carrierWave = findParameter (parameters, "osc1CarrierWave", "Osc1 Carrier Wave");

    REQUIRE (modAmount.has_value());
    REQUIRE (wavefoldAmount.has_value());
    REQUIRE (carrierWave.has_value());

    return {
        *modAmount,
        *wavefoldAmount,
        *carrierWave
    };
}

std::vector<EditedParameter> editedSynthParametersFromCurrentState (const std::vector<engine::PluginParameterDebugValue>& parameters)
{
    std::vector<EditedParameter> edited;
    for (const auto& parameter : watchedSynthParameters (parameters))
    {
        INFO ("parameter edited through UI: " << parameter.name
              << ", value=" << parameter.value
              << ", default=" << parameter.defaultValue);
        REQUIRE (std::abs (parameter.value - parameter.defaultValue) > 0.01f);
        edited.push_back (EditedParameter { parameter, parameter.value });
    }

    return edited;
}

bool watchedSynthParametersAreEdited (const std::vector<engine::PluginParameterDebugValue>& parameters)
{
    for (const auto& parameter : watchedSynthParameters (parameters))
        if (std::abs (parameter.value - parameter.defaultValue) <= 0.01f)
            return false;

    return true;
}

bool componentHasDirectLabel (const juce::Component& component, std::string_view text)
{
    for (auto index = 0; index < component.getNumChildComponents(); ++index)
    {
        const auto* child = component.getChildComponent (index);
        const auto* label = dynamic_cast<const juce::Label*> (child);
        if (label != nullptr && label->getText().toStdString() == text)
            return true;
    }

    return false;
}

juce::Component* findSynthesizerEditorRoot (juce::Component& component)
{
    if (component.getWidth() >= 1000
        && component.getHeight() >= 700
        && componentHasDirectLabel (component, "Synthesizer")
        && componentHasDirectLabel (component, "Carrier")
        && componentHasDirectLabel (component, "Modulator")
        && componentHasDirectLabel (component, "Wavefolder"))
    {
        return &component;
    }

    for (auto index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* match = findSynthesizerEditorRoot (*component.getChildComponent (index)))
            return match;

    return nullptr;
}

juce::Component* findOpenSynthesizerEditorRoot()
{
    auto& desktop = juce::Desktop::getInstance();
    for (auto index = 0; index < desktop.getNumComponents(); ++index)
        if (auto* component = desktop.getComponent (index))
            if (auto* match = findSynthesizerEditorRoot (*component))
                return match;

    return nullptr;
}

juce::Component* findLargeNativeEditorLeaf (juce::Component& component)
{
    if (component.getNumChildComponents() == 0
        && component.getWidth() >= 1000
        && component.getHeight() >= 700)
    {
        return &component;
    }

    for (auto index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* match = findLargeNativeEditorLeaf (*component.getChildComponent (index)))
            return match;

    return nullptr;
}

juce::Component* findOpenNativeVstEditorView()
{
    auto& desktop = juce::Desktop::getInstance();
    for (auto index = 0; index < desktop.getNumComponents(); ++index)
    {
        auto* component = desktop.getComponent (index);
        if (component == nullptr || component->getName() != "Synthesizer")
            continue;

        if (auto* match = findLargeNativeEditorLeaf (*component))
            return match;
    }

    return nullptr;
}

void appendComponentTree (std::ostringstream& stream, const juce::Component& component, int depth)
{
    for (auto index = 0; index < depth; ++index)
        stream << "  ";

    stream << component.getName().toStdString()
           << " type=" << typeid (component).name()
           << " bounds=" << component.getBounds().toString().toStdString()
           << " children=" << component.getNumChildComponents();

    if (const auto* label = dynamic_cast<const juce::Label*> (&component))
        stream << " label=" << label->getText().toStdString();

    stream << '\n';

    for (auto index = 0; index < component.getNumChildComponents(); ++index)
        if (const auto* child = component.getChildComponent (index))
            appendComponentTree (stream, *child, depth + 1);
}

std::string desktopComponentTree()
{
    std::ostringstream stream;
    auto& desktop = juce::Desktop::getInstance();
    stream << "desktop components=" << desktop.getNumComponents() << '\n';

    for (auto index = 0; index < desktop.getNumComponents(); ++index)
        if (const auto* component = desktop.getComponent (index))
            appendComponentTree (stream, *component, 0);

    return stream.str();
}

#if JUCE_MAC
void forceCurrentProcessToForeground()
{
    auto* nsApplicationClass = objc_getClass ("NSApplication");
    if (nsApplicationClass == nullptr)
        return;

    auto* sharedApplicationSelector = sel_registerName ("sharedApplication");
    auto* setActivationPolicySelector = sel_registerName ("setActivationPolicy:");
    auto* activateSelector = sel_registerName ("activateIgnoringOtherApps:");

    auto* app = reinterpret_cast<void* (*) (void*, SEL)> (objc_msgSend) (nsApplicationClass, sharedApplicationSelector);
    if (app == nullptr)
        return;

    reinterpret_cast<void (*) (void*, SEL, long)> (objc_msgSend) (app, setActivationPolicySelector, 0L);
    reinterpret_cast<void (*) (void*, SEL, bool)> (objc_msgSend) (app, activateSelector, true);
}

CGPoint toCGPoint (juce::Point<float> point)
{
    return CGPointMake (static_cast<CGFloat> (point.x), static_cast<CGFloat> (point.y));
}

void postMouseEvent (CGEventType type, juce::Point<float> point)
{
    auto event = CGEventCreateMouseEvent (nullptr, type, toCGPoint (point), kCGMouseButtonLeft);
    REQUIRE (event != nullptr);
    CGEventSetIntegerValueField (event, kCGMouseEventClickState, 1);
    CGEventPost (kCGHIDEventTap, event);
    CGEventPost (kCGSessionEventTap, event);
    CGEventPost (kCGAnnotatedSessionEventTap, event);
    CGEventPostToPid (getpid(), event);
    CFRelease (event);
    pumpMessagesFor (20);
}

void nativeMouseClick (juce::Point<float> point)
{
    postMouseEvent (kCGEventMouseMoved, point);
    postMouseEvent (kCGEventLeftMouseDown, point);
    postMouseEvent (kCGEventLeftMouseUp, point);
    pumpMessagesFor (120);
}

void nativeMouseDrag (juce::Point<float> start, juce::Point<float> end)
{
    postMouseEvent (kCGEventMouseMoved, start);
    postMouseEvent (kCGEventLeftMouseDown, start);

    constexpr auto steps = 12;
    for (auto step = 1; step <= steps; ++step)
    {
        const auto alpha = static_cast<float> (step) / static_cast<float> (steps);
        postMouseEvent (kCGEventLeftMouseDragged, start + ((end - start) * alpha));
    }

    postMouseEvent (kCGEventLeftMouseUp, end);
    pumpMessagesFor (250);
}

void performNativeSynthEditorMouseEdits (const std::function<juce::Point<float> (float, float)>& screenPoint)
{
    nativeMouseClick (screenPoint (103.0f, 207.0f));
    nativeMouseClick (screenPoint (103.0f, 257.0f));
    nativeMouseDrag (screenPoint (190.0f, 468.0f), screenPoint (190.0f, 328.0f));
    nativeMouseDrag (screenPoint (86.0f, 620.0f), screenPoint (86.0f, 480.0f));
    pumpMessagesFor (700);
}

std::string shellQuote (const std::string& text)
{
    std::string quoted = "'";
    for (const auto character : text)
    {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }

    quoted += "'";
    return quoted;
}

bool runAppleScriptLines (const std::vector<std::string>& lines)
{
    std::string command = "/usr/bin/osascript";
    for (const auto& line : lines)
        command += " -e " + shellQuote (line);

    auto result = std::async (std::launch::async, [command]
    {
        return std::system (command.c_str());
    });

    while (result.wait_for (std::chrono::milliseconds (10)) != std::future_status::ready)
        pumpMessagesFor (10);

    pumpMessagesFor (250);
    return result.get() == 0;
}

bool runExternalMouseTool (const std::vector<std::string>& arguments)
{
    auto tool = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                    .getSiblingFile ("tsq_mac_mouse_event_tool")
                    .getFullPathName()
                    .toStdString();

    std::string command = shellQuote (tool);
    for (const auto& argument : arguments)
        command += " " + shellQuote (argument);

    auto result = std::async (std::launch::async, [command]
    {
        return std::system (command.c_str());
    });

    while (result.wait_for (std::chrono::milliseconds (10)) != std::future_status::ready)
        pumpMessagesFor (10);

    pumpMessagesFor (250);
    return result.get() == 0;
}

std::string appleScriptPoint (juce::Point<float> point)
{
    return "{" + std::to_string (juce::roundToInt (point.x)) + ", "
               + std::to_string (juce::roundToInt (point.y)) + "}";
}

void performSystemEventsSynthEditorTextEdits (const std::function<juce::Point<float> (float, float)>& screenPoint)
{
    const auto carrierWave = appleScriptPoint (screenPoint (103.0f, 207.0f));
    const auto modAmountText = appleScriptPoint (screenPoint (190.0f, 522.0f));
    const auto wavefoldAmountText = appleScriptPoint (screenPoint (86.0f, 674.0f));

    REQUIRE (runAppleScriptLines ({
        "tell application \"System Events\"",
        "tell process \"tsq_engine_integration_tests\"",
        "set frontmost to true",
        "click at " + carrierWave,
        "delay 0.15",
        "key code 125",
        "delay 0.05",
        "key code 36",
        "delay 0.15",
        "click at " + modAmountText,
        "delay 0.1",
        "keystroke \"a\" using command down",
        "keystroke \"7\"",
        "key code 36",
        "delay 0.15",
        "click at " + wavefoldAmountText,
        "delay 0.1",
        "keystroke \"a\" using command down",
        "keystroke \"1.2\"",
        "key code 36",
        "end tell",
        "end tell"
    }));

    pumpMessagesFor (1000);
}

void performExternalMouseToolSynthEditorMouseEdits (const std::function<juce::Point<float> (float, float)>& screenPoint)
{
    const auto click = [] (juce::Point<float> point)
    {
        REQUIRE (runExternalMouseTool ({ "click", std::to_string (point.x), std::to_string (point.y) }));
    };

    const auto drag = [] (juce::Point<float> start, juce::Point<float> end)
    {
        REQUIRE (runExternalMouseTool ({
            "drag",
            std::to_string (start.x),
            std::to_string (start.y),
            std::to_string (end.x),
            std::to_string (end.y)
        }));
    };

    click (screenPoint (103.0f, 207.0f));
    click (screenPoint (103.0f, 257.0f));
    drag (screenPoint (190.0f, 468.0f), screenPoint (190.0f, 328.0f));
    drag (screenPoint (86.0f, 620.0f), screenPoint (86.0f, 480.0f));
    pumpMessagesFor (1000);
}

bool externalMouseClick (juce::Point<float> point)
{
    return runExternalMouseTool ({ "click", std::to_string (point.x), std::to_string (point.y) });
}

bool externalMouseDoubleClick (juce::Point<float> point)
{
    return runExternalMouseTool ({ "doubleclick", std::to_string (point.x), std::to_string (point.y) });
}

void* nativeViewForComponent (juce::Component& component)
{
    auto* nsViewComponent = dynamic_cast<juce::NSViewComponent*> (&component);
    if (nsViewComponent != nullptr)
        return nsViewComponent->getView();

    const auto typeName = std::string { typeid (component).name() };
    if (typeName.find ("NSViewComponentWithParent") != std::string::npos)
        return reinterpret_cast<juce::NSViewComponent*> (&component)->getView();

    return nullptr;
}

void performAppKitSynthEditorMouseEdits (void* nsView)
{
    REQUIRE (tests::clickMacNativeEditorView (nsView, { 103.0, 207.0 }));
    pumpMessagesFor (120);
    REQUIRE (tests::clickMacNativeEditorView (nsView, { 103.0, 257.0 }));
    pumpMessagesFor (120);
    REQUIRE (tests::dragMacNativeEditorView (nsView, { 190.0, 468.0 }, { 190.0, 328.0 }));
    pumpMessagesFor (250);
    REQUIRE (tests::dragMacNativeEditorView (nsView, { 86.0, 620.0 }, { 86.0, 480.0 }));
    pumpMessagesFor (700);
}

std::vector<EditedParameter> editWatchedSynthParametersThroughNativeEditorMouse (engine::TracktionPlaybackEngine& engine)
{
    auto* nativeEditorView = findOpenNativeVstEditorView();
    INFO (desktopComponentTree());
    REQUIRE (nativeEditorView != nullptr);
    INFO ("CGPreflightPostEventAccess=" << (CGPreflightPostEventAccess() ? "true" : "false"));

    juce::Process::makeForegroundProcess();
    forceCurrentProcessToForeground();
    if (auto* topLevel = nativeEditorView->getTopLevelComponent())
    {
        topLevel->setAlwaysOnTop (true);
        topLevel->toFront (true);
    }
    pumpMessagesFor (250);

    auto* nsView = nativeViewForComponent (*nativeEditorView);
    UNSCOPED_INFO ("native NSView pointer=" << nsView);
    if (nsView != nullptr)
    {
        UNSCOPED_INFO ("native NSView tree:\n" << tests::describeMacNativeViewTree (nsView));
        UNSCOPED_INFO ("native NSView carrier hit: " << tests::describeMacNativeHitView (nsView, { 103.0, 207.0 }));
        UNSCOPED_INFO ("native NSView mod hit: " << tests::describeMacNativeHitView (nsView, { 190.0, 468.0 }));
        UNSCOPED_INFO ("native NSView fold hit: " << tests::describeMacNativeHitView (nsView, { 86.0, 620.0 }));
        performAppKitSynthEditorMouseEdits (nsView);
        auto parameters = waitForPluginParameters (engine);
        if (watchedSynthParametersAreEdited (parameters))
            return editedSynthParametersFromCurrentState (parameters);
    }

    const auto directScreenPoint = [nativeEditorView] (float x, float y)
    {
        return nativeEditorView->localPointToGlobal (juce::Point<float> { x, y });
    };
    const auto platformScale = nativeEditorView->getPeer() == nullptr ? 1.0 : nativeEditorView->getPeer()->getPlatformScaleFactor();
    INFO ("native editor platformScale=" << platformScale
          << " carrier=" << directScreenPoint (103.0f, 207.0f).toString().toStdString()
          << " mod=" << directScreenPoint (190.0f, 468.0f).toString().toStdString()
          << " fold=" << directScreenPoint (86.0f, 620.0f).toString().toStdString());

    performNativeSynthEditorMouseEdits (directScreenPoint);
    auto parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    performExternalMouseToolSynthEditorMouseEdits (directScreenPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    performSystemEventsSynthEditorTextEdits (directScreenPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    const auto mainDisplayBounds = CGDisplayBounds (CGMainDisplayID());
    const auto* primaryDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto logicalDisplayWidth = primaryDisplay == nullptr ? static_cast<float> (mainDisplayBounds.size.width)
                                                               : primaryDisplay->logicalBounds.getWidth();
    const auto displayPixelScale = static_cast<float> (CGDisplayPixelsWide (CGMainDisplayID()))
                                   / std::max (1.0f, logicalDisplayWidth);
    const auto componentScale = nativeEditorView->getDesktopScaleFactor();
    const auto eventCoordinateScale = std::max ({ displayPixelScale, componentScale, 2.0f });
    INFO ("native editor displayPixelScale=" << displayPixelScale
          << " componentScale=" << componentScale
          << " eventCoordinateScale=" << eventCoordinateScale);

    const auto pixelScaledScreenPoint = [nativeEditorView, eventCoordinateScale] (float x, float y)
    {
        const auto point = nativeEditorView->localPointToGlobal (juce::Point<float> { x, y });
        return juce::Point<float> { point.x * eventCoordinateScale, point.y * eventCoordinateScale };
    };

    performNativeSynthEditorMouseEdits (pixelScaledScreenPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    performExternalMouseToolSynthEditorMouseEdits (pixelScaledScreenPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    performSystemEventsSynthEditorTextEdits (pixelScaledScreenPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    const auto flippedLogicalSystemEventsPoint = [nativeEditorView, mainDisplayBounds, eventCoordinateScale] (float x, float y)
    {
        const auto point = nativeEditorView->localPointToGlobal (juce::Point<float> { x, y });
        const auto logicalHeight = static_cast<float> (mainDisplayBounds.size.height / eventCoordinateScale);
        return juce::Point<float> { point.x, logicalHeight - point.y };
    };

    performSystemEventsSynthEditorTextEdits (flippedLogicalSystemEventsPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    const auto flippedPhysicalSystemEventsPoint = [nativeEditorView, mainDisplayBounds, eventCoordinateScale] (float x, float y)
    {
        const auto point = nativeEditorView->localPointToGlobal (juce::Point<float> { x, y });
        return juce::Point<float> {
            point.x * eventCoordinateScale,
            static_cast<float> (mainDisplayBounds.size.height) - (point.y * eventCoordinateScale)
        };
    };

    performSystemEventsSynthEditorTextEdits (flippedPhysicalSystemEventsPoint);
    parameters = waitForPluginParameters (engine);
    if (watchedSynthParametersAreEdited (parameters))
        return editedSynthParametersFromCurrentState (parameters);

    const auto flippedScreenPoint = [nativeEditorView, mainDisplayBounds] (float x, float y)
    {
        const auto point = nativeEditorView->localPointToGlobal (juce::Point<float> { x, y });
        return juce::Point<float> {
            point.x,
            static_cast<float> (mainDisplayBounds.size.height - point.y)
        };
    };

    performNativeSynthEditorMouseEdits (flippedScreenPoint);
    parameters = waitForPluginParameters (engine);

    return editedSynthParametersFromCurrentState (parameters);
}

void allowNativeVstEditorBehindOtherWindows()
{
    if (auto* nativeEditorView = findOpenNativeVstEditorView())
        if (auto* topLevel = nativeEditorView->getTopLevelComponent())
            topLevel->setAlwaysOnTop (false);
}
#endif

template<typename ComponentType>
ComponentType* directChildAt (juce::Component& parent, juce::Rectangle<int> bounds)
{
    const auto centre = bounds.getCentre();
    for (auto index = 0; index < parent.getNumChildComponents(); ++index)
    {
        auto* child = parent.getChildComponent (index);
        if (auto* typedChild = dynamic_cast<ComponentType*> (child))
        {
            const auto childBounds = child->getBounds();
            if (std::abs (childBounds.getCentreX() - centre.x) <= 3
                && std::abs (childBounds.getCentreY() - centre.y) <= 3
                && std::abs (childBounds.getWidth() - bounds.getWidth()) <= 6
                && std::abs (childBounds.getHeight() - bounds.getHeight()) <= 6)
            {
                return typedChild;
            }
        }
    }

    return nullptr;
}

std::vector<EditedParameter> editWatchedSynthParametersThroughEditor (engine::TracktionPlaybackEngine& engine)
{
    auto* editorRoot = findOpenSynthesizerEditorRoot();
    if (editorRoot == nullptr)
    {
#if JUCE_MAC
        return editWatchedSynthParametersThroughNativeEditorMouse (engine);
#else
        INFO (desktopComponentTree());
        REQUIRE (editorRoot != nullptr);
#endif
    }

    auto* carrierWave = directChildAt<juce::ComboBox> (*editorRoot, { 42, 194, 122, 26 });
    auto* modAmount = directChildAt<juce::Slider> (*editorRoot, { 146, 426, 88, 84 });
    auto* wavefoldAmount = directChildAt<juce::Slider> (*editorRoot, { 42, 578, 88, 84 });

    REQUIRE (carrierWave != nullptr);
    REQUIRE (modAmount != nullptr);
    REQUIRE (wavefoldAmount != nullptr);

    carrierWave->setSelectedId (2, juce::sendNotificationSync);
    modAmount->setValue (7.0, juce::sendNotificationSync);
    wavefoldAmount->setValue (1.2, juce::sendNotificationSync);
    pumpMessagesFor (250);

    return editedSynthParametersFromCurrentState (waitForPluginParameters (engine));
}

void requireParametersNear (engine::TracktionPlaybackEngine& engine, const std::vector<EditedParameter>& editedParameters)
{
    for (const auto& editedParameter : editedParameters)
    {
        INFO ("parameter: " << editedParameter.parameter.name << " (" << editedParameter.parameter.parameterId << ")");
        requireParameterNear (engine, editedParameter.parameter.index, editedParameter.editedValue);
    }
}

void requireParameterDefaultsNear (engine::TracktionPlaybackEngine& engine, const std::vector<EditedParameter>& editedParameters)
{
    for (const auto& editedParameter : editedParameters)
    {
        INFO ("parameter: " << editedParameter.parameter.name << " (" << editedParameter.parameter.parameterId << ")");
        requireParameterNear (engine, editedParameter.parameter.index, editedParameter.parameter.defaultValue);
    }
}

float parameterValue (engine::TracktionPlaybackEngine& engine, int parameterIndex)
{
    const auto parameters = engine.debugLoadedPluginParameters();
    const auto parameter = std::find_if (parameters.begin(), parameters.end(), [parameterIndex] (const auto& value)
    {
        return value.index == parameterIndex;
    });

    REQUIRE (parameter != parameters.end());
    return parameter->value;
}

void requireParameterNear (engine::TracktionPlaybackEngine& engine, int parameterIndex, float expected)
{
    const auto actual = parameterValue (engine, parameterIndex);
    INFO ("expected normalized value: " << expected << ", actual normalized value: " << actual);
    REQUIRE (std::abs (actual - expected) < 0.01f);
}

core::sequencing::MidiClip testClip()
{
    return core::sequencing::MidiClip {
        "integration-clip-1",
        "Integration Clip",
        core::time::TickPosition {},
        core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote * 16)
    };
}

core::time::TickPosition beatPosition (int beat)
{
    return core::time::TickPosition::fromTicks (core::time::ticksPerQuarterNote * beat);
}

core::sequencing::Region beatRegion (int startBeat, int endBeat)
{
    return core::sequencing::Region { beatPosition (startBeat), beatPosition (endBeat) };
}

core::sequencing::MidiNote testNote()
{
    return core::sequencing::MidiNote {
        "integration-note-1",
        core::music_theory::MidiPitch::fromValue (60),
        core::time::TickPosition {},
        core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote),
        100
    };
}

std::size_t noteCountForClip (const app::AppServices& services,
                              const std::string& trackId,
                              const std::string& clipId)
{
    for (const auto& track : services.project().tracks())
    {
        if (track.id() != trackId)
            continue;

        for (const auto& clip : track.clips())
            if (clip.id() == clipId)
                return clip.notes().size();
    }

    return 0;
}

std::vector<juce::Point<float>> pianoRollClickPointCandidates (juce::Point<float> point)
{
    std::vector<juce::Point<float>> candidates { point };

#if JUCE_MAC
    const auto mainDisplayBounds = CGDisplayBounds (CGMainDisplayID());
    const auto displayHeight = static_cast<float> (mainDisplayBounds.size.height);
    candidates.push_back ({ point.x, point.y + displayHeight });
    candidates.push_back ({ point.x, point.y + (displayHeight * 0.5f) });
    candidates.push_back ({ point.x * 2.0f, (point.y + displayHeight) * 2.0f });
    candidates.push_back ({ point.x * 2.0f, point.y + displayHeight });
#endif

    return candidates;
}
}

TEST_CASE ("Synthesizer plugin parameters survive MIDI clip and note creation", "[integration][vst3][plugin-state]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    auto synthesizer = findSynthesizerPlugin (services.pluginRegistry());
    if (! synthesizer.has_value())
        SKIP ("Synthesizer.vst3 is not installed on this machine");

    REQUIRE (services.assignInstrumentToTrack ("track-1", *synthesizer));

    auto* tracktionEngine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (tracktionEngine != nullptr);

    REQUIRE (services.playbackEngine().openLoadedPluginEditor());
    pumpMessagesFor (300);

    const auto parameters = waitForPluginParameters (*tracktionEngine);
    REQUIRE_FALSE (parameters.empty());

    services.observeLivePluginParameterState();
    const auto editedParameters = editWatchedSynthParametersThroughEditor (*tracktionEngine);
    requireParametersNear (*tracktionEngine, editedParameters);
#if JUCE_MAC
    allowNativeVstEditorBehindOtherWindows();
#endif

    services.playbackEngine().setProjectPluginStateDirectory (
        std::filesystem::temp_directory_path() / "TheorySequencerVstStateRegression.tseq");
    services.markPlaybackProjectDirty();
    REQUIRE (services.syncPlaybackProjectIfNeeded());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    auto addKeyRegionResult = services.commandStack().execute (
        std::make_unique<core::commands::AddKeyCenterRegionCommand> (
            core::sequencing::KeyCenterRegion { beatRegion (8, 16), core::music_theory::PitchClass::g() }));
    REQUIRE (addKeyRegionResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    auto addScaleRegionResult = services.commandStack().execute (
        std::make_unique<core::commands::AddScaleModeRegionCommand> (
            core::sequencing::ScaleModeRegion { beatRegion (8, 16), "Lydian" }));
    REQUIRE (addScaleRegionResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> ("track-1", testClip()));
    REQUIRE (addClipResult.succeeded());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    ui::PianoRollComponent pianoRoll { services };
    juce::DocumentWindow pianoRollWindow { "Piano Roll Regression",
                                           juce::Colours::black,
                                           juce::DocumentWindow::minimiseButton };
    pianoRollWindow.setContentNonOwned (&pianoRoll, false);
    pianoRollWindow.centreWithSize (960, 360);
    pianoRollWindow.setVisible (true);
    pianoRollWindow.setAlwaysOnTop (true);
    pianoRollWindow.toFront (true);
    pianoRoll.openClip ("track-1", "integration-clip-1");
    pumpMessagesFor (500);

    const auto pianoRollScreenBounds = pianoRoll.getScreenBounds();
    const auto editableCellPoint = pianoRollScreenBounds.getPosition().toFloat() + juce::Point<float> { 80.0f, 63.0f };

    auto externalPianoNoteCreated = false;
    const auto initialExternalNoteCount = noteCountForClip (services, "track-1", "integration-clip-1");
    for (const auto point : pianoRollClickPointCandidates (editableCellPoint))
    {
        REQUIRE (externalMouseClick (point));
        pumpMessagesFor (500);
        requireParametersNear (*tracktionEngine, editedParameters);
        if (noteCountForClip (services, "track-1", "integration-clip-1") > initialExternalNoteCount)
        {
            externalPianoNoteCreated = true;
            break;
        }

        const auto beforeExternalNoteCount = noteCountForClip (services, "track-1", "integration-clip-1");
        REQUIRE (externalMouseDoubleClick (point));
        pumpMessagesFor (500);
        const auto afterExternalNoteCount = noteCountForClip (services, "track-1", "integration-clip-1");
        if (afterExternalNoteCount > initialExternalNoteCount)
        {
            externalPianoNoteCreated = true;
            break;
        }
    }

    REQUIRE (externalPianoNoteCreated);
    requireParametersNear (*tracktionEngine, editedParameters);

    auto addNoteResult = services.commandStack().execute (
        std::make_unique<core::commands::AddNoteCommand> ("track-1", "integration-clip-1", testNote()));
    REQUIRE (addNoteResult.succeeded());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    pumpMessagesFor (2500);
    for (const auto& editedParameter : editedParameters)
        REQUIRE (tracktionEngine->debugSetLoadedPluginParameterValue (editedParameter.parameter.index, editedParameter.parameter.defaultValue));

    requireParameterDefaultsNear (*tracktionEngine, editedParameters);
    services.observeLivePluginParameterState();
    pumpMessagesFor (50);
    requireParametersNear (*tracktionEngine, editedParameters);

    REQUIRE (services.syncPlaybackProjectIfNeeded());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    REQUIRE (services.returnPlaybackToStart());
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);

    REQUIRE (services.startProjectPlayback());
    pumpMessagesFor (500);
    services.stopProjectPlayback();
    pumpMessagesFor (500);
    requireParametersNear (*tracktionEngine, editedParameters);
}
