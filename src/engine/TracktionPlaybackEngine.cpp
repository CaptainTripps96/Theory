#include "engine/TracktionPlaybackEngine.h"

#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/sequencing/AutomationPlayback.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/MixerMath.h"
#include "core/sequencing/PitchExpressionEvaluation.h"
#include "core/sequencing/PreparedExpressionRenderModel.h"
#include "core/sequencing/Routing.h"
#include "engine/devices/FirstPartyEffectTracktionPlugin.h"
#include "engine/devices/SimpleOscComplexTracktionPlugin.h"
#include "engine/plugins/PluginDescription.h"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsq::engine
{
namespace te = tracktion::engine;

namespace
{
class TheorySequencerEngineBehaviour final : public te::EngineBehaviour
{
public:
    bool shouldOpenAudioInputByDefault() override
    {
        return false;
    }
};

#if JUCE_LINUX
constexpr bool shouldAddPluginWindowToDesktop = false;
#else
constexpr bool shouldAddPluginWindowToDesktop = true;
#endif

class TheorySequencerPluginWindow final : public juce::DocumentWindow
{
public:
    explicit TheorySequencerPluginWindow (te::Plugin& plugin)
        : juce::DocumentWindow (plugin.getName(),
                                juce::Colours::black,
                                juce::DocumentWindow::closeButton,
                                shouldAddPluginWindowToDesktop),
          plugin_ (plugin),
          windowState_ (*plugin.windowState)
    {
        getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
        setResizeLimits (100, 50, 4000, 4000);
        recreateEditor();
        setBoundsConstrained (getLocalBounds() + plugin_.windowState->choosePositionForPluginWindow());

#if JUCE_LINUX
        setAlwaysOnTop (true);
        addToDesktop();
#endif

        updateStoredBounds_ = true;
    }

    ~TheorySequencerPluginWindow() override
    {
        updateStoredBounds_ = false;
        plugin_.edit.flushPluginStateIfNeeded (plugin_);
        setEditor (nullptr);
    }

    static std::unique_ptr<juce::Component> create (te::Plugin& plugin)
    {
        if (auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (&plugin))
            if (externalPlugin->getAudioPluginInstance() == nullptr)
                return {};

        auto window = std::make_unique<TheorySequencerPluginWindow> (plugin);

        if (window->editor_ == nullptr)
            return {};

        window->show();
        return window;
    }

    void recreateEditor()
    {
        setEditor (nullptr);
        setEditor (plugin_.createEditor());
    }

    void recreateEditorAsync()
    {
        setEditor (nullptr);

        juce::Component::SafePointer<TheorySequencerPluginWindow> safeThis { this };

        juce::Timer::callAfterDelay (50,
                                     [safeThis]
                                     {
                                         if (safeThis != nullptr)
                                             safeThis->recreateEditor();
                                     });
    }

private:
    void show()
    {
        setVisible (true);
        toFront (false);
        setBoundsConstrained (getBounds());
    }

    void setEditor (std::unique_ptr<te::Plugin::EditorComponent> editor)
    {
        JUCE_AUTORELEASEPOOL
        {
            setConstrainer (nullptr);
            editor_.reset();

            if (editor != nullptr)
            {
                editor_ = std::move (editor);
                setContentNonOwned (editor_.get(), true);
            }

            setResizable (editor_ == nullptr || editor_->allowWindowResizing(), false);

            if (editor_ != nullptr && editor_->allowWindowResizing())
                setConstrainer (editor_->getBoundsConstrainer());
        }
    }

    void moved() override
    {
        if (updateStoredBounds_)
        {
            plugin_.windowState->lastWindowBounds = getBounds();
            plugin_.edit.pluginChanged (plugin_);
        }
    }

    void userTriedToCloseWindow() override
    {
        windowState_.closeWindowExplicitly();
    }

    void closeButtonPressed() override
    {
        userTriedToCloseWindow();
    }

    float getDesktopScaleFactor() const override
    {
        return 1.0f;
    }

    te::Plugin& plugin_;
    te::PluginWindowState& windowState_;
    std::unique_ptr<te::Plugin::EditorComponent> editor_;
    bool updateStoredBounds_ = false;
};

class TheorySequencerUIBehaviour final : public te::UIBehaviour
{
public:
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& pluginWindowState) override
    {
        if (auto* windowState = dynamic_cast<te::Plugin::WindowState*> (&pluginWindowState))
            return TheorySequencerPluginWindow::create (windowState->plugin);

        return {};
    }

    void recreatePluginWindowContentAsync (te::Plugin& plugin) override
    {
        if (auto* window = dynamic_cast<TheorySequencerPluginWindow*> (plugin.windowState->pluginWindow.get()))
            return window->recreateEditorAsync();

        te::UIBehaviour::recreatePluginWindowContentAsync (plugin);
    }
};

std::string toStdString (const juce::String& text)
{
    return text.toStdString();
}

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

std::vector<std::uint8_t> memoryBlockToBytes (const juce::MemoryBlock& block)
{
    const auto* data = static_cast<const std::uint8_t*> (block.getData());
    return { data, data + block.getSize() };
}

std::optional<juce::MemoryBlock> readPluginStateFile (const std::filesystem::path& packagePath, const std::string& relativePath)
{
    if (packagePath.empty() || relativePath.empty())
        return {};

    const std::filesystem::path relative { relativePath };
    if (relative.is_absolute())
        return {};

    for (const auto& part : relative)
        if (part == "..")
            return {};

    std::ifstream stream { packagePath / relative, std::ios::binary };
    if (! stream)
        return {};

    const std::vector<char> bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    juce::MemoryBlock block;
    if (! bytes.empty())
        block.append (bytes.data(), bytes.size());

    return block;
}

juce::PluginDescription toJuceDescription (const plugins::PluginDescription& source)
{
    juce::PluginDescription description;
    description.name = toJuceString (source.name);
    description.descriptiveName = toJuceString (source.name);
    description.manufacturerName = toJuceString (source.manufacturer);
    description.pluginFormatName = toJuceString (source.format.empty() ? "VST3" : source.format);
    description.category = toJuceString (source.category);
    description.version = toJuceString (source.version);
    description.fileOrIdentifier = toJuceString (source.fileOrIdentifier);
    description.uniqueId = source.uniqueId;
    description.deprecatedUid = source.deprecatedUid;
    description.isInstrument = source.isInstrument;
    description.numInputChannels = source.numInputChannels;
    description.numOutputChannels = source.numOutputChannels;
    return description;
}

juce::PluginDescription toJuceDescription (const core::sequencing::PluginReference& source,
                                           core::sequencing::PluginKind kind)
{
    juce::PluginDescription description;
    description.name = toJuceString (source.pluginName);
    description.descriptiveName = toJuceString (source.pluginName);
    description.manufacturerName = toJuceString (source.manufacturer);
    description.pluginFormatName = toJuceString (source.format.empty() ? "VST3" : source.format);
    description.fileOrIdentifier = toJuceString (source.fileOrIdentifier);
    description.uniqueId = source.uniqueId;
    description.deprecatedUid = source.deprecatedUid;
    description.isInstrument = kind == core::sequencing::PluginKind::instrument;
    description.numInputChannels = source.numInputChannels;
    description.numOutputChannels = source.numOutputChannels;
    return description;
}

tracktion::BeatPosition toBeatPosition (core::time::TickPosition position)
{
    return tracktion::BeatPosition::fromBeats (static_cast<double> (position.ticks()) / static_cast<double> (core::time::ticksPerQuarterNote));
}

core::time::TickPosition toTickPosition (tracktion::BeatPosition position)
{
    return core::time::TickPosition::fromTicks (
        static_cast<std::int64_t> (std::llround (position.inBeats() * static_cast<double> (core::time::ticksPerQuarterNote))));
}

tracktion::BeatDuration toBeatDuration (core::time::TickDuration duration)
{
    return tracktion::BeatDuration::fromBeats (static_cast<double> (duration.ticks()) / static_cast<double> (core::time::ticksPerQuarterNote));
}

tracktion::BeatRange toBeatRange (core::time::TickPosition start, core::time::TickDuration duration)
{
    const auto startBeat = toBeatPosition (start);
    return { startBeat, startBeat + toBeatDuration (duration) };
}

core::time::TickPosition projectEndPosition (const core::sequencing::Project& project)
{
    auto end = core::time::TickPosition {};
    for (const auto& track : project.tracks())
    {
        for (const auto& clip : track.clips())
            if (clip.endInProject() > end)
                end = clip.endInProject();

        for (const auto& clip : track.audioClips())
            if (clip.endInProject() > end)
                end = clip.endInProject();
    }

    return end;
}

bool projectHasAutomationLanes (const core::sequencing::Project& project)
{
    return std::any_of (project.tracks().begin(), project.tracks().end(), [] (const auto& track)
    {
        return ! track.automationLanes().empty();
    });
}

bool pluginReferencesMatchForLiveState (const core::sequencing::PluginReference& lhs,
                                        const core::sequencing::PluginReference& rhs) noexcept
{
    return lhs.pluginName == rhs.pluginName
        && lhs.manufacturer == rhs.manufacturer
        && lhs.format == rhs.format
        && lhs.fileOrIdentifier == rhs.fileOrIdentifier
        && lhs.uniqueIdentifier == rhs.uniqueIdentifier
        && lhs.uniqueId == rhs.uniqueId
        && lhs.deprecatedUid == rhs.deprecatedUid
        && lhs.numInputChannels == rhs.numInputChannels
        && lhs.numOutputChannels == rhs.numOutputChannels;
}

float normalizedSendLevelToDb (double normalizedLevel)
{
    return static_cast<float> (core::sequencing::sendDecibelsFromNormalizedLevel (normalizedLevel, -60.0));
}

std::string pluginStateKey (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    return trackId + ":" + slotId.value;
}

std::string automationSendRouteKey (const std::string& trackId, const std::string& returnTrackId)
{
    return trackId + "->" + returnTrackId;
}

std::string nativeExpressionRouteKey (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    return trackId + ":" + slotId.value;
}

core::devices::SimpleOscComplexNoteId stableSimpleOscNoteId (const std::string& trackId,
                                                             const std::string& clipId,
                                                             const std::string& noteId,
                                                             std::int64_t repetitionStartTicks)
{
    auto hash = std::uint64_t { 1469598103934665603ull };
    const auto feed = [&hash] (std::string_view value)
    {
        for (const auto character : value)
        {
            hash ^= static_cast<std::uint8_t> (character);
            hash *= 1099511628211ull;
        }
        hash ^= 0xffu;
        hash *= 1099511628211ull;
    };

    feed (trackId);
    feed (clipId);
    feed (noteId);
    feed (std::to_string (repetitionStartTicks));
    return hash == 0 ? 1 : hash;
}

std::vector<core::sequencing::DeviceSlot> effectiveDeviceSlotsForTrack (const core::sequencing::Track& track)
{
    if (! track.deviceChain().empty())
        return track.deviceChain().slots();

    if (! track.instrument().has_value())
        return {};

    core::sequencing::DeviceSlot slot {
        core::sequencing::DeviceSlotId { "instrument" },
        core::sequencing::PluginReference::fromTrackInstrumentReference (*track.instrument()),
        core::sequencing::PluginKind::instrument
    };
    slot.setPluginStateFile (track.instrument()->pluginStateFile);
    return { slot };
}

int playbackAudioTrackCount (const core::sequencing::Project& project)
{
    const auto count = std::count_if (project.tracks().begin(), project.tracks().end(), [] (const auto& track)
    {
        return track.type() != core::sequencing::TrackType::master;
    });

    return std::max (1, static_cast<int> (count));
}

float meterLinearFromDb (float decibels) noexcept
{
    if (! std::isfinite (decibels) || decibels <= -100.0f)
        return 0.0f;

    return static_cast<float> (std::pow (10.0, static_cast<double> (decibels) / 20.0));
}

bool midiNotesMatchForPlayback (const core::sequencing::MidiNote& lhs,
                                const core::sequencing::MidiNote& rhs) noexcept
{
    return lhs.id() == rhs.id()
        && lhs.pitch() == rhs.pitch()
        && lhs.startInClip() == rhs.startInClip()
        && lhs.duration() == rhs.duration()
        && lhs.velocity() == rhs.velocity();
}

bool clipLoopsMatchForPlayback (const core::sequencing::ClipLoop& lhs,
                                const core::sequencing::ClipLoop& rhs) noexcept
{
    return lhs.isEnabled() == rhs.isEnabled()
        && lhs.loopDuration() == rhs.loopDuration();
}

bool midiClipsMatchForPlayback (const core::sequencing::MidiClip& lhs,
                                const core::sequencing::MidiClip& rhs) noexcept
{
    if (lhs.id() != rhs.id()
        || lhs.name() != rhs.name()
        || lhs.startInProject() != rhs.startInProject()
        || lhs.length() != rhs.length()
        || ! clipLoopsMatchForPlayback (lhs.loop(), rhs.loop())
        || lhs.notes().size() != rhs.notes().size())
        return false;

    for (std::size_t index = 0; index < lhs.notes().size(); ++index)
        if (! midiNotesMatchForPlayback (lhs.notes()[index], rhs.notes()[index]))
            return false;

    return true;
}

bool audioSourcesMatchForPlayback (const core::sequencing::AudioSourceReference& lhs,
                                   const core::sequencing::AudioSourceReference& rhs) noexcept
{
    return lhs.sourceId == rhs.sourceId
        && lhs.filePath == rhs.filePath
        && lhs.displayName == rhs.displayName
        && lhs.embeddedInProject == rhs.embeddedInProject;
}

bool audioClipsMatchForPlayback (const core::sequencing::AudioClip& lhs,
                                 const core::sequencing::AudioClip& rhs) noexcept
{
    return lhs.id() == rhs.id()
        && lhs.name() == rhs.name()
        && audioSourcesMatchForPlayback (lhs.source(), rhs.source())
        && lhs.startInProject() == rhs.startInProject()
        && lhs.length() == rhs.length()
        && lhs.sourceOffset() == rhs.sourceOffset()
        && lhs.loopEnabled() == rhs.loopEnabled()
        && lhs.stretchToTempo() == rhs.stretchToTempo()
        && lhs.gainDb() == rhs.gainDb();
}

bool trackClipMaterializationMatches (const core::sequencing::Track& currentTrack,
                                      const core::sequencing::Track* previousTrack) noexcept
{
    if (previousTrack == nullptr
        || currentTrack.id() != previousTrack->id()
        || currentTrack.type() != previousTrack->type()
        || currentTrack.clips().size() != previousTrack->clips().size()
        || currentTrack.audioClips().size() != previousTrack->audioClips().size())
        return false;

    for (std::size_t index = 0; index < currentTrack.clips().size(); ++index)
        if (! midiClipsMatchForPlayback (currentTrack.clips()[index], previousTrack->clips()[index]))
            return false;

    for (std::size_t index = 0; index < currentTrack.audioClips().size(); ++index)
        if (! audioClipsMatchForPlayback (currentTrack.audioClips()[index], previousTrack->audioClips()[index]))
            return false;

    return true;
}

void assertMessageThreadIfAvailable()
{
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        jassert (manager->isThisTheMessageThread());
}
}

class TracktionPlaybackEngine::Impl final
{
public:
    bool initialize()
    {
        assertMessageThreadIfAvailable();

        if (tracktionEngine_ != nullptr)
            return true;

        status_.backendName = "Tracktion Engine";

        try
        {
            tracktionEngine_ = std::make_unique<te::Engine> ("TheorySequencer",
                                                             std::make_unique<TheorySequencerUIBehaviour>(),
                                                             std::make_unique<TheorySequencerEngineBehaviour>());
            tracktionEngine_->getPluginManager().initialise();
            devices::registerSimpleOscComplexTracktionPlugin (tracktionEngine_->getPluginManager());
            devices::registerFirstPartyEffectTracktionPlugin (tracktionEngine_->getPluginManager());
            edit_ = te::Edit::createSingleTrackEdit (*tracktionEngine_, te::Edit::EditRole::forEditing);

            if (edit_ == nullptr)
            {
                status_.message = "Tracktion edit creation failed";
                shutdown();
                return false;
            }

            status_.backendVersion = toStdString (te::Engine::getVersion());
            status_.initialized = true;
            status_.message = "Ready";
            updateDeviceStatus();
            return true;
        }
        catch (const std::exception& error)
        {
            status_.message = error.what();
            shutdown();
            return false;
        }
        catch (...)
        {
            status_.message = "Unknown Tracktion initialization error";
            shutdown();
            return false;
        }
    }

    void shutdown()
    {
        assertMessageThreadIfAvailable();

        livePluginParameterChangeListener_.detach();
        observedParameterRestoreTimer_.cancel();
        automationPlaybackTimer_.setRunning (false);
        automationProject_.reset();
        automationProjectHasLanes_ = false;
        preparedExpressionPlaybackModel_ = {};
        expressionProjectHasPlaybackRoutes_ = false;
        observedProjectPluginParameters_.clear();
        observedLoadedPluginParameters_.reset();
        lastKnownProjectPluginStates_.clear();
        protectObservedPluginParameterStateUntilMs_ = 0;

        if (edit_ != nullptr)
            stopTransportPreservingPluginState();

        detachMeterClients();
        hideProjectPluginWindowsForShutdown();
        unloadTestInstrument();
        projectInstruments_.clear();
        projectAudioTracksById_.clear();
        returnBusByTrackId_.clear();
        auxSendPluginsByRoute_.clear();
        projectNativeDevices_.clear();
        edit_.reset();
        tracktionEngine_.reset();

        status_.initialized = false;
        status_.playing = false;
        status_.audioDeviceType.clear();
        status_.audioDeviceName.clear();
        status_.sampleRate = 0.0;
        status_.blockSize = 0;

        if (status_.message.empty() || status_.message == "Ready" || status_.message == "Playing" || status_.message == "Stopped")
            status_.message = "Shut down";
    }

    std::vector<std::string> getAvailableAudioDevices() const
    {
        assertMessageThreadIfAvailable();

        std::vector<std::string> devices;

        for (const auto& device : getAvailableOutputDevices())
            devices.push_back (device.displayName);

        return devices;
    }

    std::vector<AudioOutputDevice> getAvailableOutputDevices() const
    {
        assertMessageThreadIfAvailable();

        std::vector<AudioOutputDevice> devices;

        if (tracktionEngine_ == nullptr)
            return devices;

        auto& audioDeviceManager = tracktionEngine_->getDeviceManager().deviceManager;

        for (auto* type : audioDeviceManager.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            type->scanForDevices();
            const auto outputNames = type->getDeviceNames (false);

            for (const auto& deviceName : outputNames)
            {
                const auto deviceType = toStdString (type->getTypeName());
                const auto name = toStdString (deviceName);

                devices.push_back (AudioOutputDevice {
                    deviceType,
                    name,
                    deviceType + ": " + name
                });
            }
        }

        return devices;
    }

    AudioDeviceSettings getAudioDeviceSettings() const
    {
        assertMessageThreadIfAvailable();

        AudioDeviceSettings settings;

        if (tracktionEngine_ == nullptr)
        {
            settings.message = status_.message;
            return settings;
        }

        auto& audioDeviceManager = tracktionEngine_->getDeviceManager().deviceManager;
        const auto setup = audioDeviceManager.getAudioDeviceSetup();

        settings.outputDeviceType = toStdString (audioDeviceManager.getCurrentAudioDeviceType());
        settings.outputDeviceName = toStdString (setup.outputDeviceName);
        settings.sampleRate = setup.sampleRate;
        settings.bufferSize = setup.bufferSize;
        settings.message = status_.message;

        if (auto* device = audioDeviceManager.getCurrentAudioDevice())
        {
            settings.hasOpenDevice = true;
            settings.outputDeviceType = toStdString (device->getTypeName());
            settings.outputDeviceName = toStdString (device->getName());
            settings.sampleRate = device->getCurrentSampleRate();
            settings.bufferSize = device->getCurrentBufferSizeSamples();
        }
        else if (settings.message.empty() || settings.message == "Ready")
        {
            settings.message = "No audio output device open";
        }

        return settings;
    }

    bool setOutputDevice (const AudioOutputDevice& outputDevice)
    {
        assertMessageThreadIfAvailable();

        if (outputDevice.deviceType.empty() || outputDevice.deviceName.empty())
        {
            status_.message = "No audio output device selected";
            return false;
        }

        if (! initialize())
            return false;

        try
        {
            auto& tracktionDeviceManager = tracktionEngine_->getDeviceManager();
            auto& audioDeviceManager = tracktionDeviceManager.deviceManager;
            const auto requestedDeviceType = juce::String::fromUTF8 (outputDevice.deviceType.c_str());
            const auto requestedDeviceName = juce::String::fromUTF8 (outputDevice.deviceName.c_str());

            if (audioDeviceManager.getCurrentAudioDeviceType() != requestedDeviceType)
                audioDeviceManager.setCurrentAudioDeviceType (requestedDeviceType, true);

            auto setup = audioDeviceManager.getAudioDeviceSetup();
            setup.outputDeviceName = requestedDeviceName;
            setup.inputDeviceName.clear();
            setup.useDefaultOutputChannels = true;
            setup.useDefaultInputChannels = true;

            const auto error = audioDeviceManager.setAudioDeviceSetup (setup, true);

            if (error.isNotEmpty())
            {
                status_.message = toStdString (error);
                updateDeviceStatus();
                return false;
            }

            tracktionDeviceManager.rescanWaveDeviceList();
            tracktionDeviceManager.checkDefaultDevicesAreValid();
            tracktionDeviceManager.dispatchPendingUpdates();

            status_.message = "Audio output selected";
            updateDeviceStatus();
            return true;
        }
        catch (const std::exception& error)
        {
            status_.message = error.what();
            updateDeviceStatus();
            return false;
        }
        catch (...)
        {
            status_.message = "Unknown audio output selection error";
            updateDeviceStatus();
            return false;
        }
    }

    std::string createAudioDeviceSettingsXml() const
    {
        assertMessageThreadIfAvailable();

        if (tracktionEngine_ == nullptr)
            return {};

        auto& audioDeviceManager = tracktionEngine_->getDeviceManager().deviceManager;

        if (auto settingsXml = audioDeviceManager.createStateXml())
            return toStdString (settingsXml->toString (juce::XmlElement::TextFormat {}.singleLine()));

        return {};
    }

    bool restoreAudioDeviceSettingsXml (const std::string& settingsXml)
    {
        assertMessageThreadIfAvailable();

        if (settingsXml.empty())
            return true;

        if (! initialize())
            return false;

        auto parsedSettings = juce::parseXML (juce::String::fromUTF8 (settingsXml.c_str()));

        if (parsedSettings == nullptr)
        {
            status_.message = "Saved audio device settings are invalid";
            updateDeviceStatus();
            return false;
        }

        try
        {
            auto& tracktionDeviceManager = tracktionEngine_->getDeviceManager();
            auto& audioDeviceManager = tracktionDeviceManager.deviceManager;
            const auto error = audioDeviceManager.initialise (0,
                                                              te::DeviceManager::defaultNumChannelsToOpen,
                                                              parsedSettings.get(),
                                                              true);

            if (error.isNotEmpty())
            {
                status_.message = toStdString (error);
                updateDeviceStatus();
                return false;
            }

            tracktionDeviceManager.rescanWaveDeviceList();
            tracktionDeviceManager.checkDefaultDevicesAreValid();
            tracktionDeviceManager.dispatchPendingUpdates();

            status_.message = "Ready";
            updateDeviceStatus();
            return true;
        }
        catch (const std::exception& error)
        {
            status_.message = error.what();
            updateDeviceStatus();
            return false;
        }
        catch (...)
        {
            status_.message = "Unknown audio device settings restore error";
            updateDeviceStatus();
            return false;
        }
    }

    PlaybackEngineStatus getCurrentStatus() const
    {
        assertMessageThreadIfAvailable();

        auto status = status_;
        status.playing = isPlaying();

        if (tracktionEngine_ != nullptr)
            populateDeviceStatus (status);

        return status;
    }

    MeterSnapshot getMeterSnapshot()
    {
        assertMessageThreadIfAvailable();

        MeterSnapshot snapshot;
        snapshot.playing = isPlaying();
        snapshot.sequence = ++meterSnapshotSequence_;
        snapshot.sources.reserve (meterClients_.size());

        if (! snapshot.playing)
            resetMeterClients();

        for (auto& binding : meterClients_)
        {
            if (binding == nullptr)
                continue;

            MeterSourceSnapshot source;
            source.sourceId = binding->sourceId;
            source.trackId = binding->trackId;
            source.displayName = binding->displayName;
            source.master = binding->master;
            source.returnTrack = binding->returnTrack;
            source.active = binding->active;

            const auto usedChannels = snapshot.playing ? binding->client.getNumChannelsUsed() : binding->expectedChannels;
            const auto channelCount = std::clamp (usedChannels <= 0 ? binding->expectedChannels : usedChannels, 1, 2);
            source.channels.reserve (static_cast<std::size_t> (channelCount));

            for (auto channel = 0; channel < channelCount; ++channel)
            {
                MeterChannelSnapshot channelSnapshot;
                if (snapshot.playing)
                {
                    const auto peak = binding->client.getAndClearAudioLevel (channel);
                    channelSnapshot.peakDb = peak.dB;
                    channelSnapshot.peakLinear = meterLinearFromDb (peak.dB);
                }

                source.channels.push_back (channelSnapshot);
            }

            snapshot.sources.push_back (std::move (source));
        }

        return snapshot;
    }

    bool syncProject (const core::sequencing::Project& project)
    {
        core::diagnostics::ScopedPerformanceTimer syncTimer { "TracktionPlaybackEngine::syncProject" };

        assertMessageThreadIfAvailable();

        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject initialize" };
            if (! initialize())
                return false;
        }

        try
        {
            auto canUseInPlaceSync = false;
            if (! forceFullProjectSync_)
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::canSyncProjectInPlace" };
                canUseInPlaceSync = canSyncProjectInPlace (project);
            }

            if (canUseInPlaceSync)
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject in-place" };
                syncProjectInPlace (project);
                finishProjectSync (project, "Project synced in place", false, false);
                return true;
            }

            // Non-realtime preparation boundary: project traversal, plugin
            // assignment, and MIDI event creation happen here before Tracktion
            // starts rendering the edit.
            TrackPluginStateBlocks livePluginStates;
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject capture live plugin states" };
                livePluginStates = forceFullProjectSync_ ? TrackPluginStateBlocks {}
                                                         : captureLiveProjectPluginStateBlocks();
            }
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject reset edit graph" };
                stopTransportPreservingPluginState();
                detachMeterClients();

                hideProjectPluginWindowsForShutdown();
                unloadTestInstrument();
                projectInstruments_.clear();
                projectAudioTracksById_.clear();
                returnBusByTrackId_.clear();
                auxSendPluginsByRoute_.clear();
                projectNativeDevices_.clear();
                edit_ = te::Edit::createSingleTrackEdit (*tracktionEngine_, te::Edit::EditRole::forEditing);

                if (edit_ == nullptr)
                {
                    status_.message = "Tracktion edit creation failed";
                    return false;
                }
            }

            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject rebuild edit graph" };
                te::TransportControl::ReallocationInhibitor reallocationInhibitor { edit_->getTransport() };
                {
                    core::diagnostics::ScopedPerformanceTimer phaseTimer { "TracktionPlaybackEngine::syncProject configure tempo sequence" };
                    configureTempoSequence (project);
                }
                {
                    core::diagnostics::ScopedPerformanceTimer phaseTimer { "TracktionPlaybackEngine::syncProject configure time signatures" };
                    configureTimeSignatureSequence (project);
                }
                {
                    core::diagnostics::ScopedPerformanceTimer phaseTimer { "TracktionPlaybackEngine::syncProject ensure audio tracks" };
                    edit_->ensureNumberOfAudioTracks (playbackAudioTrackCount (project));
                }
                auto audioTracks = te::getAudioTracks (*edit_);
                {
                    core::diagnostics::ScopedPerformanceTimer phaseTimer { "TracktionPlaybackEngine::syncProject rebuild track lookup" };
                    rebuildTrackLookup (project, audioTracks);
                }

                auto audioTrackIndex = 0;
                for (const auto& projectTrack : project.tracks())
                {
                    if (projectTrack.type() == core::sequencing::TrackType::master)
                        continue;

                    if (audioTrackIndex >= audioTracks.size())
                        break;

                    auto* audioTrack = audioTracks[audioTrackIndex++];
                    if (audioTrack == nullptr)
                        continue;

                    if (! configureProjectTrack (*audioTrack, projectTrack, project, livePluginStates))
                        return false;
                }

                configureMasterTrack (project, livePluginStates);
                prepareExpressionPlaybackRoutes (project);
                applyPreparedExpressionPitchEventsToNativeDevices (project);
                applyPreparedExpressionRoutesToNativeDevices (preparedExpressionPlaybackModel_);
                rebuildPreparedExpressionMixerAutomationCurves (project);
            }

            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject dispatch edit updates" };
                dispatchPendingTracktionEditUpdates();
            }
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject restore live plugin states" };
                restoreLiveProjectPluginStateBlocks (livePluginStates);
            }
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProject finish sync" };
                finishProjectSync (project, "Project synced with full edit rebuild", true, true);
            }
            return true;
        }
        catch (const std::exception& error)
        {
            status_.message = error.what();
            testInstrumentStatus_.message = error.what();
            updateDeviceStatus();
            return false;
        }
        catch (...)
        {
            status_.message = "Unknown project playback sync error";
            testInstrumentStatus_.message = status_.message;
            updateDeviceStatus();
            return false;
        }
    }

    bool startPlayback()
    {
        assertMessageThreadIfAvailable();

        if (! initialize())
            return false;

        if (edit_ == nullptr)
        {
            status_.message = "No Tracktion edit is available";
            return false;
        }

        try
        {
            const auto livePluginStates = captureLiveProjectPluginStateBlocks();
            armNativeSimpleOscExpressionChase();
            edit_->getTransport().play (false);
            restoreLiveProjectPluginStateBlocks (livePluginStates);
            status_.playing = edit_->getTransport().isPlaying();
            applyAutomationAt (getPlayheadPosition());
            automationPlaybackTimer_.setRunning (status_.playing && (automationProjectHasLanes_ || expressionProjectHasPlaybackRoutes_));
            status_.message = status_.playing ? "Playing" : "Start requested";
            updateDeviceStatus();
            return true;
        }
        catch (const std::exception& error)
        {
            status_.message = error.what();
            return false;
        }
        catch (...)
        {
            status_.message = "Unknown Tracktion playback start error";
            return false;
        }
    }

    void stopPlayback()
    {
        assertMessageThreadIfAvailable();

        if (edit_ == nullptr)
            return;

        stopTransportPreservingPluginState();
        automationPlaybackTimer_.setRunning (false);
        resetMeterClients();
        status_.playing = false;
        status_.message = "Stopped";
        updateDeviceStatus();
    }

    bool isPlaying() const
    {
        return edit_ != nullptr && edit_->getTransport().isPlaying();
    }

    core::time::TickPosition getPlayheadPosition() const
    {
        assertMessageThreadIfAvailable();

        if (edit_ == nullptr)
            return playheadTick_;

        const auto position = toTickPosition (edit_->tempoSequence.toBeats (edit_->getTransport().getPosition()));
        playheadTick_ = position;
        return playheadTick_;
    }

    bool setPlayheadPosition (core::time::TickPosition position)
    {
        assertMessageThreadIfAvailable();

        playheadTick_ = position;

        if (edit_ == nullptr && ! initialize())
            return false;

        setTransportPosition (position);
        applyAutomationAt (position);
        status_.message = "Playhead moved";
        updateDeviceStatus();
        return true;
    }

    bool returnToStart()
    {
        assertMessageThreadIfAvailable();

        if (! setPlayheadPosition (core::time::TickPosition {}))
            return false;

        if (edit_ != nullptr)
            edit_->getTransport().startPosition = tracktion::TimePosition();

        status_.message = "Returned to start";
        updateDeviceStatus();
        return true;
    }

    bool setLoopEnabled (bool enabled)
    {
        assertMessageThreadIfAvailable();

        loopEnabled_ = enabled;
        if (edit_ != nullptr)
        {
            applyTransportLoopRange();
            status_.message = loopEnabled_ ? "Loop enabled" : "Loop disabled";
            updateDeviceStatus();
        }

        return true;
    }

    bool isLoopEnabled() const noexcept
    {
        return loopEnabled_;
    }

    bool loadTestInstrument (const plugins::PluginDescription& plugin)
    {
        assertMessageThreadIfAvailable();

        if (! plugin.isInstrument)
        {
            testInstrumentStatus_.message = "Selected plugin is not marked as a VST3 instrument";
            return false;
        }

        if (! initialize())
        {
            testInstrumentStatus_.message = status_.message;
            return false;
        }

        if (edit_ == nullptr || tracktionEngine_ == nullptr)
        {
            testInstrumentStatus_.message = "Playback edit is not available";
            return false;
        }

        try
        {
            auto* track = getFirstAudioTrack();

            if (track == nullptr)
            {
                testInstrumentStatus_.message = "No audio track is available for the test instrument";
                return false;
            }

            stopTransportPreservingPluginState();
            unloadTestInstrument();

            const auto resolvedDescription = resolvePluginDescription (plugin);

            if (! resolvedDescription.has_value())
                return false;

            const auto juceDescription = *resolvedDescription;
            tracktionEngine_->getPluginManager().knownPluginList.addType (juceDescription);

            auto createdPlugin = edit_->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName,
                                                                          juceDescription);
            auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (createdPlugin.get());

            if (externalPlugin == nullptr)
            {
                testInstrumentStatus_.message = "Tracktion could not create an external VST3 plugin";
                return false;
            }

            track->pluginList.insertPlugin (createdPlugin, 0, nullptr);
            loadedInstrument_ = externalPlugin;

            if (! validateLoadedInstrument())
                return false;

            if (! createOrReplaceTestPhraseClip())
                return false;

            testInstrumentStatus_.pluginLoaded = true;
            testInstrumentStatus_.phraseReady = true;
            testInstrumentStatus_.pluginName = plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name;
            testInstrumentStatus_.pluginIdentifier = plugin.uniqueIdentifier;
            testInstrumentStatus_.message = "Loaded test instrument";
            refreshPluginEditorSupport();
            refreshLivePluginParameterListeners();
            status_.message = "Test instrument loaded";
            updateDeviceStatus();
            return true;
        }
        catch (const std::exception& error)
        {
            testInstrumentStatus_.message = error.what();
            status_.message = error.what();
            updateDeviceStatus();
            return false;
        }
        catch (...)
        {
            testInstrumentStatus_.message = "Unknown VST3 instrument load error";
            status_.message = testInstrumentStatus_.message;
            updateDeviceStatus();
            return false;
        }
    }

    bool playTestPhrase()
    {
        assertMessageThreadIfAvailable();

        if (! initialize())
        {
            testInstrumentStatus_.message = status_.message;
            return false;
        }

        if (edit_ == nullptr || loadedInstrument_ == nullptr || testPhraseClip_ == nullptr)
        {
            testInstrumentStatus_.message = "Load a VST3 instrument before playing the test phrase";
            return false;
        }

        if (! validateLoadedInstrument())
            return false;

        try
        {
            auto& transport = edit_->getTransport();
            const auto phraseRange = testPhraseClip_->getPosition().time;

            transport.stop (false, false);
            transport.setPosition (tracktion::TimePosition());
            transport.setLoopRange (phraseRange);
            transport.looping = true;
            transport.play (false);

            status_.playing = transport.isPlaying();
            status_.message = status_.playing ? "Playing test phrase" : "Test phrase start requested";
            testInstrumentStatus_.message = status_.message;
            updateDeviceStatus();
            return true;
        }
        catch (const std::exception& error)
        {
            testInstrumentStatus_.message = error.what();
            status_.message = error.what();
            updateDeviceStatus();
            return false;
        }
        catch (...)
        {
            testInstrumentStatus_.message = "Unknown test phrase playback error";
            status_.message = testInstrumentStatus_.message;
            updateDeviceStatus();
            return false;
        }
    }

    void stopTestPhrase()
    {
        assertMessageThreadIfAvailable();

        if (edit_ == nullptr)
            return;

        stopTransportPreservingPluginState();
        status_.playing = false;
        status_.message = "Stopped";

        if (testInstrumentStatus_.pluginLoaded)
            testInstrumentStatus_.message = "Stopped test phrase";

        updateDeviceStatus();
    }

    bool openLoadedPluginEditor()
    {
        assertMessageThreadIfAvailable();

        if (loadedInstrument_ == nullptr)
        {
            testInstrumentStatus_.message = "No test instrument is loaded";
            return false;
        }

        refreshPluginEditorSupport();

        if (! testInstrumentStatus_.pluginEditorSupported)
        {
            testInstrumentStatus_.message = "Loaded plugin does not report an editor";
            return false;
        }

        loadedInstrument_->showWindowExplicitly();
        refreshLivePluginParameterListeners();
        observeLivePluginParameterStateNow();
        testInstrumentStatus_.message = "Plugin editor opened";
        return true;
    }

    bool openTrackPluginEditor (const std::string& trackId, const std::string& slotId)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::openTrackPluginEditor" };

        assertMessageThreadIfAvailable();

        if (trackId.empty() || slotId.empty())
        {
            status_.message = "No device selected";
            updateDeviceStatus();
            return false;
        }

        if (edit_ == nullptr)
        {
            status_.message = "Project playback is not synced";
            updateDeviceStatus();
            return false;
        }

        const auto device = std::find_if (projectInstruments_.begin(),
                                          projectInstruments_.end(),
                                          [&trackId, &slotId] (const auto& projectPlugin)
                                          {
                                              return projectPlugin.trackId == trackId
                                                  && projectPlugin.slotId.value == slotId;
                                          });

        if (device == projectInstruments_.end() || device->plugin == nullptr)
        {
            status_.message = "Device plugin is not loaded";
            updateDeviceStatus();
            return false;
        }

        const auto loadError = device->plugin->getLoadError();
        if (loadError.isNotEmpty())
        {
            status_.message = toStdString (loadError);
            updateDeviceStatus();
            return false;
        }

        auto* instance = device->plugin->getAudioPluginInstance();
        if (instance == nullptr)
        {
            status_.message = "Device plugin instance is not ready";
            updateDeviceStatus();
            return false;
        }

        if (! instance->hasEditor())
        {
            status_.message = "Device plugin does not report an editor";
            updateDeviceStatus();
            return false;
        }

        device->plugin->showWindowExplicitly();
        refreshLivePluginParameterListeners();
        observeLivePluginParameterStateNow();
        status_.message = "Plugin editor opened";
        updateDeviceStatus();
        return true;
    }

    bool setTrackPluginBypassed (const std::string& trackId, const std::string& slotId, bool bypassed)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::setTrackPluginBypassed" };

        assertMessageThreadIfAvailable();

        if (trackId.empty() || slotId.empty())
        {
            status_.message = "No device selected";
            updateDeviceStatus();
            return false;
        }

        const auto device = std::find_if (projectInstruments_.begin(),
                                          projectInstruments_.end(),
                                          [&trackId, &slotId] (const auto& projectPlugin)
                                          {
                                              return projectPlugin.trackId == trackId
                                                  && projectPlugin.slotId.value == slotId;
                                          });

        if (device == projectInstruments_.end() || device->plugin == nullptr)
        {
            const auto nativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                    projectNativeDevices_.end(),
                                                    [&trackId, &slotId] (const auto& projectPlugin)
                                                    {
                                                        return projectPlugin.trackId == trackId
                                                            && projectPlugin.slotId.value == slotId;
                                                    });

            if (nativeDevice == projectNativeDevices_.end() || nativeDevice->plugin == nullptr)
            {
                status_.message = "Device plugin is not loaded";
                updateDeviceStatus();
                return false;
            }

            nativeDevice->plugin->setEnabled (! bypassed);
            status_.message = bypassed ? "Device bypassed" : "Device enabled";
            updateDeviceStatus();
            return true;
        }

        device->plugin->setEnabled (! bypassed);
        status_.message = bypassed ? "Device bypassed" : "Device enabled";
        updateDeviceStatus();
        return true;
    }

    TestInstrumentStatus getTestInstrumentStatus() const
    {
        assertMessageThreadIfAvailable();

        return testInstrumentStatus_;
    }

    void setProjectPluginStateDirectory (std::filesystem::path packagePath)
    {
        assertMessageThreadIfAvailable();

        projectPluginStateDirectory_ = std::move (packagePath);
        lastKnownProjectPluginStates_.clear();
        forceFullProjectSync_ = true;
    }

    std::vector<TrackPluginState> captureTrackPluginStates()
    {
        assertMessageThreadIfAvailable();

        std::vector<TrackPluginState> states;

        for (const auto& [trackId, capturedState] : captureLiveProjectPluginStateBlocks())
            if (capturedState.stateBlock.getSize() > 0)
                states.push_back (TrackPluginState { trackId, memoryBlockToBytes (capturedState.stateBlock) });

        return states;
    }

    void observeLivePluginParameterState()
    {
        assertMessageThreadIfAvailable();

        const auto now = juce::Time::getMillisecondCounter();
        if (suppressObservedPluginParameterStateUntilMs_ != 0u
            && static_cast<std::int32_t> (now - suppressObservedPluginParameterStateUntilMs_) < 0)
            return;

        if (lastObservedPluginParameterStateMs_ != 0u
            && static_cast<std::uint32_t> (now - lastObservedPluginParameterStateMs_) < 1000u)
            return;

        lastObservedPluginParameterStateMs_ = now;
        observeLivePluginParameterStateNow();
    }

    void restoreObservedPluginParameterStateSoon()
    {
        assertMessageThreadIfAvailable();

        observeLivePluginParameterState();
        const auto now = juce::Time::getMillisecondCounter();
        suppressObservedPluginParameterStateUntilMs_ = now + 1000u;
        protectObservedPluginParameterStateUntilMs_ = now + 15000u;
        observedParameterRestoreTimer_.schedule();
    }

    void observeLivePluginParameterStateFromParameterChange()
    {
        assertMessageThreadIfAvailable();
        observeLivePluginParameterStateNow();
    }

    std::vector<PluginParameterDebugValue> debugLoadedPluginParameters() const
    {
        assertMessageThreadIfAvailable();

        std::vector<PluginParameterDebugValue> values;
        if (loadedInstrument_ == nullptr)
            return values;

        return debugPluginParameters (*loadedInstrument_);
    }

    bool debugSetLoadedPluginParameterValue (int parameterIndex, float normalizedValue)
    {
        assertMessageThreadIfAvailable();

        if (loadedInstrument_ == nullptr)
            return false;

        auto* instance = loadedInstrument_->getAudioPluginInstance();
        if (instance == nullptr)
            return false;

        auto& parameters = instance->getParameters();
        if (parameterIndex < 0 || parameterIndex >= parameters.size())
            return false;

        auto* parameter = parameters[parameterIndex];
        if (parameter == nullptr)
            return false;

        const auto clampedValue = juce::jlimit (0.0f, 1.0f, normalizedValue);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (clampedValue);
        parameter->endChangeGesture();
        return true;
    }

    std::optional<float> debugTrackVolumeDb (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        if (edit_ == nullptr)
            return std::nullopt;

        auto* volumePlugin = volumePluginForProjectTrackId (trackId);
        if (volumePlugin == nullptr)
            return std::nullopt;

        return volumePlugin->getVolumeDb();
    }

    std::optional<float> debugTrackPan (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        if (edit_ == nullptr)
            return std::nullopt;

        auto* volumePlugin = volumePluginForProjectTrackId (trackId);
        if (volumePlugin == nullptr)
            return std::nullopt;

        return volumePlugin->getPan();
    }

    std::size_t debugTrackVolumeAutomationPointCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        auto* volumePlugin = volumePluginForProjectTrackId (trackId);
        if (volumePlugin == nullptr || volumePlugin->volParam == nullptr)
            return 0;

        return static_cast<std::size_t> (volumePlugin->volParam->getCurve().getNumPoints());
    }

    std::optional<float> debugSendGainDb (const std::string& trackId, const std::string& returnTrackId) const
    {
        assertMessageThreadIfAvailable();

        const auto send = auxSendPluginsByRoute_.find (automationSendRouteKey (trackId, returnTrackId));
        if (send == auxSendPluginsByRoute_.end() || send->second == nullptr)
            return std::nullopt;

        return send->second->getGainDb();
    }

    std::vector<std::uint64_t> debugNativeSimpleOscExpressionNoteOnEventIds (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugExpressionNoteOnEventIds();
        }

        return {};
    }

    std::size_t debugNativeSimpleOscExpressionSlurEventCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->expressionSlurEventCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscLegatoSlurEventCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->expressionLegatoSlurEventCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscActiveVoiceCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugActiveVoiceCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscMidiNoteOnCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugMidiNoteOnCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscRenderCallbackCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugRenderCallbackCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscRenderCallbackWithMidiCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugRenderCallbackWithMidiCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscRenderCallbackPlayingCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugRenderCallbackPlayingCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscExpressionSlurFallbackCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugExpressionSlurFallbackCount();
        }

        return 0;
    }

    float debugNativeSimpleOscMaxOutputPeak (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugMaxOutputPeak();
        }

        return 0.0f;
    }

    float debugNativeSimpleOscLastOutputPeak (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugLastOutputPeak();
        }

        return 0.0f;
    }

    std::vector<std::size_t> debugNativeSimpleOscEventCounters (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugEventCounters();
        }

        return {};
    }

    std::pair<double, double> debugNativeSimpleOscLastRenderTimeRange (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return { simpleOsc->debugLastRenderStartSeconds(), simpleOsc->debugLastRenderEndSeconds() };
        }

        return {};
    }

    std::size_t debugTracktionMidiNoteCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        auto* audioTrack = tracktionTrackForProjectTrackId (trackId);
        if (audioTrack == nullptr)
            return 0;

        auto noteCount = std::size_t {};
        for (auto* clip : audioTrack->getClips())
        {
            if (auto* midiClip = dynamic_cast<te::MidiClip*> (clip))
                noteCount += static_cast<std::size_t> (midiClip->getSequence().getNumNotes());
        }

        return noteCount;
    }

    std::size_t debugNativeSimpleOscExpressionModulationStreamCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->expressionModulationStreamCount();
        }

        return 0;
    }

    std::size_t debugNativeSimpleOscPatchStateRefreshCount (const std::string& trackId) const
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (const auto* simpleOsc = dynamic_cast<const devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                return simpleOsc->debugPatchStateRefreshCount();
        }

        return 0;
    }

    bool debugChaseNativeSimpleOscAtPlayhead (const std::string& trackId)
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            if (nativeDevice.trackId != trackId || nativeDevice.plugin == nullptr)
                continue;

            if (auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
            {
                simpleOsc->debugChaseExpressionPlaybackAt (tickPositionToEditSeconds (getPlayheadPosition()));
                return true;
            }
        }

        return false;
    }

    void armNativeSimpleOscExpressionChase()
    {
        assertMessageThreadIfAvailable();

        for (const auto& nativeDevice : projectNativeDevices_)
            if (auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                simpleOsc->armExpressionPlaybackChase();
    }

    std::vector<std::string> debugPluginParameterStateLines (std::string_view label) const
    {
        assertMessageThreadIfAvailable();

        std::vector<std::string> lines;
        const auto labelText = std::string { label };
        if (! labelText.empty())
            lines.push_back ("plugin-state event: " + labelText);

        if (projectInstruments_.empty() && loadedInstrument_ == nullptr)
        {
            lines.push_back ("plugin-state: no loaded/project instrument");
            return lines;
        }

        for (const auto& projectInstrument : projectInstruments_)
        {
            if (projectInstrument.plugin == nullptr)
                continue;

            appendDebugPluginParameterStateLines (lines,
                                                  "project track " + projectInstrument.trackId,
                                                  *projectInstrument.plugin);
        }

        const auto loadedAlreadyListed = std::any_of (projectInstruments_.begin(),
                                                      projectInstruments_.end(),
                                                      [this] (const auto& projectInstrument)
                                                      {
                                                          return projectInstrument.plugin == loadedInstrument_;
                                                      });
        if (! loadedAlreadyListed && loadedInstrument_ != nullptr)
            appendDebugPluginParameterStateLines (lines, "loaded test instrument", *loadedInstrument_);

        return lines;
    }

private:
    std::vector<PluginParameterDebugValue> debugPluginParameters (te::ExternalPlugin& externalPlugin) const
    {
        std::vector<PluginParameterDebugValue> values;
        auto* instance = externalPlugin.getAudioPluginInstance();
        if (instance == nullptr)
            return values;

        const auto& parameters = instance->getParameters();
        values.reserve (static_cast<std::size_t> (parameters.size()));
        for (int index = 0; index < parameters.size(); ++index)
        {
            auto* parameter = parameters[index];
            if (parameter == nullptr)
                continue;

            std::string parameterId;
            if (auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                parameterId = toStdString (parameterWithId->paramID);

            values.push_back (PluginParameterDebugValue {
                index,
                std::move (parameterId),
                toStdString (parameter->getName (128)),
                juce::jlimit (0.0f, 1.0f, parameter->getValue()),
                juce::jlimit (0.0f, 1.0f, parameter->getDefaultValue())
            });
        }

        return values;
    }

    static std::string formatNormalizedValue (float value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision (4) << value;
        return stream.str();
    }

    static bool isWatchedDiagnosticParameter (const PluginParameterDebugValue& value)
    {
        return value.parameterId == "osc1ModAmount"
            || value.parameterId == "osc1WavefoldAmount"
            || value.parameterId == "osc1CarrierWave"
            || value.name == "Osc1 Mod Amount"
            || value.name == "Osc1 Wavefold Amount"
            || value.name == "Osc1 Carrier Wave";
    }

    static std::string describeDebugParameter (const PluginParameterDebugValue& value)
    {
        std::string description = value.name.empty() ? std::string { "parameter" } : value.name;
        if (! value.parameterId.empty())
            description += " id=" + value.parameterId;

        description += " idx=" + std::to_string (value.index);
        description += " value=" + formatNormalizedValue (value.value);
        description += " default=" + formatNormalizedValue (value.defaultValue);
        return description;
    }

    void appendDebugPluginParameterStateLines (std::vector<std::string>& lines,
                                               const std::string& prefix,
                                               te::ExternalPlugin& externalPlugin) const
    {
        const auto parameters = debugPluginParameters (externalPlugin);
        if (parameters.empty())
        {
            lines.push_back ("plugin-state " + prefix + ": no hosted parameters");
            return;
        }

        auto nonDefaultCount = 0;
        std::vector<std::string> watched;
        std::vector<std::string> firstNonDefault;

        for (const auto& parameter : parameters)
        {
            const auto edited = std::abs (parameter.value - parameter.defaultValue) > 0.0001f;
            if (edited)
            {
                ++nonDefaultCount;
                if (firstNonDefault.size() < 8)
                    firstNonDefault.push_back (describeDebugParameter (parameter));
            }

            if (isWatchedDiagnosticParameter (parameter))
                watched.push_back (describeDebugParameter (parameter));
        }

        std::string summary = "plugin-state " + prefix + ": ";
        summary += toStdString (externalPlugin.getName());
        summary += " params=" + std::to_string (parameters.size());
        summary += " nonDefault=" + std::to_string (nonDefaultCount);
        lines.push_back (summary);

        for (const auto& parameter : watched)
            lines.push_back ("plugin-state " + prefix + " watched: " + parameter);

        for (const auto& parameter : firstNonDefault)
            lines.push_back ("plugin-state " + prefix + " non-default: " + parameter);
    }

    struct CapturedTrackPluginState
    {
        core::sequencing::PluginReference pluginReference;
        juce::MemoryBlock stateBlock;
        std::vector<std::pair<int, float>> parameterValues;
    };

    using TrackPluginStateBlocks = std::unordered_map<std::string, CapturedTrackPluginState>;

    struct ProjectInstrumentPlugin
    {
        std::string trackId;
        core::sequencing::DeviceSlotId slotId;
        core::sequencing::PluginKind kind = core::sequencing::PluginKind::unknown;
        core::sequencing::PluginReference pluginReference;
        te::ExternalPlugin::Ptr plugin;
    };

    struct ProjectNativeDevicePlugin
    {
        std::string trackId;
        core::sequencing::DeviceSlotId slotId;
        core::sequencing::PluginKind kind = core::sequencing::PluginKind::unknown;
        std::string typeId;
        te::Plugin::Ptr plugin;
    };

    struct MeterClientBinding
    {
        std::string sourceId;
        std::string trackId;
        std::string displayName;
        bool master = false;
        bool returnTrack = false;
        bool active = true;
        int expectedChannels = 2;
        te::LevelMeterPlugin* meterPlugin = nullptr;
        te::LevelMeasurer::Client client;
    };

    struct CapturedPluginParameterSnapshot
    {
        std::vector<std::pair<int, float>> values;
        std::vector<std::pair<int, float>> defaultValues;
        int nonDefaultCount = 0;
    };

    struct ObservedTrackPluginParameterState
    {
        core::sequencing::PluginReference pluginReference;
        CapturedPluginParameterSnapshot snapshot;
    };

    using ObservedTrackPluginParameterStates = std::unordered_map<std::string, ObservedTrackPluginParameterState>;

    class DeferredObservedParameterRestoreTimer final : private juce::Timer
    {
    public:
        explicit DeferredObservedParameterRestoreTimer (Impl& owner)
            : owner_ (owner)
        {
        }

        void schedule()
        {
            remainingCallbacks_ = 4;

            const auto now = juce::Time::getMillisecondCounter();
            if (lastImmediateRestoreMs_ == 0u
                || static_cast<std::uint32_t> (now - lastImmediateRestoreMs_) >= 250u)
            {
                lastImmediateRestoreMs_ = now;
                owner_.restoreObservedPluginParameterStateNow();
            }

            startTimer (250);
        }

        void cancel()
        {
            remainingCallbacks_ = 0;
            lastImmediateRestoreMs_ = 0;
            stopTimer();
        }

    private:
        void timerCallback() override
        {
            owner_.restoreObservedPluginParameterStateNow();

            if (--remainingCallbacks_ <= 0)
                stopTimer();
        }

        Impl& owner_;
        int remainingCallbacks_ = 0;
        std::uint32_t lastImmediateRestoreMs_ = 0;
    };

    class AutomationPlaybackTimer final : private juce::Timer
    {
    public:
        explicit AutomationPlaybackTimer (Impl& owner)
            : owner_ (owner)
        {
        }

        void setRunning (bool running)
        {
            if (running)
                startTimerHz (30);
            else
                stopTimer();
        }

    private:
        void timerCallback() override
        {
            owner_.applyAutomationAt (owner_.getPlayheadPosition());
        }

        Impl& owner_;
    };

    class LivePluginParameterChangeListener final : private juce::AudioProcessorParameter::Listener,
                                                    private juce::AsyncUpdater
    {
    public:
        explicit LivePluginParameterChangeListener (Impl& owner)
            : owner_ (owner)
        {
        }

        ~LivePluginParameterChangeListener() override
        {
            detach();
        }

        void attachTo (const std::vector<ProjectInstrumentPlugin>& projectInstruments,
                       const te::ExternalPlugin::Ptr& loadedInstrument)
        {
            detach();

            for (const auto& projectInstrument : projectInstruments)
                if (projectInstrument.plugin != nullptr)
                    attachTo (*projectInstrument.plugin);

            const auto loadedAlreadyAttached = std::any_of (projectInstruments.begin(),
                                                            projectInstruments.end(),
                                                            [&loadedInstrument] (const auto& projectInstrument)
                                                            {
                                                                return projectInstrument.plugin == loadedInstrument;
                                                            });

            if (! loadedAlreadyAttached && loadedInstrument != nullptr)
                attachTo (*loadedInstrument);
        }

        void detach()
        {
            cancelPendingUpdate();

            for (auto* parameter : parameters_)
                if (parameter != nullptr)
                    parameter->removeListener (this);

            parameters_.clear();
        }

    private:
        void attachTo (te::ExternalPlugin& plugin)
        {
            auto* instance = plugin.getAudioPluginInstance();
            if (instance == nullptr)
                return;

            for (auto* parameter : instance->getParameters())
            {
                if (parameter == nullptr)
                    continue;

                parameter->addListener (this);
                parameters_.push_back (parameter);
            }
        }

        void parameterValueChanged (int, float) override
        {
            triggerAsyncUpdate();
        }

        void parameterGestureChanged (int, bool) override
        {
        }

        void handleAsyncUpdate() override
        {
            owner_.observeLivePluginParameterStateFromParameterChange();
        }

        Impl& owner_;
        std::vector<juce::AudioProcessorParameter*> parameters_;
    };

    std::optional<juce::PluginDescription> resolvePluginDescription (const plugins::PluginDescription& plugin)
    {
        auto description = toJuceDescription (plugin);

        if (description.uniqueId != 0 || description.deprecatedUid != 0)
            return description;

        if (plugin.fileOrIdentifier.empty())
            return description;

#if JUCE_PLUGINHOST_VST3
        try
        {
            juce::VST3PluginFormat format;
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format.findAllTypesForFile (descriptions, toJuceString (plugin.fileOrIdentifier));

            if (descriptions.isEmpty())
            {
                testInstrumentStatus_.message = "No VST3 plugin description found in selected file";
                return {};
            }

            for (auto* candidate : descriptions)
            {
                if (candidate == nullptr)
                    continue;

                const auto nameMatches = candidate->name == description.name;
                const auto makerMatches = description.manufacturerName.isEmpty()
                                          || candidate->manufacturerName == description.manufacturerName;

                if (nameMatches && makerMatches)
                    return *candidate;
            }

            if (auto* firstDescription = descriptions.getFirst())
                return *firstDescription;

            testInstrumentStatus_.message = "No usable VST3 plugin description found in selected file";
            return {};
        }
        catch (const std::exception& error)
        {
            testInstrumentStatus_.message = error.what();
            return {};
        }
        catch (...)
        {
            testInstrumentStatus_.message = "Unknown VST3 description lookup error";
            return {};
        }
#else
        testInstrumentStatus_.message = "VST3 hosting is not enabled in this build";
        return {};
#endif
    }

    te::AudioTrack* getFirstAudioTrack()
    {
        if (edit_ == nullptr)
            return nullptr;

        edit_->ensureNumberOfAudioTracks (1);

        auto audioTracks = te::getAudioTracks (*edit_);
        return audioTracks.isEmpty() ? nullptr : audioTracks.getFirst();
    }

    void configureTempoSequence (const core::sequencing::Project& project)
    {
        auto& sequence = edit_->tempoSequence;

        for (auto index = sequence.getNumTempos() - 1; index > 0; --index)
            sequence.removeTempo (index, false);

        const auto& nodes = project.tempoMap().nodes();
        if (nodes.empty())
            return;

        if (auto* tempo = sequence.getTempo (0))
            tempo->setBpm (nodes.front().tempo.bpm());

        for (std::size_t index = 1; index < nodes.size(); ++index)
            sequence.insertTempo (toBeatPosition (nodes[index].position), nodes[index].tempo.bpm(), 0.0f);
    }

    void configureTimeSignatureSequence (const core::sequencing::Project& project)
    {
        auto& sequence = edit_->tempoSequence;

        for (auto index = sequence.getNumTimeSigs() - 1; index > 0; --index)
            sequence.removeTimeSig (index);

        const auto& markers = project.timeSignatureMap().markers();
        if (markers.empty())
            return;

        const auto setTimeSignature = [] (te::TimeSigSetting* setting, const core::time::TimeSignature& timeSignature)
        {
            if (setting != nullptr)
            {
                setting->setStringTimeSig (
                    juce::String (timeSignature.numerator()) + "/" + juce::String (timeSignature.denominator()));
            }
        };

        setTimeSignature (sequence.getTimeSig (0), markers.front().timeSignature);

        for (std::size_t index = 1; index < markers.size(); ++index)
            setTimeSignature (sequence.insertTimeSig (toBeatPosition (markers[index].position)).get(), markers[index].timeSignature);
    }

    bool canSyncProjectInPlace (const core::sequencing::Project& project) const
    {
        if (edit_ == nullptr)
            return false;

        const auto audioTracks = te::getAudioTracks (*edit_);
        const auto requiredAudioTrackCount = playbackAudioTrackCount (project);
        if (audioTracks.size() != requiredAudioTrackCount)
            return false;

        std::vector<std::string> expectedExternalKeys;
        std::vector<std::string> expectedNativeKeys;
        for (const auto& projectTrack : project.tracks())
        {
            if (projectTrack.type() == core::sequencing::TrackType::returnTrack
                || ! projectTrack.routing().sends().empty()
                || projectTrack.routing().audioTo().kind == core::sequencing::RouteEndpointKind::track
                || projectTrack.routing().audioTo().kind == core::sequencing::RouteEndpointKind::returnTrack
                || projectTrack.routing().audioTo().kind == core::sequencing::RouteEndpointKind::sidechain
                || projectTrack.routing().midiFrom().kind != core::sequencing::RouteEndpointKind::none
                || projectTrack.routing().midiTo().kind != core::sequencing::RouteEndpointKind::none)
                return false;

            for (const auto& slot : effectiveDeviceSlotsForTrack (projectTrack))
            {
                if (slot.kind() == core::sequencing::PluginKind::unknown)
                    continue;

                if (slot.isFirstPartyDevice())
                {
                    const auto& deviceState = *slot.firstPartyDevice();
                    if (deviceState.typeId != core::devices::simpleOscComplexTypeId()
                        && ! devices::FirstPartyEffectTracktionPlugin::supportsTypeId (deviceState.typeId))
                        return false;

                    const auto key = nativeExpressionRouteKey (projectTrack.id(), slot.id());
                    expectedNativeKeys.push_back (key);

                    const auto currentNativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                                   projectNativeDevices_.end(),
                                                                   [&key] (const auto& projectPlugin)
                                                                   {
                                                                       return nativeExpressionRouteKey (projectPlugin.trackId,
                                                                                                        projectPlugin.slotId) == key;
                                                                   });

                    if (currentNativeDevice == projectNativeDevices_.end()
                        || currentNativeDevice->plugin == nullptr
                        || currentNativeDevice->kind != slot.kind()
                        || currentNativeDevice->typeId != deviceState.typeId
                        || currentNativeDevice->plugin->isEnabled() == slot.bypassed())
                        return false;

                    continue;
                }

                const auto key = pluginStateKey (projectTrack.id(), slot.id());
                expectedExternalKeys.push_back (key);

                const auto currentPlugin = std::find_if (projectInstruments_.begin(),
                                                         projectInstruments_.end(),
                                                         [&key] (const auto& projectPlugin)
                                                         {
                                                             return pluginStateKey (projectPlugin.trackId, projectPlugin.slotId) == key;
                                                         });

                if (currentPlugin == projectInstruments_.end()
                    || currentPlugin->plugin == nullptr
                    || currentPlugin->kind != slot.kind()
                    || currentPlugin->plugin->isEnabled() == slot.bypassed()
                    || ! pluginReferencesMatchForLiveState (currentPlugin->pluginReference, slot.plugin()))
                    return false;
            }
        }

        if (expectedExternalKeys.size() != projectInstruments_.size())
            return false;

        if (expectedNativeKeys.size() != projectNativeDevices_.size())
            return false;

        return true;
    }

    void syncProjectInPlace (const core::sequencing::Project& project)
    {
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace stop transport" };
            stopTransportPreservingPluginState();
        }

        {
            te::TransportControl::ReallocationInhibitor reallocationInhibitor { edit_->getTransport() };
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace configure tempo sequence" };
                configureTempoSequence (project);
            }
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace configure time signatures" };
                configureTimeSignatureSequence (project);
            }

            auto audioTracks = te::getAudioTracks (*edit_);
            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace rebuild track lookup" };
                rebuildTrackLookup (project, audioTracks);
            }

            auto audioTrackIndex = 0;
            auto reusedClipTracks = 0;
            auto rebuiltClipTracks = 0;
            for (const auto& projectTrack : project.tracks())
            {
                if (projectTrack.type() == core::sequencing::TrackType::master)
                    continue;

                if (audioTrackIndex >= audioTracks.size())
                    break;

                const auto currentAudioTrackIndex = audioTrackIndex;
                auto* audioTrack = audioTracks[audioTrackIndex++];
                if (audioTrack == nullptr)
                    continue;

                const auto clipsAlreadyCurrent = trackClipMaterializationMatches (
                    projectTrack,
                    previousProjectTrackAtAudioIndex (currentAudioTrackIndex));

                if (! clipsAlreadyCurrent)
                {
                    {
                        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace clear track clips track=" + projectTrack.id() };
                        clearTrackClips (*audioTrack);
                    }
                    ++rebuiltClipTracks;
                }
                else
                {
                    ++reusedClipTracks;
                }

                applyProjectTrackMixer (*audioTrack, projectTrack, project);
                configureProjectTrackRouting (*audioTrack, projectTrack, project);

                if (! clipsAlreadyCurrent)
                    createProjectTrackClips (*audioTrack, projectTrack, ! trackUsesNativeSimpleOsc (projectTrack));
            }

            core::diagnostics::writePerformanceTrace (
                "TracktionPlaybackEngine::syncProjectInPlace clip materialization summary reusedTracks="
                    + std::to_string (reusedClipTracks)
                    + " rebuiltTracks=" + std::to_string (rebuiltClipTracks),
                0);

            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace refresh native devices" };
                refreshNativeFirstPartyDeviceStates (project);
            }

            {
                core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace configure master mixer" };
                configureMasterMixerOnly (project);
            }
            prepareExpressionPlaybackRoutes (project);
            applyPreparedExpressionPitchEventsToNativeDevices (project);
            applyPreparedExpressionRoutesToNativeDevices (preparedExpressionPlaybackModel_);
            rebuildPreparedExpressionMixerAutomationCurves (project);
        }

        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::syncProjectInPlace dispatch edit updates" };
            dispatchPendingTracktionEditUpdates();
        }
    }

    void finishProjectSync (const core::sequencing::Project& project,
                            const std::string& message,
                            bool preserveLivePluginStates,
                            bool refreshPluginObservers)
    {
        forceFullProjectSync_ = false;
        automationProject_ = project;
        automationProjectHasLanes_ = projectHasAutomationLanes (project);
        jassert (automationProjectHasLanes_ == projectHasAutomationLanes (*automationProject_));
        testInstrumentStatus_.phraseReady = false;
        projectEndTick_ = std::max (projectEndPosition (project), core::time::TickPosition::fromTicks (core::time::ticksPerQuarterNote * 4));
        TrackPluginStateBlocks livePluginStates;
        if (preserveLivePluginStates)
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::finishProjectSync capture live plugin states" };
            livePluginStates = captureLiveProjectPluginStateBlocks();
        }
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::finishProjectSync transport position and loop" };
            applyTransportLoopRange();
            setTransportPosition (playheadTick_);
        }
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::finishProjectSync rebuild meter clients" };
            rebuildMeterClients (project);
            resetMeterClients();
        }
        if (preserveLivePluginStates)
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::finishProjectSync restore live plugin states" };
            restoreLiveProjectPluginStateBlocks (livePluginStates);
        }
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::finishProjectSync apply automation" };
            applyAutomationAt (playheadTick_);
        }

        const auto loadedInstrument = std::find_if (projectInstruments_.begin(),
                                                    projectInstruments_.end(),
                                                    [] (const auto& projectPlugin)
                                                    {
                                                        return projectPlugin.kind == core::sequencing::PluginKind::instrument
                                                            && projectPlugin.plugin != nullptr;
                                                    });

        if (loadedInstrument != projectInstruments_.end())
        {
            loadedInstrument_ = loadedInstrument->plugin;
            testInstrumentStatus_.pluginLoaded = true;
            testInstrumentStatus_.pluginName = toStdString (loadedInstrument_->getName());
            testInstrumentStatus_.pluginIdentifier = {};
            testInstrumentStatus_.message = message;
            refreshPluginEditorSupport();
        }
        else if (const auto nativeInstrument = std::find_if (projectNativeDevices_.begin(),
                                                             projectNativeDevices_.end(),
                                                             [] (const auto& projectPlugin)
                                                             {
                                                                 return projectPlugin.kind == core::sequencing::PluginKind::instrument
                                                                     && projectPlugin.plugin != nullptr;
                                                             });
                 nativeInstrument != projectNativeDevices_.end())
        {
            loadedInstrument_ = nullptr;
            testInstrumentStatus_.pluginLoaded = true;
            testInstrumentStatus_.pluginEditorSupported = false;
            testInstrumentStatus_.pluginName = toStdString (nativeInstrument->plugin->getName());
            testInstrumentStatus_.pluginIdentifier = nativeInstrument->typeId;
            testInstrumentStatus_.message = message;
        }
        else
        {
            loadedInstrument_ = nullptr;
            testInstrumentStatus_.pluginLoaded = false;
            testInstrumentStatus_.pluginEditorSupported = false;
            testInstrumentStatus_.pluginName.clear();
            testInstrumentStatus_.pluginIdentifier.clear();
            testInstrumentStatus_.message = message + "; no track instruments assigned";
        }

        if (refreshPluginObservers)
        {
            core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::finishProjectSync refresh plugin observers" };
            refreshLivePluginParameterListeners();
            observeLivePluginParameterStateNow();
        }
        status_.message = message;
        updateDeviceStatus();
    }

    void stopTransportPreservingPluginState()
    {
        automationPlaybackTimer_.setRunning (false);

        if (edit_ == nullptr)
            return;

        edit_->getTransport().stop (false, false);
    }

    void dispatchPendingTracktionEditUpdates()
    {
        if (edit_ == nullptr)
            return;

        edit_->dispatchPendingUpdatesSynchronously();
    }

    void setTransportPosition (core::time::TickPosition position)
    {
        if (edit_ == nullptr)
            return;

        const auto time = edit_->tempoSequence.toTime (toBeatPosition (position));
        auto& transport = edit_->getTransport();
        transport.setPosition (time);
        transport.startPosition = time;
    }

    void applyTransportLoopRange()
    {
        if (edit_ == nullptr)
            return;

        auto& transport = edit_->getTransport();
        const auto endTick = std::max (projectEndTick_, core::time::TickPosition::fromTicks (core::time::ticksPerQuarterNote * 4));
        transport.setLoopRange (edit_->tempoSequence.toTime (toBeatRange (core::time::TickPosition {}, endTick - core::time::TickPosition {})));
        transport.looping = loopEnabled_;
    }

    void clearTrackClips (te::AudioTrack& audioTrack)
    {
        auto clips = audioTrack.getClips();

        for (auto* clip : clips)
            if (clip != nullptr)
                clip->removeFromParent();
    }

    const core::sequencing::Track* previousProjectTrackAtAudioIndex (int targetAudioTrackIndex) const noexcept
    {
        if (! automationProject_.has_value())
            return nullptr;

        auto audioTrackIndex = 0;
        for (const auto& track : automationProject_->tracks())
        {
            if (track.type() == core::sequencing::TrackType::master)
                continue;

            if (audioTrackIndex == targetAudioTrackIndex)
                return &track;

            ++audioTrackIndex;
        }

        return nullptr;
    }

    TrackPluginStateBlocks captureLiveProjectPluginStateBlocks()
    {
        TrackPluginStateBlocks states;

        if (edit_ == nullptr)
            return states;

        for (const auto& projectInstrument : projectInstruments_)
        {
            if (projectInstrument.plugin == nullptr)
                continue;

            auto block = capturePluginStateBlock (*projectInstrument.plugin);
            auto parameterValues = capturePluginParameterValues (*projectInstrument.plugin);

            if (block.has_value() || ! parameterValues.empty())
            {
                const auto stateKey = pluginStateKey (projectInstrument.trackId, projectInstrument.slotId);
                states[stateKey] = CapturedTrackPluginState {
                    projectInstrument.pluginReference,
                    block.value_or (juce::MemoryBlock {}),
                    std::move (parameterValues)
                };
                lastKnownProjectPluginStates_[stateKey] = states[stateKey];
            }
        }

        return states;
    }

    void restoreLiveProjectPluginStateBlocks (const TrackPluginStateBlocks& states)
    {
        if (states.empty())
            return;

        for (const auto& projectInstrument : projectInstruments_)
        {
            if (projectInstrument.plugin == nullptr)
                continue;

            const auto state = states.find (pluginStateKey (projectInstrument.trackId, projectInstrument.slotId));
            if (state == states.end())
                continue;

            if (! pluginReferencesMatchForLiveState (state->second.pluginReference, projectInstrument.pluginReference))
                continue;

            restorePluginStateBlock (*projectInstrument.plugin, state->second.stateBlock);
            restorePluginParameterValues (*projectInstrument.plugin, state->second.parameterValues);
        }
    }

    void restorePluginStateBlock (te::ExternalPlugin& externalPlugin, const juce::MemoryBlock& stateBlock)
    {
        if (stateBlock.getSize() == 0)
            return;

        const auto encodedState = stateBlock.toBase64Encoding();
        externalPlugin.state.setProperty (te::IDs::state, encodedState, nullptr);
        externalPlugin.elementState.setProperty (te::IDs::state, encodedState, nullptr);
        externalPlugin.restorePluginStateFromValueTree (externalPlugin.state);
    }

    std::optional<juce::MemoryBlock> capturePluginStateBlock (te::ExternalPlugin& externalPlugin)
    {
        if (auto* instance = externalPlugin.getAudioPluginInstance())
        {
            juce::MemoryBlock block;
            instance->getStateInformation (block);

            if (block.getSize() > 0)
                return block;
        }

        if (edit_ != nullptr)
            edit_->flushPluginStateIfNeeded (externalPlugin);

        juce::MemoryBlock block;
        externalPlugin.getPluginStateFromTree (block);
        if (block.getSize() > 0)
            return block;

        return std::nullopt;
    }

    std::vector<std::pair<int, float>> capturePluginParameterValues (te::ExternalPlugin& externalPlugin) const
    {
        return capturePluginParameterSnapshot (externalPlugin).values;
    }

    void restorePluginParameterValues (te::ExternalPlugin& externalPlugin,
                                       const std::vector<std::pair<int, float>>& values)
    {
        if (values.empty())
            return;

        auto* instance = externalPlugin.getAudioPluginInstance();
        if (instance == nullptr)
            return;

        auto& parameters = instance->getParameters();
        for (const auto& [index, value] : values)
        {
            if (index < 0 || index >= parameters.size())
                continue;

            if (auto* parameter = parameters[index])
            {
                const auto clampedValue = juce::jlimit (0.0f, 1.0f, value);
                if (parameter->getValue() != clampedValue)
                    parameter->setValue (clampedValue);
            }
        }
    }

    CapturedPluginParameterSnapshot capturePluginParameterSnapshot (te::ExternalPlugin& externalPlugin) const
    {
        CapturedPluginParameterSnapshot snapshot;

        if (auto* instance = externalPlugin.getAudioPluginInstance())
        {
            const auto& parameters = instance->getParameters();
            snapshot.values.reserve (static_cast<std::size_t> (parameters.size()));

            for (int index = 0; index < parameters.size(); ++index)
            {
                auto* parameter = parameters[index];
                if (parameter == nullptr)
                    continue;

                const auto value = juce::jlimit (0.0f, 1.0f, parameter->getValue());
                const auto defaultValue = juce::jlimit (0.0f, 1.0f, parameter->getDefaultValue());
                snapshot.values.emplace_back (index, value);
                snapshot.defaultValues.emplace_back (index, defaultValue);

                if (std::abs (value - defaultValue) > 0.0001f)
                    ++snapshot.nonDefaultCount;
            }
        }

        return snapshot;
    }

    static std::optional<float> snapshotValueForIndex (const std::vector<std::pair<int, float>>& values, int index)
    {
        const auto value = std::find_if (values.begin(), values.end(), [index] (const auto& entry)
        {
            return entry.first == index;
        });

        if (value == values.end())
            return std::nullopt;

        return value->second;
    }

    bool isObservedPluginProtectionActive() const
    {
        return protectObservedPluginParameterStateUntilMs_ != 0u
            && static_cast<std::int32_t> (juce::Time::getMillisecondCounter() - protectObservedPluginParameterStateUntilMs_) < 0;
    }

    bool isSuspiciousDefaultReset (const CapturedPluginParameterSnapshot* previous,
                                   const CapturedPluginParameterSnapshot& current) const
    {
        if (! isObservedPluginProtectionActive() || previous == nullptr || previous->nonDefaultCount == 0 || current.values.empty())
            return false;

        for (const auto& [index, currentValue] : current.values)
        {
            const auto previousValue = snapshotValueForIndex (previous->values, index);
            const auto defaultValue = snapshotValueForIndex (current.defaultValues, index);

            if (! previousValue.has_value() || ! defaultValue.has_value())
                continue;

            const auto previousWasEdited = std::abs (*previousValue - *defaultValue) > 0.0001f;
            const auto currentReturnedToDefault = std::abs (currentValue - *defaultValue) <= 0.0001f;
            const auto valueChanged = std::abs (*previousValue - currentValue) > 0.0001f;
            if (previousWasEdited && currentReturnedToDefault && valueChanged)
                return true;
        }

        return false;
    }

    bool shouldAcceptObservedSnapshot (const CapturedPluginParameterSnapshot* previous,
                                       const CapturedPluginParameterSnapshot& current) const
    {
        if (current.values.empty())
            return false;

        return ! isSuspiciousDefaultReset (previous, current);
    }

    void observeProjectPluginParameterStates()
    {
        if (edit_ == nullptr)
            return;

        for (const auto& projectInstrument : projectInstruments_)
        {
            if (projectInstrument.plugin == nullptr)
                continue;

            const auto key = pluginStateKey (projectInstrument.trackId, projectInstrument.slotId);
            const auto snapshot = capturePluginParameterSnapshot (*projectInstrument.plugin);
            const auto existing = observedProjectPluginParameters_.find (key);
            const auto* previousSnapshot = existing == observedProjectPluginParameters_.end() ? nullptr : &existing->second.snapshot;

            if (isSuspiciousDefaultReset (previousSnapshot, snapshot))
            {
                restorePluginParameterValues (*projectInstrument.plugin, previousSnapshot->values);
                continue;
            }

            if (! shouldAcceptObservedSnapshot (previousSnapshot, snapshot))
                continue;

            observedProjectPluginParameters_[key] = ObservedTrackPluginParameterState {
                projectInstrument.pluginReference,
                snapshot
            };
        }
    }

    void observeLoadedPluginParameterStateIfNeeded()
    {
        if (loadedInstrument_ == nullptr)
        {
            observedLoadedPluginParameters_.reset();
            return;
        }

        const auto belongsToProject = std::any_of (projectInstruments_.begin(),
                                                   projectInstruments_.end(),
                                                   [this] (const auto& projectInstrument)
                                                   {
                                                       return projectInstrument.plugin == loadedInstrument_;
                                                   });
        if (belongsToProject)
            return;

        const auto snapshot = capturePluginParameterSnapshot (*loadedInstrument_);
        const auto* previousSnapshot = observedLoadedPluginParameters_.has_value() ? &*observedLoadedPluginParameters_ : nullptr;
        if (isSuspiciousDefaultReset (previousSnapshot, snapshot))
        {
            restorePluginParameterValues (*loadedInstrument_, previousSnapshot->values);
            return;
        }

        if (! shouldAcceptObservedSnapshot (previousSnapshot, snapshot))
            return;

        observedLoadedPluginParameters_ = snapshot;
    }

    void observeLivePluginParameterStateNow()
    {
        lastObservedPluginParameterStateMs_ = juce::Time::getMillisecondCounter();
        observeProjectPluginParameterStates();
        observeLoadedPluginParameterStateIfNeeded();
    }

    void refreshLivePluginParameterListeners()
    {
        livePluginParameterChangeListener_.attachTo (projectInstruments_, loadedInstrument_);
    }

    void restoreObservedPluginParameterStateNow()
    {
        if (edit_ == nullptr)
            return;

        for (const auto& projectInstrument : projectInstruments_)
        {
            if (projectInstrument.plugin == nullptr)
                continue;

            const auto key = pluginStateKey (projectInstrument.trackId, projectInstrument.slotId);
            auto state = observedProjectPluginParameters_.find (key);
            if (state == observedProjectPluginParameters_.end())
            {
                const auto snapshot = capturePluginParameterSnapshot (*projectInstrument.plugin);
                if (! snapshot.values.empty())
                {
                    observedProjectPluginParameters_[key] = ObservedTrackPluginParameterState {
                        projectInstrument.pluginReference,
                        snapshot
                    };
                }
                continue;
            }

            if (! pluginReferencesMatchForLiveState (state->second.pluginReference, projectInstrument.pluginReference))
                continue;

            const auto current = capturePluginParameterSnapshot (*projectInstrument.plugin);
            if (isSuspiciousDefaultReset (&state->second.snapshot, current))
            {
                restorePluginParameterValues (*projectInstrument.plugin, state->second.snapshot.values);
                continue;
            }

            if (shouldAcceptObservedSnapshot (&state->second.snapshot, current))
                state->second.snapshot = current;
        }

        const auto loadedBelongsToProject = std::any_of (projectInstruments_.begin(),
                                                         projectInstruments_.end(),
                                                         [this] (const auto& projectInstrument)
                                                         {
                                                             return projectInstrument.plugin == loadedInstrument_;
                                                         });
        if (loadedBelongsToProject || loadedInstrument_ == nullptr)
            return;

        const auto current = capturePluginParameterSnapshot (*loadedInstrument_);
        if (! observedLoadedPluginParameters_.has_value())
        {
            if (! current.values.empty())
                observedLoadedPluginParameters_ = current;

            return;
        }

        if (isSuspiciousDefaultReset (&*observedLoadedPluginParameters_, current))
        {
            restorePluginParameterValues (*loadedInstrument_, observedLoadedPluginParameters_->values);
            return;
        }

        if (shouldAcceptObservedSnapshot (&*observedLoadedPluginParameters_, current))
            observedLoadedPluginParameters_ = current;
    }

    void appendSyncWarning (const std::string& warning)
    {
        if (warning.empty())
            return;

        if (status_.message.empty() || status_.message == "Ready")
            status_.message = warning;
        else if (status_.message.find (warning) == std::string::npos)
            status_.message += "; " + warning;
    }

    std::optional<int> returnBusForTrack (const std::string& trackId) const
    {
        const auto bus = returnBusByTrackId_.find (trackId);
        if (bus == returnBusByTrackId_.end())
            return std::nullopt;

        return bus->second;
    }

    void rebuildTrackLookup (const core::sequencing::Project& project, const juce::Array<te::AudioTrack*>& audioTracks)
    {
        projectAudioTracksById_.clear();
        returnBusByTrackId_.clear();
        auxSendPluginsByRoute_.clear();

        auto audioTrackIndex = 0;
        auto returnBusIndex = 0;
        for (const auto& projectTrack : project.tracks())
        {
            if (projectTrack.type() == core::sequencing::TrackType::master)
                continue;

            if (audioTrackIndex >= audioTracks.size())
                break;

            if (auto* audioTrack = audioTracks[audioTrackIndex++])
            {
                projectAudioTracksById_[projectTrack.id()] = audioTrack;
                if (projectTrack.type() == core::sequencing::TrackType::returnTrack)
                {
                    const auto bus = returnBusIndex++;
                    returnBusByTrackId_[projectTrack.id()] = bus;
                    if (edit_ != nullptr)
                        edit_->setAuxBusName (bus, toJuceString (projectTrack.name()));
                }
            }
        }
    }

    std::string meterSourceIdForTrack (const core::sequencing::Track& projectTrack) const
    {
        if (! projectTrack.mixerStrip().meterSourceId().empty())
            return projectTrack.mixerStrip().meterSourceId();

        return projectTrack.id();
    }

    te::LevelMeterPlugin* ensureMasterLevelMeterPlugin()
    {
        if (edit_ == nullptr)
            return nullptr;

        auto& masterPluginList = edit_->getMasterPluginList();
        if (auto meter = masterPluginList.getPluginsOfType<te::LevelMeterPlugin>().getLast())
            return meter;

        auto meterPlugin = edit_->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {});
        auto* meter = dynamic_cast<te::LevelMeterPlugin*> (meterPlugin.get());
        if (meter == nullptr)
        {
            appendSyncWarning ("Could not create master level meter");
            return nullptr;
        }

        masterPluginList.insertPlugin (meterPlugin, -1, nullptr);
        return meter;
    }

    void detachMeterClients()
    {
        for (auto& binding : meterClients_)
            if (binding != nullptr && binding->meterPlugin != nullptr)
                binding->meterPlugin->measurer.removeClient (binding->client);

        meterClients_.clear();
    }

    void resetMeterClients()
    {
        for (auto& binding : meterClients_)
            if (binding != nullptr)
                binding->client.reset();
    }

    void attachMeterClient (std::string sourceId,
                            std::string trackId,
                            std::string displayName,
                            bool master,
                            bool returnTrack,
                            bool active,
                            te::LevelMeterPlugin* meterPlugin)
    {
        if (meterPlugin == nullptr)
            return;

        auto binding = std::make_unique<MeterClientBinding>();
        binding->sourceId = std::move (sourceId);
        binding->trackId = std::move (trackId);
        binding->displayName = std::move (displayName);
        binding->master = master;
        binding->returnTrack = returnTrack;
        binding->active = active;
        binding->meterPlugin = meterPlugin;
        binding->client.reset();
        meterPlugin->measurer.addClient (binding->client);
        meterClients_.push_back (std::move (binding));
    }

    void rebuildMeterClients (const core::sequencing::Project& project)
    {
        detachMeterClients();

        if (edit_ == nullptr)
            return;

        const auto audioTracks = te::getAudioTracks (*edit_);
        auto audioTrackIndex = 0;
        for (const auto& projectTrack : project.tracks())
        {
            if (projectTrack.type() == core::sequencing::TrackType::master)
                continue;

            if (audioTrackIndex >= audioTracks.size())
                break;

            auto* audioTrack = audioTracks[audioTrackIndex++];
            if (audioTrack == nullptr)
                continue;

            attachMeterClient (meterSourceIdForTrack (projectTrack),
                               projectTrack.id(),
                               projectTrack.name(),
                               false,
                               projectTrack.type() == core::sequencing::TrackType::returnTrack,
                               projectTrack.mixerStrip().active(),
                               audioTrack->getLevelMeterPlugin());
        }

        const auto* masterTrack = project.masterTrack();
        attachMeterClient (masterTrack == nullptr ? std::string { "master" } : meterSourceIdForTrack (*masterTrack),
                           masterTrack == nullptr ? std::string { "master" } : masterTrack->id(),
                           masterTrack == nullptr ? std::string { "Master" } : masterTrack->name(),
                           true,
                           false,
                           masterTrack == nullptr ? true : masterTrack->mixerStrip().active(),
                           ensureMasterLevelMeterPlugin());
    }

    te::AudioTrack* tracktionTrackForProjectTrackId (const std::string& trackId) const
    {
        const auto track = projectAudioTracksById_.find (trackId);
        return track == projectAudioTracksById_.end() ? nullptr : track->second;
    }

    te::VolumeAndPanPlugin* volumePluginForProjectTrackId (const std::string& trackId) const
    {
        if (edit_ == nullptr)
            return nullptr;

        if (trackId == "master")
            return edit_->getMasterVolumePlugin().get();

        auto* audioTrack = tracktionTrackForProjectTrackId (trackId);
        return audioTrack == nullptr ? nullptr : audioTrack->getVolumePlugin();
    }

    te::VolumeAndPanPlugin* volumePluginForProjectTrack (const core::sequencing::Track& projectTrack) const
    {
        if (projectTrack.type() == core::sequencing::TrackType::master)
            return edit_ == nullptr ? nullptr : edit_->getMasterVolumePlugin().get();

        return volumePluginForProjectTrackId (projectTrack.id());
    }

    void applyVolumeAndPan (te::VolumeAndPanPlugin* volumePlugin, const core::sequencing::MixerStrip& strip)
    {
        if (volumePlugin == nullptr)
            return;

        auto volumeDb = strip.volumeDb();
        if (! strip.active())
            volumeDb = core::sequencing::MixerStrip::silenceDb();

        volumePlugin->setVolumeDb (core::sequencing::MixerStrip::isSilenceDb (volumeDb)
                                       ? core::sequencing::MixerStrip::minimumFiniteVolumeDb
                                       : static_cast<float> (volumeDb));
        volumePlugin->setPan (static_cast<float> (strip.pan()));
    }

    void applyProjectTrackMixer (te::AudioTrack& audioTrack,
                                 const core::sequencing::Track& projectTrack,
                                 const core::sequencing::Project& project)
    {
        audioTrack.setName (toJuceString (projectTrack.name()));
        audioTrack.setMute (projectTrack.mixerStrip().muted());
        const auto effectiveSolo = projectTrack.mixerStrip().soloed()
            || (projectTrack.type() == core::sequencing::TrackType::returnTrack
                && core::sequencing::returnTrackIsRequiredForSoloPath (project, projectTrack.id()));

        audioTrack.setSolo (effectiveSolo);
        applyVolumeAndPan (audioTrack.getVolumePlugin(), projectTrack.mixerStrip());
    }

    void configureMasterMixerOnly (const core::sequencing::Project& project)
    {
        if (edit_ == nullptr)
            return;

        const auto* masterTrack = project.masterTrack();
        if (masterTrack == nullptr)
            return;

        applyVolumeAndPan (edit_->getMasterVolumePlugin().get(), masterTrack->mixerStrip());
    }

    void applyAutomatedVolume (const core::sequencing::Track& projectTrack, double normalizedValue)
    {
        if (edit_ == nullptr)
            return;

        auto* volumePlugin = volumePluginForProjectTrack (projectTrack);

        if (volumePlugin == nullptr)
            return;

        const auto volumeDb = projectTrack.mixerStrip().active()
            ? core::sequencing::volumeDbFromAutomationValue (normalizedValue)
            : core::sequencing::MixerStrip::silenceDb();

        volumePlugin->setVolumeDb (core::sequencing::MixerStrip::isSilenceDb (volumeDb)
                                       ? core::sequencing::MixerStrip::minimumFiniteVolumeDb
                                       : static_cast<float> (volumeDb));
    }

    void applyAutomatedPan (const core::sequencing::Track& projectTrack, double normalizedValue)
    {
        if (edit_ == nullptr)
            return;

        auto* volumePlugin = volumePluginForProjectTrack (projectTrack);

        if (volumePlugin != nullptr)
            volumePlugin->setPan (static_cast<float> (core::sequencing::panFromAutomationValue (normalizedValue)));
    }

    void applyAutomatedSend (const core::sequencing::AutomationTarget& target, double normalizedValue)
    {
        const auto send = auxSendPluginsByRoute_.find (automationSendRouteKey (target.trackId, target.sendTargetTrackId));
        if (send == auxSendPluginsByRoute_.end() || send->second == nullptr)
            return;

        const auto clampedValue = std::clamp (normalizedValue, 0.0, 1.0);
        if (clampedValue <= 0.0)
        {
            send->second->setGainDb (normalizedSendLevelToDb (clampedValue));
            send->second->setMute (true);
        }
        else
        {
            if (send->second->isMute())
                send->second->setMute (false);
            send->second->setGainDb (normalizedSendLevelToDb (clampedValue));
        }
    }

    static bool parameterMatchesAutomationTarget (juce::AudioProcessorParameter& parameter,
                                                 int index,
                                                 const std::string& parameterId)
    {
        if (parameterId == std::to_string (index))
            return true;

        if (auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*> (&parameter))
            if (toStdString (parameterWithId->paramID) == parameterId)
                return true;

        return false;
    }

    void applyAutomatedPluginParameter (const core::sequencing::AutomationTarget& target, double normalizedValue)
    {
        const auto device = std::find_if (projectInstruments_.begin(),
                                          projectInstruments_.end(),
                                          [&target] (const auto& projectPlugin)
                                          {
                                              return projectPlugin.trackId == target.trackId
                                                  && projectPlugin.slotId == target.deviceSlotId;
                                          });

        if (device == projectInstruments_.end() || device->plugin == nullptr)
            return;

        auto* instance = device->plugin->getAudioPluginInstance();
        if (instance == nullptr)
            return;

        auto& parameters = instance->getParameters();
        const auto clampedValue = static_cast<float> (std::clamp (normalizedValue, 0.0, 1.0));
        for (int index = 0; index < parameters.size(); ++index)
        {
            auto* parameter = parameters[index];
            if (parameter == nullptr || ! parameterMatchesAutomationTarget (*parameter, index, target.pluginParameterId))
                continue;

            if (parameter->getValue() != clampedValue)
                parameter->setValue (clampedValue);
            return;
        }
    }

    void applyAutomatedDeviceBypass (const core::sequencing::AutomationTarget& target, double normalizedValue)
    {
        const auto device = std::find_if (projectInstruments_.begin(),
                                          projectInstruments_.end(),
                                          [&target] (const auto& projectPlugin)
                                          {
                                              return projectPlugin.trackId == target.trackId
                                                  && projectPlugin.slotId == target.deviceSlotId;
                                          });

        if (device != projectInstruments_.end() && device->plugin != nullptr)
        {
            device->plugin->setEnabled (normalizedValue < 0.5);
            return;
        }

        const auto nativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                projectNativeDevices_.end(),
                                                [&target] (const auto& projectPlugin)
                                                {
                                                    return projectPlugin.trackId == target.trackId
                                                        && projectPlugin.slotId == target.deviceSlotId;
                                                });

        if (nativeDevice != projectNativeDevices_.end() && nativeDevice->plugin != nullptr)
            nativeDevice->plugin->setEnabled (normalizedValue < 0.5);
    }

    static std::optional<double> preparedExpressionValueAt (const core::sequencing::PreparedExpressionRouteRenderData& route,
                                                            core::time::TickPosition localPosition)
    {
        for (const auto& segment : route.outputSegments)
        {
            if (localPosition < segment.start || localPosition > segment.end)
                continue;

            if (segment.end <= segment.start)
                return segment.endValue;

            const auto elapsed = static_cast<double> ((localPosition - segment.start).ticks());
            const auto duration = static_cast<double> ((segment.end - segment.start).ticks());
            const auto alpha = duration <= 0.0 ? 1.0 : std::clamp (elapsed / duration, 0.0, 1.0);
            return segment.startValue + ((segment.endValue - segment.startValue) * alpha);
        }

        return std::nullopt;
    }

    static bool expressionDestinationIsMixerPlaybackRoute (core::sequencing::ExpressionDestinationKind kind) noexcept
    {
        return kind == core::sequencing::ExpressionDestinationKind::trackVolume
            || kind == core::sequencing::ExpressionDestinationKind::trackPan
            || kind == core::sequencing::ExpressionDestinationKind::sendLevel;
    }

    static bool expressionDestinationUsesTracktionAutomationCurve (core::sequencing::ExpressionDestinationKind kind) noexcept
    {
        return kind == core::sequencing::ExpressionDestinationKind::trackVolume
            || kind == core::sequencing::ExpressionDestinationKind::trackPan;
    }

    static float tracktionMixerParameterValueForExpressionRoute (
        const core::sequencing::Track& projectTrack,
        core::sequencing::ExpressionDestinationKind kind,
        double normalizedValue)
    {
        switch (kind)
        {
            case core::sequencing::ExpressionDestinationKind::trackVolume:
            {
                const auto volumeDb = projectTrack.mixerStrip().active()
                    ? core::sequencing::volumeDbFromAutomationValue (normalizedValue)
                    : core::sequencing::MixerStrip::silenceDb();
                const auto finiteVolumeDb = core::sequencing::MixerStrip::isSilenceDb (volumeDb)
                    ? core::sequencing::MixerStrip::minimumFiniteVolumeDb
                    : volumeDb;
                return te::decibelsToVolumeFaderPosition (static_cast<float> (finiteVolumeDb));
            }

            case core::sequencing::ExpressionDestinationKind::trackPan:
                return static_cast<float> (core::sequencing::panFromAutomationValue (normalizedValue));

            case core::sequencing::ExpressionDestinationKind::pitch:
            case core::sequencing::ExpressionDestinationKind::pitchBend:
            case core::sequencing::ExpressionDestinationKind::sendLevel:
            case core::sequencing::ExpressionDestinationKind::firstPartyParameter:
            case core::sequencing::ExpressionDestinationKind::pluginParameter:
            case core::sequencing::ExpressionDestinationKind::midiCc:
                break;
        }

        return static_cast<float> (normalizedValue);
    }

    te::AutomatableParameter* automatableParameterForExpressionDestination (
        const core::sequencing::ExpressionDestination& destination) const
    {
        auto* volumePlugin = volumePluginForProjectTrackId (destination.trackId);
        if (volumePlugin == nullptr)
            return nullptr;

        switch (destination.kind)
        {
            case core::sequencing::ExpressionDestinationKind::trackVolume:
                return volumePlugin->volParam.get();

            case core::sequencing::ExpressionDestinationKind::trackPan:
                return volumePlugin->panParam.get();

            case core::sequencing::ExpressionDestinationKind::pitch:
            case core::sequencing::ExpressionDestinationKind::pitchBend:
            case core::sequencing::ExpressionDestinationKind::sendLevel:
            case core::sequencing::ExpressionDestinationKind::firstPartyParameter:
            case core::sequencing::ExpressionDestinationKind::pluginParameter:
            case core::sequencing::ExpressionDestinationKind::midiCc:
                break;
        }

        return nullptr;
    }

    void clearPreparedExpressionMixerAutomationCurves (const core::sequencing::Project& project)
    {
        if (edit_ == nullptr)
            return;

        const auto clearForTrackId = [this] (const std::string& trackId)
        {
            auto* volumePlugin = volumePluginForProjectTrackId (trackId);
            if (volumePlugin == nullptr)
                return;

            if (volumePlugin->volParam != nullptr)
                volumePlugin->volParam->getCurve().clear (nullptr);
            if (volumePlugin->panParam != nullptr)
                volumePlugin->panParam->getCurve().clear (nullptr);
        };

        for (const auto& track : project.tracks())
            clearForTrackId (track.id());

        if (project.masterTrack() != nullptr)
            clearForTrackId (project.masterTrack()->id());
        else
            clearForTrackId ("master");
    }

    void rebuildPreparedExpressionMixerAutomationCurves (const core::sequencing::Project& project)
    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "TracktionPlaybackEngine::rebuildPreparedExpressionMixerAutomationCurves"
        };

        clearPreparedExpressionMixerAutomationCurves (project);

        if (! expressionProjectHasPlaybackRoutes_ || edit_ == nullptr)
            return;

        for (const auto& clip : preparedExpressionPlaybackModel_.clips)
        {
            for (const auto& lane : clip.lanes)
            {
                if (! lane.enabled)
                    continue;

                for (const auto& route : lane.routes)
                {
                    if (! route.available
                        || route.outputSegments.empty()
                        || ! expressionDestinationUsesTracktionAutomationCurve (route.destination.kind))
                    {
                        continue;
                    }

                    const auto* projectTrack = project.findTrackById (route.destination.trackId);
                    if (projectTrack == nullptr)
                        continue;
                    if (route.destination.kind == core::sequencing::ExpressionDestinationKind::trackVolume
                        && trackUsesNativeSimpleOsc (*projectTrack))
                    {
                        continue;
                    }

                    auto* parameter = automatableParameterForExpressionDestination (route.destination);
                    if (parameter == nullptr)
                        continue;

                    auto& curve = parameter->getCurve();
                    auto previousEndTicks = std::optional<std::int64_t> {};
                    auto previousEndValue = std::optional<float> {};
                    for (const auto& segment : route.outputSegments)
                    {
                        const auto projectStart = core::time::TickPosition::fromTicks (
                            clip.clipStartInProject.ticks() + segment.start.ticks());
                        const auto projectEnd = core::time::TickPosition::fromTicks (
                            clip.clipStartInProject.ticks() + segment.end.ticks());
                        if (projectEnd < projectStart)
                            continue;

                        const auto startValue = tracktionMixerParameterValueForExpressionRoute (
                            *projectTrack,
                            route.destination.kind,
                            segment.startValue);
                        const auto endValue = tracktionMixerParameterValueForExpressionRoute (
                            *projectTrack,
                            route.destination.kind,
                            segment.endValue);

                        if (! previousEndTicks.has_value()
                            || *previousEndTicks != projectStart.ticks()
                            || ! previousEndValue.has_value()
                            || std::abs (*previousEndValue - startValue) > 0.000001f)
                        {
                            curve.addPoint (tracktion::TimePosition::fromSeconds (tickPositionToEditSeconds (projectStart)),
                                            startValue,
                                            0.0f,
                                            nullptr);
                        }

                        curve.addPoint (tracktion::TimePosition::fromSeconds (tickPositionToEditSeconds (projectEnd)),
                                        endValue,
                                        0.0f,
                                        nullptr);
                        previousEndTicks = projectEnd.ticks();
                        previousEndValue = endValue;
                    }
                }
            }
        }
    }

    void prepareExpressionPlaybackRoutes (const core::sequencing::Project& project)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::prepareExpressionPlaybackRoutes" };
        constexpr auto expressionSegmentStepTicks = core::time::ticksPerQuarterNote / 16;
        preparedExpressionPlaybackModel_ = core::sequencing::prepareExpressionRenderModel (
            project,
            core::time::TickDuration::fromTicks (expressionSegmentStepTicks));

        expressionProjectHasPlaybackRoutes_ = false;
        for (const auto& clip : preparedExpressionPlaybackModel_.clips)
            for (const auto& lane : clip.lanes)
                if (lane.enabled)
                    for (const auto& route : lane.routes)
                        if (route.available
                            && ! route.outputSegments.empty()
                            && expressionDestinationIsMixerPlaybackRoute (route.destination.kind))
                        {
                            expressionProjectHasPlaybackRoutes_ = true;
                            return;
                        }
    }

    void applyPreparedExpressionMixerRoutesAt (core::time::TickPosition position)
    {
        if (! expressionProjectHasPlaybackRoutes_ || ! automationProject_.has_value() || edit_ == nullptr)
            return;

        for (const auto& clip : preparedExpressionPlaybackModel_.clips)
        {
            const auto localTicks = position.ticks() - clip.clipStartInProject.ticks();
            if (localTicks < clip.localRegion.start().ticks() || localTicks > clip.localRegion.end().ticks())
                continue;

            const auto localPosition = core::time::TickPosition::fromTicks (localTicks);
            for (const auto& lane : clip.lanes)
            {
                if (! lane.enabled)
                    continue;

                for (const auto& route : lane.routes)
                {
                    if (! route.available
                        || route.outputSegments.empty()
                        || ! expressionDestinationIsMixerPlaybackRoute (route.destination.kind))
                    {
                        continue;
                    }

                    const auto value = preparedExpressionValueAt (route, localPosition);
                    if (! value.has_value())
                        continue;

                    const auto* projectTrack = automationProject_->findTrackById (route.destination.trackId);
                    if (projectTrack == nullptr)
                        continue;
                    if (route.destination.kind == core::sequencing::ExpressionDestinationKind::trackVolume
                        && trackUsesNativeSimpleOsc (*projectTrack))
                    {
                        continue;
                    }

                    switch (route.destination.kind)
                    {
                        case core::sequencing::ExpressionDestinationKind::trackVolume:
                            applyAutomatedVolume (*projectTrack, *value);
                            break;

                        case core::sequencing::ExpressionDestinationKind::trackPan:
                            applyAutomatedPan (*projectTrack, *value);
                            break;

                        case core::sequencing::ExpressionDestinationKind::sendLevel:
                            applyAutomatedSend (core::sequencing::AutomationTarget::sendLevel (route.destination.trackId,
                                                                                                route.destination.sendTargetTrackId),
                                                *value);
                            break;

                        case core::sequencing::ExpressionDestinationKind::pitch:
                        case core::sequencing::ExpressionDestinationKind::pitchBend:
                        case core::sequencing::ExpressionDestinationKind::firstPartyParameter:
                        case core::sequencing::ExpressionDestinationKind::pluginParameter:
                        case core::sequencing::ExpressionDestinationKind::midiCc:
                            break;
                    }
                }
            }
        }
    }

    void applyAutomationAt (core::time::TickPosition position)
    {
        if (edit_ == nullptr)
            return;

        if (automationProjectHasLanes_ && automationProject_.has_value())
        {
            const auto snapshot = core::sequencing::automationPlaybackSnapshotAt (*automationProject_, position);
            for (const auto& value : snapshot.values)
            {
                const auto* projectTrack = automationProject_->findTrackById (value.target.trackId);
                if (projectTrack == nullptr)
                    continue;

                switch (value.target.kind)
                {
                    case core::sequencing::AutomationTargetKind::trackVolume:
                        applyAutomatedVolume (*projectTrack, value.normalizedValue);
                        break;

                    case core::sequencing::AutomationTargetKind::trackPan:
                        applyAutomatedPan (*projectTrack, value.normalizedValue);
                        break;

                    case core::sequencing::AutomationTargetKind::sendLevel:
                        applyAutomatedSend (value.target, value.normalizedValue);
                        break;

                    case core::sequencing::AutomationTargetKind::deviceBypass:
                        applyAutomatedDeviceBypass (value.target, value.normalizedValue);
                        break;

                    case core::sequencing::AutomationTargetKind::pluginParameter:
                        applyAutomatedPluginParameter (value.target, value.normalizedValue);
                        break;

                    case core::sequencing::AutomationTargetKind::trackMute:
                        break;
                }
            }
        }

        applyPreparedExpressionMixerRoutesAt (position);
    }

    double tickPositionToEditSeconds (core::time::TickPosition position) const
    {
        if (edit_ == nullptr)
            return 0.0;

        return edit_->tempoSequence.toTime (toBeatPosition (position)).inSeconds();
    }

    void refreshNativeFirstPartyDeviceStates (const core::sequencing::Project& project)
    {
        for (const auto& track : project.tracks())
        {
            for (const auto& slot : effectiveDeviceSlotsForTrack (track))
            {
                if (! slot.isFirstPartyDevice() || ! slot.firstPartyDevice().has_value())
                    continue;

                const auto key = nativeExpressionRouteKey (track.id(), slot.id());
                const auto nativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                        projectNativeDevices_.end(),
                                                        [&key] (const auto& projectPlugin)
                                                        {
                                                            return nativeExpressionRouteKey (projectPlugin.trackId,
                                                                                             projectPlugin.slotId) == key;
                                                        });

                if (nativeDevice == projectNativeDevices_.end() || nativeDevice->plugin == nullptr)
                    continue;

                nativeDevice->plugin->setEnabled (! slot.bypassed());

                if (auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice->plugin.get()))
                    simpleOsc->setFirstPartyDeviceState (*slot.firstPartyDevice());
                else if (auto* effect = dynamic_cast<devices::FirstPartyEffectTracktionPlugin*> (nativeDevice->plugin.get()))
                    effect->setFirstPartyDeviceState (*slot.firstPartyDevice());
            }
        }
    }

    devices::SimpleOscComplexTracktionPlugin* simpleOscPluginForExpressionDestination (
        const core::sequencing::ExpressionDestination& destination) const
    {
        if (destination.kind != core::sequencing::ExpressionDestinationKind::firstPartyParameter)
            return nullptr;

        const auto routeKey = nativeExpressionRouteKey (destination.trackId, destination.deviceSlotId);
        const auto nativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                projectNativeDevices_.end(),
                                                [&routeKey] (const auto& projectPlugin)
                                                {
                                                    return nativeExpressionRouteKey (projectPlugin.trackId, projectPlugin.slotId) == routeKey;
                                                });

        if (nativeDevice == projectNativeDevices_.end() || nativeDevice->plugin == nullptr)
            return nullptr;

        return dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice->plugin.get());
    }

    std::optional<std::string> nativeSimpleOscDeviceKeyForTrackId (const std::string& trackId) const
    {
        const auto nativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                projectNativeDevices_.end(),
                                                [&trackId] (const auto& projectPlugin)
                                                {
                                                    if (projectPlugin.trackId != trackId || projectPlugin.plugin == nullptr)
                                                        return false;

                                                    return dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (
                                                        projectPlugin.plugin.get()) != nullptr;
                                                });

        if (nativeDevice == projectNativeDevices_.end())
            return std::nullopt;

        return nativeExpressionRouteKey (nativeDevice->trackId, nativeDevice->slotId);
    }

    bool trackUsesNativeSimpleOsc (const core::sequencing::Track& track) const
    {
        return std::any_of (projectNativeDevices_.begin(), projectNativeDevices_.end(), [&track] (const auto& nativeDevice)
        {
            if (nativeDevice.trackId != track.id() || nativeDevice.plugin == nullptr)
                return false;

            return dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()) != nullptr;
        });
    }

    void appendSimpleOscPitchEventsForClip (const std::string& trackId,
                                            const core::sequencing::MidiClip& clip,
                                            const core::sequencing::Region& repetition,
                                            std::vector<devices::SimpleOscComplexScheduledNoteEvent>& noteEvents,
                                            std::vector<devices::SimpleOscComplexScheduledSlurEvent>& slurEvents,
                                            std::vector<devices::SimpleOscComplexScheduledPitchOffsetEvent>& pitchOffsetEvents,
                                            const core::time::ProjectRhythmSettings& rhythmSettings) const
    {
        const auto clipStart = clip.startInProject() + (repetition.start() - core::time::TickPosition {});
        const auto sourceLength = clip.loop().isEnabled() ? clip.loop().loopDuration() : clip.length();
        const auto repetitionLength = repetition.duration();
        const auto repetitionStartTicks = repetition.start().ticks();
        const auto noteHandleFor = [&] (const std::string& noteId)
        {
            return stableSimpleOscNoteId (trackId, clip.id(), noteId, repetitionStartTicks);
        };
        const auto noteHasPlayableSpan = [&sourceLength, &repetitionLength] (const core::sequencing::MidiNote& note)
        {
            if (note.startInClip() >= core::time::TickPosition {} + sourceLength)
                return false;
            if (note.startInClip() >= core::time::TickPosition {} + repetitionLength)
                return false;

            const auto remainingTicks = repetitionLength.ticks() - note.startInClip().ticks();
            if (remainingTicks <= 0)
                return false;

            return std::min (note.duration().ticks(), remainingTicks) > 0;
        };

        std::unordered_set<std::string> slurDestinationNoteIds;
        std::unordered_set<std::string> legatoSlurSourceNoteIds;
        if (const auto* pitchLane = clip.expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId()))
        {
            for (const auto& slur : pitchLane->pitchSlurs())
            {
                const auto* sourceNote = clip.findNoteById (slur.sourceNoteId());
                const auto* destinationNote = clip.findNoteById (slur.destinationNoteId());
                if (sourceNote == nullptr || destinationNote == nullptr || ! noteHasPlayableSpan (*destinationNote))
                    continue;

                slurDestinationNoteIds.insert (slur.destinationNoteId());
                if (slur.legatoNoRetrigger() && noteHasPlayableSpan (*sourceNote))
                    legatoSlurSourceNoteIds.insert (slur.sourceNoteId());
            }
        }

        for (const auto& note : clip.notes())
        {
            if (! noteHasPlayableSpan (note))
                continue;

            const auto remainingTicks = repetitionLength.ticks() - note.startInClip().ticks();
            const auto noteDuration = core::time::TickDuration::fromTicks (std::min (note.duration().ticks(), remainingTicks));
            const auto noteId = noteHandleFor (note.id());
            const auto noteStart = clipStart + core::time::TickDuration::fromTicks (note.startInClip().ticks());
            const auto noteEnd = noteStart + noteDuration;

            if (slurDestinationNoteIds.find (note.id()) == slurDestinationNoteIds.end())
            {
                noteEvents.push_back (devices::SimpleOscComplexScheduledNoteEvent {
                    tickPositionToEditSeconds (noteStart),
                    noteId,
                    note.pitch().value(),
                    static_cast<float> (std::clamp (note.velocity(), 0, 127)) / 127.0f,
                    true
                });
            }

            if (legatoSlurSourceNoteIds.find (note.id()) == legatoSlurSourceNoteIds.end())
            {
                noteEvents.push_back (devices::SimpleOscComplexScheduledNoteEvent {
                    tickPositionToEditSeconds (noteEnd),
                    noteId,
                    note.pitch().value(),
                    0.0f,
                    false
                });
            }
        }

        const auto* pitchLane = clip.expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (pitchLane == nullptr)
            return;

        for (const auto& slur : pitchLane->pitchSlurs())
        {
            const auto* sourceNote = clip.findNoteById (slur.sourceNoteId());
            const auto* destinationNote = clip.findNoteById (slur.destinationNoteId());
            if (sourceNote == nullptr || destinationNote == nullptr)
                continue;
            if (destinationNote->startInClip() >= core::time::TickPosition {} + sourceLength
                || destinationNote->startInClip() >= core::time::TickPosition {} + repetitionLength)
            {
                continue;
            }

            const auto destinationStart = clipStart + core::time::TickDuration::fromTicks (destinationNote->startInClip().ticks());
            const auto slurEnd = destinationStart + slur.slurTime();
            const auto slurTimeSeconds = std::max (0.0, tickPositionToEditSeconds (slurEnd) - tickPositionToEditSeconds (destinationStart));
            slurEvents.push_back (devices::SimpleOscComplexScheduledSlurEvent {
                tickPositionToEditSeconds (destinationStart),
                noteHandleFor (sourceNote->id()),
                noteHandleFor (destinationNote->id()),
                destinationNote->pitch().value(),
                static_cast<float> (std::clamp (destinationNote->velocity(), 0, 127)) / 127.0f,
                slurTimeSeconds,
                slur.curveShape(),
                slur.legatoNoRetrigger()
            });
        }

        constexpr auto vibratoSampleTicks = core::time::ticksPerQuarterNote / 32;
        for (const auto& vibrato : pitchLane->vibratoExpressions())
        {
            if (vibrato.amplitudeSemitones() <= 0.0)
                continue;

            for (const auto& sourceNoteId : vibrato.sourceNoteIds())
            {
                const auto* sourceNote = clip.findNoteById (sourceNoteId);
                if (sourceNote == nullptr)
                    continue;

                auto localStart = std::max (sourceNote->startInClip().ticks(), vibrato.phraseRegion().start().ticks());
                auto localEnd = std::min ((sourceNote->startInClip() + sourceNote->duration()).ticks(), vibrato.phraseRegion().end().ticks());
                std::optional<std::string> slurDestinationId;
                std::optional<core::time::TickPosition> slurDestinationStart;
                for (const auto& slur : pitchLane->pitchSlurs())
                {
                    if (slur.sourceNoteId() != sourceNoteId)
                        continue;

                    if (const auto* destinationNote = clip.findNoteById (slur.destinationNoteId()))
                    {
                        slurDestinationId = destinationNote->id();
                        slurDestinationStart = destinationNote->startInClip();
                        localEnd = std::max (localEnd,
                                             std::min ((destinationNote->startInClip() + destinationNote->duration()).ticks(),
                                                       vibrato.phraseRegion().end().ticks()));
                    }
                }

                localStart = std::max<std::int64_t> (localStart, 0);
                localEnd = std::min<std::int64_t> (localEnd, repetitionLength.ticks());
                if (localEnd <= localStart)
                    continue;

                for (auto tick = localStart; tick <= localEnd; tick += vibratoSampleTicks)
                {
                    const auto localPosition = core::time::TickPosition::fromTicks (tick);
                    const auto sample = core::sequencing::evaluatePitchVoiceTrajectoryAt (clip,
                                                                                          *pitchLane,
                                                                                          sourceNoteId,
                                                                                          localPosition,
                                                                                          rhythmSettings);
                    const auto projectPosition = clipStart + core::time::TickDuration::fromTicks (tick);
                    const auto targetNoteId = (slurDestinationId.has_value()
                                               && slurDestinationStart.has_value()
                                               && localPosition >= *slurDestinationStart)
                        ? *slurDestinationId
                        : sourceNoteId;

                    pitchOffsetEvents.push_back (devices::SimpleOscComplexScheduledPitchOffsetEvent {
                        tickPositionToEditSeconds (projectPosition),
                        noteHandleFor (targetNoteId),
                        sample.vibratoOffsetSemitones
                    });
                }
            }
        }
    }

    void applyPreparedExpressionPitchEventsToNativeDevices (const core::sequencing::Project& project)
    {
        for (const auto& nativeDevice : projectNativeDevices_)
            if (auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                simpleOsc->clearExpressionPitchEvents();

        if (edit_ == nullptr)
            return;

        for (const auto& nativeDevice : projectNativeDevices_)
        {
            auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get());
            if (simpleOsc == nullptr)
                continue;

            const auto* track = project.findTrackById (nativeDevice.trackId);
            if (track == nullptr)
                continue;

            std::vector<devices::SimpleOscComplexScheduledNoteEvent> noteEvents;
            std::vector<devices::SimpleOscComplexScheduledSlurEvent> slurEvents;
            std::vector<devices::SimpleOscComplexScheduledPitchOffsetEvent> pitchOffsetEvents;
            for (const auto& clip : track->clips())
            {
                for (const auto& repetition : clip.loop().repetitionsForLength (clip.length()))
                    appendSimpleOscPitchEventsForClip (track->id(),
                                                       clip,
                                                       repetition,
                                                       noteEvents,
                                                       slurEvents,
                                                       pitchOffsetEvents,
                                                       project.rhythmSettings());
            }

            simpleOsc->setExpressionPitchEvents (std::move (noteEvents), std::move (slurEvents), std::move (pitchOffsetEvents));
        }
    }

    void appendPreparedRouteStream (std::vector<devices::SimpleOscComplexModulationStream>& streams,
                                    const core::sequencing::PreparedExpressionClipRenderData& clip,
                                    const core::sequencing::PreparedExpressionRouteRenderData& route,
                                    std::string_view parameterIdOverride = {})
    {
        const auto parameterId = parameterIdOverride.empty()
            ? std::string_view { route.destination.parameterId }
            : parameterIdOverride;
        if (parameterId.empty() || route.outputSegments.empty())
            return;

        auto stream = std::find_if (streams.begin(), streams.end(), [parameterId] (const auto& candidate)
        {
            return candidate.parameterId == parameterId;
        });

        if (stream == streams.end())
        {
            streams.push_back (devices::SimpleOscComplexModulationStream { std::string { parameterId }, {} });
            stream = std::prev (streams.end());
        }

        stream->segments.reserve (stream->segments.size() + route.outputSegments.size());
        for (const auto& segment : route.outputSegments)
        {
            const auto projectStart = core::time::TickPosition::fromTicks (clip.clipStartInProject.ticks() + segment.start.ticks());
            const auto projectEnd = core::time::TickPosition::fromTicks (clip.clipStartInProject.ticks() + segment.end.ticks());
            const auto startSeconds = tickPositionToEditSeconds (projectStart);
            const auto endSeconds = tickPositionToEditSeconds (projectEnd);

            if (endSeconds < startSeconds)
                continue;

            stream->segments.push_back (devices::SimpleOscComplexModulationSegment {
                startSeconds,
                endSeconds,
                segment.startValue,
                segment.endValue
            });
        }
    }

    void applyPreparedExpressionRoutesToNativeDevices (const core::sequencing::PreparedExpressionRenderModel& prepared)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::applyPreparedExpressionRoutesToNativeDevices" };
        for (const auto& nativeDevice : projectNativeDevices_)
            if (auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice.plugin.get()))
                simpleOsc->clearExpressionModulationStreams();

        if (projectNativeDevices_.empty() || edit_ == nullptr)
            return;

        auto unsupportedPluginRouteCount = 0;
        auto unsupportedMidiRouteCount = 0;
        std::unordered_map<std::string, std::vector<devices::SimpleOscComplexModulationStream>> streamsByDevice;

        for (const auto& clip : prepared.clips)
        {
            for (const auto& lane : clip.lanes)
            {
                if (! lane.enabled)
                    continue;

                for (const auto& route : lane.routes)
                {
                    if (! route.available || route.outputSegments.empty())
                        continue;

                    switch (route.destination.kind)
                    {
                        case core::sequencing::ExpressionDestinationKind::firstPartyParameter:
                        {
                            if (simpleOscPluginForExpressionDestination (route.destination) == nullptr)
                                continue;

                            auto& streams = streamsByDevice[nativeExpressionRouteKey (route.destination.trackId,
                                                                                       route.destination.deviceSlotId)];
                            appendPreparedRouteStream (streams, clip, route);
                            break;
                        }

                        case core::sequencing::ExpressionDestinationKind::trackVolume:
                        {
                            const auto deviceKey = nativeSimpleOscDeviceKeyForTrackId (route.destination.trackId);
                            if (! deviceKey.has_value())
                                break;

                            auto& streams = streamsByDevice[*deviceKey];
                            appendPreparedRouteStream (streams, clip, route, "amp.level");
                            break;
                        }

                        case core::sequencing::ExpressionDestinationKind::pluginParameter:
                            ++unsupportedPluginRouteCount;
                            break;

                        case core::sequencing::ExpressionDestinationKind::midiCc:
                        case core::sequencing::ExpressionDestinationKind::pitch:
                        case core::sequencing::ExpressionDestinationKind::pitchBend:
                            ++unsupportedMidiRouteCount;
                            break;

                        case core::sequencing::ExpressionDestinationKind::trackPan:
                        case core::sequencing::ExpressionDestinationKind::sendLevel:
                            break;
                    }
                }
            }
        }

        for (auto& [deviceKey, streams] : streamsByDevice)
        {
            for (auto& stream : streams)
            {
                std::sort (stream.segments.begin(), stream.segments.end(), [] (const auto& lhs, const auto& rhs)
                {
                    return lhs.startSeconds < rhs.startSeconds;
                });
            }

            const auto separator = deviceKey.find (':');
            if (separator == std::string::npos)
                continue;

            const auto trackId = deviceKey.substr (0, separator);
            const auto slotId = core::sequencing::DeviceSlotId { deviceKey.substr (separator + 1) };
            const auto nativeDevice = std::find_if (projectNativeDevices_.begin(),
                                                    projectNativeDevices_.end(),
                                                    [&trackId, &slotId] (const auto& projectPlugin)
                                                    {
                                                        return projectPlugin.trackId == trackId
                                                            && projectPlugin.slotId == slotId;
                                                    });

            if (nativeDevice == projectNativeDevices_.end() || nativeDevice->plugin == nullptr)
                continue;

            if (auto* simpleOsc = dynamic_cast<devices::SimpleOscComplexTracktionPlugin*> (nativeDevice->plugin.get()))
                simpleOsc->setExpressionModulationStreams (std::move (streams));
        }

        if (unsupportedPluginRouteCount > 0)
            appendSyncWarning ("Expression routes to third-party plugin parameters are not playback-mapped yet");
        if (unsupportedMidiRouteCount > 0)
            appendSyncWarning ("Expression routes to MIDI/pitch destinations are not playback-mapped yet");
    }

    void configureProjectTrackRouting (te::AudioTrack& audioTrack,
                                       const core::sequencing::Track& projectTrack,
                                       const core::sequencing::Project&)
    {
        const auto& audioTo = projectTrack.routing().audioTo();
        switch (audioTo.kind)
        {
            case core::sequencing::RouteEndpointKind::none:
            case core::sequencing::RouteEndpointKind::master:
            case core::sequencing::RouteEndpointKind::hardwareOutput:
                audioTrack.getOutput().setOutputToDefaultDevice (false);
                break;

            case core::sequencing::RouteEndpointKind::track:
            case core::sequencing::RouteEndpointKind::returnTrack:
                if (auto* destination = tracktionTrackForProjectTrackId (audioTo.id))
                {
                    audioTrack.getOutput().setOutputToTrack (destination);
                }
                else
                {
                    appendSyncWarning ("Track '" + projectTrack.name() + "' routes to a missing track; using master output");
                    audioTrack.getOutput().setOutputToDefaultDevice (false);
                }
                break;

            case core::sequencing::RouteEndpointKind::sidechain:
                appendSyncWarning ("Track '" + projectTrack.name() + "' uses sidechain routing that is not engine-mapped yet; using master output");
                audioTrack.getOutput().setOutputToDefaultDevice (false);
                break;
        }

        if (! projectTrack.routing().midiFrom().empty() || ! projectTrack.routing().midiTo().empty())
            appendSyncWarning ("Track '" + projectTrack.name() + "' has MIDI routing selections that are not engine-mapped yet");
    }

    te::Plugin::Ptr createExternalPluginForSlot (const core::sequencing::DeviceSlot& slot)
    {
        if (edit_ == nullptr || tracktionEngine_ == nullptr)
            return {};

        auto description = toJuceDescription (slot.plugin(), slot.kind());
        tracktionEngine_->getPluginManager().knownPluginList.addType (description);
        return edit_->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, description);
    }

    te::Plugin::Ptr createFirstPartyPluginForSlot (const core::sequencing::DeviceSlot& slot)
    {
        if (edit_ == nullptr || ! slot.firstPartyDevice().has_value())
            return {};

        const auto& device = *slot.firstPartyDevice();
        if (device.typeId == core::devices::simpleOscComplexTypeId())
        {
            return edit_->getPluginCache().createNewPlugin (
                devices::SimpleOscComplexTracktionPlugin::createState (device));
        }

        if (devices::FirstPartyEffectTracktionPlugin::supportsTypeId (device.typeId))
        {
            return edit_->getPluginCache().createNewPlugin (
                devices::FirstPartyEffectTracktionPlugin::createState (device));
        }

        return {};
    }

    bool addFirstPartySlotToPluginList (te::PluginList& pluginList,
                                        const std::string& trackId,
                                        const std::string& trackName,
                                        const core::sequencing::DeviceSlot& slot,
                                        int& insertIndex)
    {
        const auto deviceName = slot.firstPartyDevice().has_value() ? slot.firstPartyDevice()->typeId : std::string { "unknown" };
        auto createdPlugin = createFirstPartyPluginForSlot (slot);
        if (createdPlugin == nullptr)
        {
            appendSyncWarning ("Could not create first-party device '" + deviceName + "' on track '" + trackName + "'");
            return true;
        }

        pluginList.insertPlugin (createdPlugin, insertIndex++, nullptr);
        createdPlugin->setEnabled (! slot.bypassed());
        projectNativeDevices_.push_back (ProjectNativeDevicePlugin {
            trackId,
            slot.id(),
            slot.kind(),
            deviceName,
            createdPlugin
        });
        return true;
    }

    bool restoreStateForSlot (te::ExternalPlugin& externalPlugin,
                              const std::string& trackId,
                              const core::sequencing::DeviceSlot& slot,
                              const TrackPluginStateBlocks& livePluginStates)
    {
        const auto stateKey = pluginStateKey (trackId, slot.id());
        if (const auto liveState = livePluginStates.find (stateKey);
            liveState != livePluginStates.end()
            && pluginReferencesMatchForLiveState (liveState->second.pluginReference, slot.plugin()))
        {
            restorePluginStateBlock (externalPlugin, liveState->second.stateBlock);
            restorePluginParameterValues (externalPlugin, liveState->second.parameterValues);
            return true;
        }

        if (const auto knownState = lastKnownProjectPluginStates_.find (stateKey);
            knownState != lastKnownProjectPluginStates_.end()
            && pluginReferencesMatchForLiveState (knownState->second.pluginReference, slot.plugin()))
        {
            restorePluginStateBlock (externalPlugin, knownState->second.stateBlock);
            restorePluginParameterValues (externalPlugin, knownState->second.parameterValues);
            return knownState->second.stateBlock.getSize() > 0 || ! knownState->second.parameterValues.empty();
        }

        if (auto stateBlock = readPluginStateFile (projectPluginStateDirectory_, slot.pluginStateFile());
            stateBlock.has_value())
        {
            restorePluginStateBlock (externalPlugin, *stateBlock);
            return true;
        }

        return false;
    }

    bool addExternalSlotToPluginList (te::PluginList& pluginList,
                                      const std::string& trackId,
                                      const std::string& trackName,
                                      const core::sequencing::DeviceSlot& slot,
                                      int& insertIndex,
                                      const TrackPluginStateBlocks& livePluginStates)
    {
        if (slot.kind() == core::sequencing::PluginKind::unknown)
            return true;

        if (slot.isFirstPartyDevice())
            return addFirstPartySlotToPluginList (pluginList, trackId, trackName, slot, insertIndex);

        if (slot.kind() == core::sequencing::PluginKind::midiEffect)
        {
            appendSyncWarning ("Track '" + trackName + "' has a MIDI effect slot that is not engine-mapped yet");
            return true;
        }

        auto createdPlugin = createExternalPluginForSlot (slot);
        auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (createdPlugin.get());

        if (externalPlugin == nullptr)
        {
            appendSyncWarning ("Could not create plugin '" + slot.plugin().pluginName + "' on track '" + trackName + "'");
            return true;
        }

        pluginList.insertPlugin (createdPlugin, insertIndex++, nullptr);
        externalPlugin->setEnabled (! slot.bypassed());
        projectInstruments_.push_back (ProjectInstrumentPlugin {
            trackId,
            slot.id(),
            slot.kind(),
            slot.plugin(),
            externalPlugin
        });

        const auto loadError = externalPlugin->getLoadError();
        if (loadError.isNotEmpty())
        {
            appendSyncWarning ("Plugin load warning on track '" + trackName + "': " + toStdString (loadError));
            return true;
        }

        restoreStateForSlot (*externalPlugin, trackId, slot, livePluginStates);
        return true;
    }

    bool addAuxReturnToTrack (te::AudioTrack& audioTrack,
                              const core::sequencing::Track& projectTrack,
                              int& insertIndex)
    {
        const auto bus = returnBusForTrack (projectTrack.id());
        if (! bus.has_value())
            return true;

        auto auxReturnPlugin = edit_->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
        auto* auxReturn = dynamic_cast<te::AuxReturnPlugin*> (auxReturnPlugin.get());
        if (auxReturn == nullptr)
        {
            appendSyncWarning ("Could not create aux return for track '" + projectTrack.name() + "'");
            return true;
        }

        audioTrack.pluginList.insertPlugin (auxReturnPlugin, insertIndex++, nullptr);
        auxReturn->busNumber.setValue (*bus, nullptr);
        return true;
    }

    bool addAuxSendsToTrack (te::AudioTrack& audioTrack,
                             const core::sequencing::Track& projectTrack,
                             int& insertIndex)
    {
        for (const auto& send : projectTrack.routing().sends())
        {
            const auto bus = returnBusForTrack (send.targetReturnTrackId);
            if (! bus.has_value())
            {
                appendSyncWarning ("Track '" + projectTrack.name() + "' has a send to a missing return track");
                continue;
            }

            auto auxSendPlugin = edit_->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
            auto* auxSend = dynamic_cast<te::AuxSendPlugin*> (auxSendPlugin.get());
            if (auxSend == nullptr)
            {
                appendSyncWarning ("Could not create aux send on track '" + projectTrack.name() + "'");
                continue;
            }

            audioTrack.pluginList.insertPlugin (auxSendPlugin, insertIndex++, nullptr);
            auxSend->busNumber.setValue (*bus, nullptr);
            auxSend->setGainDb (normalizedSendLevelToDb (send.normalizedLevel));
            auxSend->setMute (send.normalizedLevel <= 0.0);
            auxSendPluginsByRoute_[automationSendRouteKey (projectTrack.id(), send.targetReturnTrackId)] = auxSend;
        }

        return true;
    }

    bool configureProjectTrack (te::AudioTrack& audioTrack,
                                const core::sequencing::Track& projectTrack,
                                const core::sequencing::Project& project,
                                const TrackPluginStateBlocks& livePluginStates)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::configureProjectTrack track=" + projectTrack.id() };

        applyProjectTrackMixer (audioTrack, projectTrack, project);
        configureProjectTrackRouting (audioTrack, projectTrack, project);

        auto insertIndex = 0;
        if (projectTrack.type() == core::sequencing::TrackType::returnTrack)
            addAuxReturnToTrack (audioTrack, projectTrack, insertIndex);

        for (const auto& slot : effectiveDeviceSlotsForTrack (projectTrack))
            if (! addExternalSlotToPluginList (audioTrack.pluginList, projectTrack.id(), projectTrack.name(), slot, insertIndex, livePluginStates))
                return false;

        addAuxSendsToTrack (audioTrack, projectTrack, insertIndex);
        createProjectTrackClips (audioTrack, projectTrack, ! trackUsesNativeSimpleOsc (projectTrack));
        return true;
    }

    void configureMasterTrack (const core::sequencing::Project& project,
                               const TrackPluginStateBlocks& livePluginStates)
    {
        configureMasterMixerOnly (project);

        if (edit_ == nullptr)
            return;

        const auto* masterTrack = project.masterTrack();
        if (masterTrack == nullptr)
        {
            ensureMasterLevelMeterPlugin();
            return;
        }

        auto insertIndex = 0;
        for (const auto& slot : effectiveDeviceSlotsForTrack (*masterTrack))
        {
            if (slot.kind() == core::sequencing::PluginKind::instrument || slot.kind() == core::sequencing::PluginKind::midiEffect)
            {
                appendSyncWarning ("Master track device '" + slot.plugin().pluginName + "' is not an audio effect; skipping");
                continue;
            }

            addExternalSlotToPluginList (edit_->getMasterPluginList(),
                                         masterTrack->id(),
                                         masterTrack->name(),
                                         slot,
                                         insertIndex,
                                         livePluginStates);
        }

        ensureMasterLevelMeterPlugin();
    }

    void createProjectTrackClips (te::AudioTrack& audioTrack,
                                  const core::sequencing::Track& projectTrack,
                                  bool materializeMidiNotes = true)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TracktionPlaybackEngine::createProjectTrackClips track=" + projectTrack.id() };

        auto midiClipCount = std::size_t {};
        auto midiClipRepetitionCount = std::size_t {};
        auto midiNoteCount = std::size_t {};
        auto audioClipCount = projectTrack.audioClips().size();

        for (const auto& clip : projectTrack.clips())
        {
            ++midiClipCount;
            midiNoteCount += clip.notes().size();
            for (const auto& repetition : clip.loop().repetitionsForLength (clip.length()))
            {
                ++midiClipRepetitionCount;
                createProjectMidiClip (audioTrack,
                                       clip,
                                       repetition,
                                       materializeMidiNotes,
                                       ! materializeMidiNotes);
            }
        }

        for (const auto& clip : projectTrack.audioClips())
            createProjectAudioClip (audioTrack, clip);

        core::diagnostics::writePerformanceTrace (
            "TracktionPlaybackEngine::createProjectTrackClips summary track=" + projectTrack.id()
                + " midiClips=" + std::to_string (midiClipCount)
                + " midiClipRepetitions=" + std::to_string (midiClipRepetitionCount)
                + " midiNotes=" + std::to_string (midiNoteCount)
                + " audioClips=" + std::to_string (audioClipCount),
            0);
    }

    void createProjectAudioClip (te::AudioTrack& audioTrack,
                                 const core::sequencing::AudioClip& clip)
    {
        const auto clipRange = toBeatRange (clip.startInProject(), clip.length());
        const auto clipPosition = te::createClipPosition (edit_->tempoSequence,
                                                          clipRange,
                                                          toBeatDuration (clip.sourceOffset()));
        auto waveClip = audioTrack.insertWaveClip (toJuceString (clip.name()),
                                                   juce::File::createFileWithoutCheckingPath (toJuceString (clip.source().filePath)),
                                                   clipPosition,
                                                   false);
        if (waveClip == nullptr)
        {
            appendSyncWarning ("Could not create audio clip '" + clip.name() + "'");
            return;
        }

        waveClip->setGainDB (core::sequencing::MixerStrip::isSilenceDb (clip.gainDb())
                                 ? core::sequencing::MixerStrip::minimumFiniteVolumeDb
                                 : static_cast<float> (clip.gainDb()));
        waveClip->setAutoTempo (clip.stretchToTempo());

        if (clip.loopEnabled())
            waveClip->setLoopRangeBeats ({ tracktion::BeatPosition::fromBeats (0.0), toBeatDuration (clip.length()) });
    }

    void createProjectMidiClip (te::AudioTrack& audioTrack,
                                const core::sequencing::MidiClip& clip,
                                const core::sequencing::Region& repetition,
                                bool materializeMidiNotes,
                                bool addNativeWakeEvent = false)
    {
        const auto clipStart = clip.startInProject() + (repetition.start() - core::time::TickPosition {});
        const auto clipDuration = repetition.duration();
        const auto clipRange = edit_->tempoSequence.toTime (toBeatRange (clipStart, clipDuration));

        auto midiClip = audioTrack.insertMIDIClip (toJuceString (clip.name()), clipRange, nullptr);
        if (midiClip == nullptr)
            return;

        midiClip->setMidiChannel (te::MidiChannel (1));
        auto& sequence = midiClip->getSequence();

        if (! materializeMidiNotes)
        {
            if (addNativeWakeEvent)
                sequence.addControllerEvent (tracktion::BeatPosition::fromBeats (0.0), 1, 0, nullptr);
            return;
        }

        // MIDI events are materialized into Tracktion clips up front; the
        // audio callback consumes the prepared edit instead of traversing the
        // TheorySequencer project model.
        const auto sourceLength = clip.loop().isEnabled() ? clip.loop().loopDuration() : clip.length();
        const auto repetitionLength = repetition.duration();

        for (const auto& note : clip.notes())
        {
            if (note.startInClip() >= core::time::TickPosition {} + sourceLength)
                continue;

            if (note.startInClip() >= core::time::TickPosition {} + repetitionLength)
                continue;

            const auto remainingTicks = repetitionLength.ticks() - note.startInClip().ticks();
            if (remainingTicks <= 0)
                continue;

            const auto noteDuration = core::time::TickDuration::fromTicks (std::min (note.duration().ticks(), remainingTicks));
            if (noteDuration.ticks() <= 0)
                continue;

            sequence.addNote (note.pitch().value(),
                              toBeatPosition (note.startInClip()),
                              toBeatDuration (noteDuration),
                              note.velocity(),
                              0,
                              nullptr);
        }
    }

    void unloadTestInstrument()
    {
        livePluginParameterChangeListener_.detach();

        if (testPhraseClip_ != nullptr)
        {
            testPhraseClip_->removeFromParent();
            testPhraseClip_ = nullptr;
        }

        if (loadedInstrument_ != nullptr)
        {
            loadedInstrument_->hideWindowForShutdown();
            loadedInstrument_->deleteFromParent();
            loadedInstrument_ = nullptr;
        }

        testInstrumentStatus_ = TestInstrumentStatus {};
    }

    void hideProjectPluginWindowsForShutdown()
    {
        for (const auto& projectInstrument : projectInstruments_)
            if (projectInstrument.plugin != nullptr)
                projectInstrument.plugin->hideWindowForShutdown();
    }

    bool validateLoadedInstrument()
    {
        if (loadedInstrument_ == nullptr)
        {
            testInstrumentStatus_.message = "No test instrument plugin was created";
            return false;
        }

        const auto loadError = loadedInstrument_->getLoadError();

        if (loadError.isNotEmpty())
        {
            testInstrumentStatus_.message = toStdString (loadError);
            return false;
        }

        if (loadedInstrument_->getAudioPluginInstance() == nullptr && ! loadedInstrument_->isInitialisingAsync())
        {
            testInstrumentStatus_.message = "VST3 plugin instance was not created";
            return false;
        }

        refreshPluginEditorSupport();
        return true;
    }

    bool createOrReplaceTestPhraseClip()
    {
        auto* track = getFirstAudioTrack();

        if (track == nullptr || edit_ == nullptr)
        {
            testInstrumentStatus_.message = "No audio track is available for the test phrase";
            return false;
        }

        if (testPhraseClip_ != nullptr)
        {
            testPhraseClip_->removeFromParent();
            testPhraseClip_ = nullptr;
        }

        if (edit_->tempoSequence.getNumTempos() > 0)
            if (auto* tempo = edit_->tempoSequence.getTempo (0))
                tempo->setBpm (120.0);

        const auto phraseEnd = edit_->tempoSequence.toTime ({ 1, tracktion::BeatDuration() });
        auto midiClip = track->insertMIDIClip ("Prompt 07 Test Phrase", { tracktion::TimePosition(), phraseEnd }, nullptr);

        if (midiClip == nullptr)
        {
            testInstrumentStatus_.message = "Could not create test MIDI clip";
            return false;
        }

        midiClip->setMidiChannel (te::MidiChannel (1));

        auto& sequence = midiClip->getSequence();
        constexpr std::array<int, 4> notes { 60, 64, 67, 72 };

        for (size_t index = 0; index < notes.size(); ++index)
        {
            sequence.addNote (notes[index],
                              tracktion::BeatPosition::fromBeats (static_cast<double> (index)),
                              tracktion::BeatDuration::fromBeats (1.0),
                              100,
                              0,
                              nullptr);
        }

        midiClip->setLoopRangeBeats ({ tracktion::BeatPosition::fromBeats (0.0), tracktion::BeatPosition::fromBeats (4.0) });
        testPhraseClip_ = midiClip;
        return true;
    }

    void refreshPluginEditorSupport()
    {
        testInstrumentStatus_.pluginEditorSupported = false;

        if (loadedInstrument_ == nullptr)
            return;

        if (auto* instance = loadedInstrument_->getAudioPluginInstance())
            testInstrumentStatus_.pluginEditorSupported = instance->hasEditor();
    }

    void updateDeviceStatus()
    {
        populateDeviceStatus (status_);
    }

    void populateDeviceStatus (PlaybackEngineStatus& status) const
    {
        if (tracktionEngine_ == nullptr)
            return;

        auto& audioDeviceManager = tracktionEngine_->getDeviceManager().deviceManager;

        status.audioDeviceType = toStdString (audioDeviceManager.getCurrentAudioDeviceType());

        if (auto* device = audioDeviceManager.getCurrentAudioDevice())
        {
            status.audioDeviceType = toStdString (device->getTypeName());
            status.audioDeviceName = toStdString (device->getName());
            status.sampleRate = device->getCurrentSampleRate();
            status.blockSize = device->getCurrentBufferSizeSamples();
        }
        else
        {
            status.audioDeviceName.clear();
            status.sampleRate = 0.0;
            status.blockSize = 0;

            if (status.message.empty() || status.message == "Ready")
                status.message = "No audio output device open";
        }
    }

    std::unique_ptr<te::Engine> tracktionEngine_;
    std::unique_ptr<te::Edit> edit_;
    te::ExternalPlugin::Ptr loadedInstrument_;

    std::vector<ProjectInstrumentPlugin> projectInstruments_;
    std::vector<ProjectNativeDevicePlugin> projectNativeDevices_;
    std::vector<std::unique_ptr<MeterClientBinding>> meterClients_;
    std::unordered_map<std::string, te::AudioTrack*> projectAudioTracksById_;
    std::unordered_map<std::string, int> returnBusByTrackId_;
    std::unordered_map<std::string, te::AuxSendPlugin*> auxSendPluginsByRoute_;
    std::optional<core::sequencing::Project> automationProject_;
    core::sequencing::PreparedExpressionRenderModel preparedExpressionPlaybackModel_;
    ObservedTrackPluginParameterStates observedProjectPluginParameters_;
    std::optional<CapturedPluginParameterSnapshot> observedLoadedPluginParameters_;
    TrackPluginStateBlocks lastKnownProjectPluginStates_;
    DeferredObservedParameterRestoreTimer observedParameterRestoreTimer_ { *this };
    AutomationPlaybackTimer automationPlaybackTimer_ { *this };
    LivePluginParameterChangeListener livePluginParameterChangeListener_ { *this };
    std::filesystem::path projectPluginStateDirectory_;
    te::MidiClip::Ptr testPhraseClip_;
    mutable core::time::TickPosition playheadTick_ {};
    core::time::TickPosition projectEndTick_ { core::time::TickPosition::fromTicks (core::time::ticksPerQuarterNote * 4) };
    bool loopEnabled_ = false;
    bool forceFullProjectSync_ = true;
    bool automationProjectHasLanes_ = false;
    bool expressionProjectHasPlaybackRoutes_ = false;
    std::uint64_t meterSnapshotSequence_ = 0;
    std::uint32_t lastObservedPluginParameterStateMs_ = 0;
    std::uint32_t suppressObservedPluginParameterStateUntilMs_ = 0;
    std::uint32_t protectObservedPluginParameterStateUntilMs_ = 0;
    PlaybackEngineStatus status_ { false, false, "Tracktion Engine", {}, {}, {}, 0.0, 0, "Not initialized" };
    TestInstrumentStatus testInstrumentStatus_ {};
};

TracktionPlaybackEngine::TracktionPlaybackEngine()
    : impl_ (std::make_unique<Impl>())
{
}

TracktionPlaybackEngine::~TracktionPlaybackEngine() = default;

bool TracktionPlaybackEngine::initialize()
{
    return impl_->initialize();
}

void TracktionPlaybackEngine::shutdown()
{
    impl_->shutdown();
}

std::vector<std::string> TracktionPlaybackEngine::getAvailableAudioDevices() const
{
    return impl_->getAvailableAudioDevices();
}

std::vector<AudioOutputDevice> TracktionPlaybackEngine::getAvailableOutputDevices() const
{
    return impl_->getAvailableOutputDevices();
}

AudioDeviceSettings TracktionPlaybackEngine::getAudioDeviceSettings() const
{
    return impl_->getAudioDeviceSettings();
}

bool TracktionPlaybackEngine::setOutputDevice (const AudioOutputDevice& outputDevice)
{
    return impl_->setOutputDevice (outputDevice);
}

std::string TracktionPlaybackEngine::createAudioDeviceSettingsXml() const
{
    return impl_->createAudioDeviceSettingsXml();
}

bool TracktionPlaybackEngine::restoreAudioDeviceSettingsXml (const std::string& settingsXml)
{
    return impl_->restoreAudioDeviceSettingsXml (settingsXml);
}

PlaybackEngineStatus TracktionPlaybackEngine::getCurrentStatus() const
{
    return impl_->getCurrentStatus();
}

MeterSnapshot TracktionPlaybackEngine::getMeterSnapshot()
{
    return impl_->getMeterSnapshot();
}

bool TracktionPlaybackEngine::syncProject (const core::sequencing::Project& project)
{
    return impl_->syncProject (project);
}

bool TracktionPlaybackEngine::startPlayback()
{
    return impl_->startPlayback();
}

void TracktionPlaybackEngine::stopPlayback()
{
    impl_->stopPlayback();
}

bool TracktionPlaybackEngine::isPlaying() const
{
    return impl_->isPlaying();
}

core::time::TickPosition TracktionPlaybackEngine::getPlayheadPosition() const
{
    return impl_->getPlayheadPosition();
}

bool TracktionPlaybackEngine::setPlayheadPosition (core::time::TickPosition position)
{
    return impl_->setPlayheadPosition (position);
}

bool TracktionPlaybackEngine::returnToStart()
{
    return impl_->returnToStart();
}

bool TracktionPlaybackEngine::setLoopEnabled (bool enabled)
{
    return impl_->setLoopEnabled (enabled);
}

bool TracktionPlaybackEngine::isLoopEnabled() const
{
    return impl_->isLoopEnabled();
}

bool TracktionPlaybackEngine::loadTestInstrument (const plugins::PluginDescription& plugin)
{
    return impl_->loadTestInstrument (plugin);
}

bool TracktionPlaybackEngine::playTestPhrase()
{
    return impl_->playTestPhrase();
}

void TracktionPlaybackEngine::stopTestPhrase()
{
    impl_->stopTestPhrase();
}

bool TracktionPlaybackEngine::openLoadedPluginEditor()
{
    return impl_->openLoadedPluginEditor();
}

bool TracktionPlaybackEngine::openTrackPluginEditor (const std::string& trackId, const std::string& slotId)
{
    return impl_->openTrackPluginEditor (trackId, slotId);
}

bool TracktionPlaybackEngine::setTrackPluginBypassed (const std::string& trackId, const std::string& slotId, bool bypassed)
{
    return impl_->setTrackPluginBypassed (trackId, slotId, bypassed);
}

TestInstrumentStatus TracktionPlaybackEngine::getTestInstrumentStatus() const
{
    return impl_->getTestInstrumentStatus();
}

std::vector<PluginParameterDebugValue> TracktionPlaybackEngine::debugLoadedPluginParameters() const
{
    return impl_->debugLoadedPluginParameters();
}

bool TracktionPlaybackEngine::debugSetLoadedPluginParameterValue (int parameterIndex, float normalizedValue)
{
    return impl_->debugSetLoadedPluginParameterValue (parameterIndex, normalizedValue);
}

std::optional<float> TracktionPlaybackEngine::debugTrackVolumeDb (const std::string& trackId) const
{
    return impl_->debugTrackVolumeDb (trackId);
}

std::optional<float> TracktionPlaybackEngine::debugTrackPan (const std::string& trackId) const
{
    return impl_->debugTrackPan (trackId);
}

std::size_t TracktionPlaybackEngine::debugTrackVolumeAutomationPointCount (const std::string& trackId) const
{
    return impl_->debugTrackVolumeAutomationPointCount (trackId);
}

std::optional<float> TracktionPlaybackEngine::debugSendGainDb (const std::string& trackId, const std::string& returnTrackId) const
{
    return impl_->debugSendGainDb (trackId, returnTrackId);
}

std::vector<std::uint64_t> TracktionPlaybackEngine::debugNativeSimpleOscExpressionNoteOnEventIds (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscExpressionNoteOnEventIds (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscExpressionSlurEventCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscExpressionSlurEventCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscLegatoSlurEventCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscLegatoSlurEventCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscActiveVoiceCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscActiveVoiceCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscMidiNoteOnCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscMidiNoteOnCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscRenderCallbackCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscRenderCallbackCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscRenderCallbackWithMidiCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscRenderCallbackWithMidiCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscRenderCallbackPlayingCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscRenderCallbackPlayingCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscExpressionSlurFallbackCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscExpressionSlurFallbackCount (trackId);
}

float TracktionPlaybackEngine::debugNativeSimpleOscMaxOutputPeak (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscMaxOutputPeak (trackId);
}

float TracktionPlaybackEngine::debugNativeSimpleOscLastOutputPeak (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscLastOutputPeak (trackId);
}

std::vector<std::size_t> TracktionPlaybackEngine::debugNativeSimpleOscEventCounters (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscEventCounters (trackId);
}

std::pair<double, double> TracktionPlaybackEngine::debugNativeSimpleOscLastRenderTimeRange (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscLastRenderTimeRange (trackId);
}

std::size_t TracktionPlaybackEngine::debugTracktionMidiNoteCount (const std::string& trackId) const
{
    return impl_->debugTracktionMidiNoteCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscExpressionModulationStreamCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscExpressionModulationStreamCount (trackId);
}

std::size_t TracktionPlaybackEngine::debugNativeSimpleOscPatchStateRefreshCount (const std::string& trackId) const
{
    return impl_->debugNativeSimpleOscPatchStateRefreshCount (trackId);
}

bool TracktionPlaybackEngine::debugChaseNativeSimpleOscAtPlayhead (const std::string& trackId)
{
    return impl_->debugChaseNativeSimpleOscAtPlayhead (trackId);
}

void TracktionPlaybackEngine::setProjectPluginStateDirectory (std::filesystem::path packagePath)
{
    impl_->setProjectPluginStateDirectory (std::move (packagePath));
}

std::vector<TrackPluginState> TracktionPlaybackEngine::captureTrackPluginStates()
{
    return impl_->captureTrackPluginStates();
}

void TracktionPlaybackEngine::observeLivePluginParameterState()
{
    impl_->observeLivePluginParameterState();
}

void TracktionPlaybackEngine::restoreObservedPluginParameterStateSoon()
{
    impl_->restoreObservedPluginParameterStateSoon();
}

std::vector<std::string> TracktionPlaybackEngine::debugPluginParameterStateLines (std::string_view label) const
{
    return impl_->debugPluginParameterStateLines (label);
}
}
