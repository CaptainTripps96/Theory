#include "app/AppServices.h"

#include "app/MidiInputRecordingService.h"
#include "core/commands/AddTrackCommand.h"
#include "core/commands/AddClipCommand.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/AssignTrackInstrumentCommand.h"
#include "core/commands/MixerCommands.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/midi/MidiImporter.h"
#include "core/music_theory/EnharmonicSpelling.h"
#include "core/music_theory/MidiPitch.h"
#include "core/serialization/ProjectPackage.h"
#include "core/sequencing/HarmonicContextResolver.h"
#include "core/sequencing/RecordingInputTransform.h"
#include "app/AppSettingsService.h"
#include "engine/PlaybackEngine.h"
#include "engine/TracktionPlaybackEngine.h"
#include "engine/plugins/PluginDescription.h"
#include "engine/plugins/PluginRegistry.h"
#include "engine/plugins/PluginScanService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace tsq::app
{
namespace
{
constexpr auto defaultRecordingClipBars = 4;
constexpr auto beatsPerBar = 4;

bool envFlagEnabled (const char* value)
{
    if (value == nullptr || *value == '\0')
        return false;

    auto text = std::string { value };
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char character) {
        return static_cast<char> (std::tolower (character));
    });

    return text != "0" && text != "false" && text != "off" && text != "no";
}

bool pluginStateTraceEnabled()
{
    static const auto enabled = envFlagEnabled (std::getenv ("TSQ_PLUGIN_STATE_TRACE"));
    return enabled;
}

std::string detectBuildType()
{
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

std::string detectPlatformString()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__) && defined(__MACH__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown platform";
#endif
}

core::sequencing::TrackInstrumentReference toTrackInstrumentReference (const engine::plugins::PluginDescription& plugin)
{
    return core::sequencing::TrackInstrumentReference {
        plugin.name,
        plugin.manufacturer,
        plugin.format.empty() ? "VST3" : plugin.format,
        plugin.fileOrIdentifier,
        plugin.uniqueIdentifier,
        plugin.uniqueId,
        plugin.deprecatedUid,
        plugin.isInstrument,
        plugin.numInputChannels,
        plugin.numOutputChannels,
        {}
    };
}

core::sequencing::PluginReference toPluginReference (const engine::plugins::PluginDescription& plugin)
{
    return core::sequencing::PluginReference {
        plugin.name,
        plugin.manufacturer,
        plugin.format.empty() ? "VST3" : plugin.format,
        plugin.fileOrIdentifier,
        plugin.uniqueIdentifier,
        plugin.uniqueId,
        plugin.deprecatedUid,
        plugin.numInputChannels,
        plugin.numOutputChannels
    };
}

std::string sanitizedIdPart (const std::string& text)
{
    std::string sanitized;
    sanitized.reserve (text.size());

    for (const auto character : text)
    {
        const auto unsignedCharacter = static_cast<unsigned char> (character);
        if (std::isalnum (unsignedCharacter) || character == '-' || character == '_')
            sanitized.push_back (character);
        else if (! sanitized.empty() && sanitized.back() != '-')
            sanitized.push_back ('-');
    }

    while (! sanitized.empty() && sanitized.back() == '-')
        sanitized.pop_back();

    return sanitized.empty() ? "device" : sanitized;
}

core::sequencing::DeviceSlot deviceSlotFromPlugin (const engine::plugins::PluginDescription& plugin,
                                                   core::sequencing::PluginKind deviceKind,
                                                   std::string slotId)
{
    return core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { std::move (slotId) },
        toPluginReference (plugin),
        deviceKind
    };
}

std::string nextDeviceSlotId (const core::sequencing::DeviceChain& chain, const engine::plugins::PluginDescription& plugin)
{
    auto base = sanitizedIdPart (plugin.name.empty() ? engine::plugins::stablePluginIdentifier (plugin) : plugin.name);
    auto candidate = base;
    auto index = 2;

    while (chain.hasSlotId (core::sequencing::DeviceSlotId { candidate }))
        candidate = base + "-" + std::to_string (index++);

    return candidate;
}

core::sequencing::DeviceSlot legacyInstrumentSlot (const core::sequencing::TrackInstrumentReference& instrument,
                                                   const core::sequencing::DeviceChain& chain)
{
    auto slotId = std::string { "instrument" };
    auto index = 2;
    while (chain.hasSlotId (core::sequencing::DeviceSlotId { slotId }))
        slotId = "instrument-" + std::to_string (index++);

    core::sequencing::DeviceSlot slot {
        core::sequencing::DeviceSlotId { std::move (slotId) },
        core::sequencing::PluginReference::fromTrackInstrumentReference (instrument),
        core::sequencing::PluginKind::instrument
    };
    slot.setPluginStateFile (instrument.pluginStateFile);
    return slot;
}

int midiEventKey (int channel, int midiNote) noexcept
{
    return (std::max (1, channel) * 128) + midiNote;
}

std::string nextRecordingClipId (const core::sequencing::Track& track)
{
    auto index = track.clips().size() + 1;
    while (true)
    {
        auto id = "recording-clip-" + std::to_string (index);
        if (track.findClipById (id) == nullptr)
            return id;

        ++index;
    }
}

std::string trackTypePrefix (core::sequencing::TrackType trackType)
{
    switch (trackType)
    {
        case core::sequencing::TrackType::midi: return "MIDI";
        case core::sequencing::TrackType::audio: return "Audio";
        case core::sequencing::TrackType::returnTrack: return "Return";
        case core::sequencing::TrackType::master: return "Master";
    }

    return "Track";
}

std::string nextTrackId (const core::sequencing::Project& project)
{
    for (auto index = project.tracks().size() + 1; index < project.tracks().size() + 10000; ++index)
    {
        auto candidate = "track-" + std::to_string (index);
        if (project.findTrackById (candidate) == nullptr)
            return candidate;
    }

    return "track-" + std::to_string (project.tracks().size() + 1);
}

std::string nextTrackName (const core::sequencing::Project& project, core::sequencing::TrackType trackType)
{
    const auto prefix = trackTypePrefix (trackType);
    auto count = 0;
    for (const auto& track : project.tracks())
        if (track.type() == trackType)
            ++count;

    return prefix + " " + std::to_string (count + 1);
}

std::string displayNameForPlugin (const engine::plugins::PluginDescription& plugin)
{
    if (! plugin.name.empty())
        return plugin.name;

    if (! plugin.fileOrIdentifier.empty())
        return std::filesystem::path { plugin.fileOrIdentifier }.stem().string();

    return "Plugin";
}

std::string displayNameForPath (const std::filesystem::path& path, const std::string& displayName)
{
    if (! displayName.empty())
        return displayName;

    const auto stem = path.stem().string();
    return stem.empty() ? path.filename().string() : stem;
}

std::string sourceIdForAudioFile (const std::filesystem::path& path)
{
    return "audio-source-" + sanitizedIdPart (path.stem().string().empty() ? path.filename().string() : path.stem().string());
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

bool isSupportedMidiFile (const std::filesystem::path& path)
{
    const auto extension = lowercase (path.extension().string());
    return extension == ".mid" || extension == ".midi";
}

core::time::TickDuration defaultTrackCreationClipLength (const core::sequencing::Project& project)
{
    return project.timeSignatureMap().barDurationAt ({}) * defaultRecordingClipBars;
}

std::string pluginStatePathPart (const std::string& text, const std::string& fallback)
{
    std::string sanitized;
    sanitized.reserve (text.size());

    for (const auto character : text)
    {
        const auto unsignedCharacter = static_cast<unsigned char> (character);
        if (std::isalnum (unsignedCharacter) || character == '-' || character == '_')
            sanitized.push_back (character);
        else
            sanitized.push_back ('_');
    }

    return sanitized.empty() ? fallback : sanitized;
}

std::string pluginStateFileNameForTrack (const std::string& trackId)
{
    return "plugin-states/" + pluginStatePathPart (trackId, "track") + ".vststate";
}

std::string pluginStateFileNameForDevice (const std::string& trackId, const std::string& slotId)
{
    return "plugin-states/"
        + pluginStatePathPart (trackId, "track")
        + "--"
        + pluginStatePathPart (slotId, "device")
        + ".vststate";
}

std::string pluginStateKeyForDevice (const std::string& trackId, const std::string& slotId)
{
    return trackId + ":" + slotId;
}

bool pluginReferenceMatches (const core::sequencing::TrackInstrumentReference& reference,
                             const engine::plugins::PluginDescription& plugin)
{
    if (! reference.uniqueIdentifier.empty() && reference.uniqueIdentifier == plugin.uniqueIdentifier)
        return true;

    if (! reference.fileOrIdentifier.empty() && reference.fileOrIdentifier == plugin.fileOrIdentifier)
        return true;

    return reference.uniqueId != 0 && reference.uniqueId == plugin.uniqueId;
}

bool pluginReferenceMatches (const core::sequencing::PluginReference& reference,
                             const engine::plugins::PluginDescription& plugin)
{
    if (! reference.uniqueIdentifier.empty() && reference.uniqueIdentifier == plugin.uniqueIdentifier)
        return true;

    if (! reference.fileOrIdentifier.empty() && reference.fileOrIdentifier == plugin.fileOrIdentifier)
        return true;

    return reference.uniqueId != 0 && reference.uniqueId == plugin.uniqueId;
}

std::string pluginWarningKey (const std::string& trackId, const core::sequencing::PluginReference& reference)
{
    if (! reference.uniqueIdentifier.empty())
        return trackId + ":uid:" + reference.uniqueIdentifier;

    if (! reference.fileOrIdentifier.empty())
        return trackId + ":file:" + reference.fileOrIdentifier;

    return trackId + ":name:" + reference.pluginName;
}

std::string pluginDisplayName (const core::sequencing::PluginReference& reference)
{
    return reference.pluginName.empty() ? reference.fileOrIdentifier : reference.pluginName;
}

std::optional<std::string> pluginKindValidationError (const engine::plugins::PluginDescription& plugin,
                                                      core::sequencing::PluginKind deviceKind)
{
    if (deviceKind == core::sequencing::PluginKind::instrument && ! plugin.isInstrument)
        return "selected plugin is not marked as an instrument";

    if (deviceKind == core::sequencing::PluginKind::audioEffect && ! plugin.isAudioEffect)
        return "selected plugin is not marked as an audio effect";

    if (deviceKind == core::sequencing::PluginKind::midiEffect)
        return "MIDI-effect plugin metadata is not supported yet";

    if (deviceKind == core::sequencing::PluginKind::unknown)
        return "unsupported plugin type";

    return std::nullopt;
}

}

AppServices::AppServices()
    : project_ ("default-project", "Untitled Song"),
      projectCommandContext_ (project_),
      commandStack_ (projectCommandContext_),
      defaultTempo_ (core::time::Tempo { 120.0 }),
      defaultTimeSignature_ (core::time::TimeSignature { 4, 4 }),
      appSettingsService_ (std::make_unique<AppSettingsService>()),
      appSettings_ (appSettingsService_->load()),
      pluginRegistry_ (std::make_unique<engine::plugins::PluginRegistry> (appSettingsService_->pluginRegistryCacheFilePath())),
      pluginScanService_ (std::make_unique<engine::plugins::PluginScanService> (*pluginRegistry_,
                                                                                 appSettingsService_->pluginScanDeadMansPedalFilePath())),
      playbackEngine_ (std::make_unique<engine::TracktionPlaybackEngine>()),
      midiInputRecordingService_ (std::make_unique<MidiInputRecordingService>()),
      buildType_ (detectBuildType()),
      platformString_ (detectPlatformString())
{
    core::diagnostics::ScopedPerformanceTimer startupTimer { "AppServices startup body" };

    if (logger_.setFileOutput (appSettingsService_->diagnosticsLogFilePath()))
        logger_.info ("Diagnostics log: " + appSettingsService_->diagnosticsLogFilePath());
    else
        logger_.warning ("Diagnostics log could not be opened: " + appSettingsService_->diagnosticsLogFilePath());

    logger_.info ("App started");
    logger_.info ("Build type: " + buildType_);
    logger_.info ("Platform: " + platformString_);
    logger_.info ("App settings file: " + appSettingsService_->settingsFilePath());
    logger_.info ("Plugin registry cache: " + pluginRegistry_->cacheFilePath());
    logger_.info ("VST3 search paths: " + pluginScanService_->defaultVst3SearchPaths());
    logger_.info ("Default project: 120 BPM, 4/4, C Major");

    const auto addDefaultTrack = commandStack_.execute (
        std::make_unique<core::commands::AddTrackCommand> (core::sequencing::Track { "track-1", "MIDI 1" }));

    if (addDefaultTrack.failed())
        reportWarning ("Default track creation failed: " + addDefaultTrack.error());
    else
    {
        selectedTrackId_ = "track-1";
        commandStack_.clearHistory();
    }

    commandStack_.setChangeCallback ([this] { markPlaybackProjectDirty(); });

    if (pluginRegistry_->load())
        logger_.info ("Plugin registry loaded: " + std::to_string (pluginRegistry_->pluginCount()) + " entries");
    else
        reportWarning ("Plugin registry cache could not be loaded; starting with an empty registry");

    if (playbackEngine_->initialize())
    {
        logger_.info ("Playback engine initialized: " + playbackEngine_->getCurrentStatus().backendVersion);

        if (appSettings_.hasAudioDeviceState())
        {
            if (playbackEngine_->restoreAudioDeviceSettingsXml (appSettings_.audioDeviceStateXml))
                logger_.info ("Audio device settings restored");
            else
                reportWarning ("Saved audio device settings could not be restored: " + playbackEngine_->getCurrentStatus().message);
        }
        else if (appSettings_.hasOutputDeviceChoice())
        {
            engine::AudioOutputDevice outputDevice {
                appSettings_.outputDeviceType,
                appSettings_.outputDeviceName,
                appSettings_.outputDeviceType.empty() ? appSettings_.outputDeviceName
                                                      : appSettings_.outputDeviceType + ": " + appSettings_.outputDeviceName
            };

            if (playbackEngine_->setOutputDevice (outputDevice))
                logger_.info ("Audio output restored: " + outputDevice.displayName);
            else
                reportWarning ("Saved audio output could not be restored: " + playbackEngine_->getCurrentStatus().message);
        }
    }
    else
    {
        reportError ("Playback engine initialization failed: " + playbackEngine_->getCurrentStatus().message);
    }
}

AppServices::~AppServices()
{
    if (playbackEngine_ != nullptr)
        playbackEngine_->shutdown();
}

core::diagnostics::Logger& AppServices::logger() noexcept
{
    return logger_;
}

const core::diagnostics::Logger& AppServices::logger() const noexcept
{
    return logger_;
}

void AppServices::tracePluginState (std::string_view event) noexcept
{
    try
    {
        if (! pluginStateTraceEnabled())
            return;

        core::diagnostics::ScopedPerformanceTimer timer { "AppServices::tracePluginState" };

        if (event.empty())
            return;

        logger_.debug ("trace: " + std::string { event });

        if (playbackEngine_ == nullptr)
            return;

        for (const auto& line : playbackEngine_->debugPluginParameterStateLines (event))
            logger_.debug (line);
    }
    catch (...)
    {
    }
}

std::string_view AppServices::lastUserMessage() const noexcept
{
    return lastUserMessage_;
}

void AppServices::clearUserMessage() noexcept
{
    lastUserMessage_.clear();
}

void AppServices::reportWarning (std::string message)
{
    if (message.empty())
        return;

    lastUserMessage_ = std::move (message);
    logger_.warning (lastUserMessage_);
}

void AppServices::reportError (std::string message)
{
    if (message.empty())
        return;

    lastUserMessage_ = std::move (message);
    logger_.error (lastUserMessage_);
}

engine::PlaybackEngine& AppServices::playbackEngine() noexcept
{
    return *playbackEngine_;
}

const engine::PlaybackEngine& AppServices::playbackEngine() const noexcept
{
    return *playbackEngine_;
}

const AppSettings& AppServices::appSettings() const noexcept
{
    return appSettings_;
}

bool AppServices::persistAudioSettings()
{
    if (playbackEngine_ == nullptr || appSettingsService_ == nullptr)
        return false;

    const auto deviceSettings = playbackEngine_->getAudioDeviceSettings();

    appSettings_.schemaVersion = AppSettings::currentSchemaVersion;
    appSettings_.audioDeviceStateXml = playbackEngine_->createAudioDeviceSettingsXml();
    appSettings_.outputDeviceType = deviceSettings.outputDeviceType;
    appSettings_.outputDeviceName = deviceSettings.outputDeviceName;
    appSettings_.sampleRate = deviceSettings.sampleRate;
    appSettings_.bufferSize = deviceSettings.bufferSize;

    if (! appSettingsService_->save (appSettings_))
    {
        reportError ("Failed to save app settings: " + appSettingsService_->settingsFilePath());
        return false;
    }

    logger_.info ("Audio settings saved: " + appSettingsService_->settingsFilePath());
    clearUserMessage();
    return true;
}

bool AppServices::newProject()
{
    stopProjectPlayback();
    resetToDefaultProject();
    currentProjectPackagePath_.reset();

    if (playbackEngine_ != nullptr)
        playbackEngine_->setProjectPluginStateDirectory ({});

    commandStack_.clearHistory();
    playbackProjectDirty_ = true;
    logger_.info ("New project created");
    clearUserMessage();
    return true;
}

bool AppServices::saveProject()
{
    if (! currentProjectPackagePath_.has_value())
    {
        reportWarning ("Save project failed: no project package path selected");
        return false;
    }

    return saveProjectAs (*currentProjectPackagePath_);
}

bool AppServices::saveProjectAs (const std::filesystem::path& packagePath)
{
    if (packagePath.empty())
    {
        reportWarning ("Save project failed: empty project package path");
        return false;
    }

    try
    {
        stopProjectPlayback();

        std::vector<engine::TrackPluginState> pluginStates;
        if (playbackEngine_ != nullptr)
            pluginStates = playbackEngine_->captureTrackPluginStates();

        auto projectToSave = projectPreparedForSave (pluginStates);
        core::serialization::ProjectPackage::save (projectToSave, packagePath);

        if (! writePluginStateFiles (packagePath, projectToSave, pluginStates))
            return false;

        project_ = std::move (projectToSave);
        currentProjectPackagePath_ = packagePath;

        if (playbackEngine_ != nullptr)
            playbackEngine_->setProjectPluginStateDirectory (packagePath);

        logger_.info ("Project saved: " + packagePath.string());
        clearUserMessage();
        return true;
    }
    catch (const std::exception& error)
    {
        reportError ("Save project failed: " + std::string { error.what() });
        return false;
    }
}

bool AppServices::loadProject (const std::filesystem::path& packagePath)
{
    if (packagePath.empty())
    {
        reportWarning ("Open project failed: empty project package path");
        return false;
    }

    try
    {
        stopProjectPlayback();

        auto loadResult = core::serialization::ProjectPackage::loadWithWarnings (packagePath);
        project_ = std::move (loadResult.project);
        currentProjectPackagePath_ = packagePath;

        commandStack_.clearHistory();
        playbackProjectDirty_ = true;
        activeRecordedNotes_.clear();
        selectedTrackId_ = project_.tracks().empty() ? std::optional<std::string> {} : std::optional<std::string> { project_.tracks().front().id() };
        selectedRecordingClip_.reset();
        activeRecordingClip_.reset();
        recordingClockInitialized_ = false;

        if (playbackEngine_ != nullptr)
            playbackEngine_->setProjectPluginStateDirectory (packagePath);

        clearUserMessage();

        for (const auto& warning : loadResult.warnings)
            reportWarning ("Project load warning: " + warning);

        warnAboutMissingPlugins();

        if (! syncPlaybackProjectIfNeeded())
            reportWarning ("Project opened, but playback restore reported: " + playbackEngine_->getCurrentStatus().message);

        logger_.info ("Project opened: " + packagePath.string());
        return true;
    }
    catch (const std::exception& error)
    {
        reportError ("Open project failed: " + std::string { error.what() });
        return false;
    }
}

const std::optional<std::filesystem::path>& AppServices::currentProjectPackagePath() const noexcept
{
    return currentProjectPackagePath_;
}

bool AppServices::loadTestInstrument (const engine::plugins::PluginDescription& plugin)
{
    if (playbackEngine_ == nullptr || appSettingsService_ == nullptr)
        return false;

    if (! playbackEngine_->loadTestInstrument (plugin))
    {
        reportWarning ("Test instrument load failed: " + playbackEngine_->getTestInstrumentStatus().message);
        return false;
    }

    appSettings_.schemaVersion = AppSettings::currentSchemaVersion;
    appSettings_.selectedTestInstrumentIdentifier = plugin.uniqueIdentifier;
    appSettings_.selectedTestInstrumentName = plugin.name;

    if (! appSettingsService_->save (appSettings_))
    {
        reportError ("Failed to save selected test instrument: " + appSettingsService_->settingsFilePath());
        return false;
    }

    logger_.info ("Test instrument loaded: " + (plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name));
    clearUserMessage();
    return true;
}

bool AppServices::assignInstrumentToTrack (const std::string& trackId, const engine::plugins::PluginDescription& plugin)
{
    if (trackId.empty())
    {
        reportWarning ("Track instrument assignment failed: no track selected");
        return false;
    }

    if (! plugin.isInstrument)
    {
        reportWarning ("Track instrument assignment failed: selected plugin is not marked as an instrument");
        return false;
    }

    const auto result = commandStack_.execute (
        std::make_unique<core::commands::AssignTrackInstrumentCommand> (trackId, toTrackInstrumentReference (plugin)));

    if (result.failed())
    {
        reportWarning ("Track instrument assignment failed: " + result.error());
        return false;
    }

    return syncPlaybackAfterDeviceEdit ("Assigned instrument to track: " + (plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name),
                                        "Track instrument assignment failed");
}

bool AppServices::addPluginDeviceToTrack (const std::string& trackId,
                                          const engine::plugins::PluginDescription& plugin,
                                          core::sequencing::PluginKind deviceKind)
{
    if (trackId.empty())
    {
        reportWarning ("Device assignment failed: no track selected");
        return false;
    }

    auto* track = project_.findTrackById (trackId);
    if (track == nullptr)
    {
        reportWarning ("Device assignment failed: selected track was not found");
        return false;
    }

    if (const auto validationError = pluginKindValidationError (plugin, deviceKind))
    {
        reportWarning ("Device assignment failed: " + *validationError);
        return false;
    }

    auto chain = track->deviceChain();
    if (chain.empty() && track->instrument().has_value() && deviceKind == core::sequencing::PluginKind::audioEffect)
        chain.appendSlot (legacyInstrumentSlot (*track->instrument(), chain));

    const auto insertIndex = chain.size();
    auto slot = deviceSlotFromPlugin (plugin, deviceKind, nextDeviceSlotId (chain, plugin));

    std::optional<core::commands::CommandResult> result;
    if (chain.size() == track->deviceChain().size())
    {
        result = commandStack_.execute (
            std::make_unique<core::commands::AddTrackDeviceCommand> (trackId, std::move (slot), insertIndex));
    }
    else
    {
        chain.insertSlot (insertIndex, std::move (slot));
        result = commandStack_.execute (
            std::make_unique<core::commands::SetTrackDeviceChainCommand> (trackId, std::move (chain)));
    }

    if (result->failed())
    {
        reportWarning ("Device assignment failed: " + result->error());
        return false;
    }

    return syncPlaybackAfterDeviceEdit ("Added plugin device to track: " + (plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name),
                                        "Device assignment failed");
}

bool AppServices::addPluginDeviceToTrackByStableId (const std::string& trackId,
                                                    const std::string& pluginStableId,
                                                    core::sequencing::PluginKind deviceKind)
{
    const auto plugin = pluginRegistry_ == nullptr ? std::nullopt : pluginRegistry_->findByStableId (pluginStableId);
    if (! plugin.has_value())
    {
        reportWarning ("Device assignment failed: plugin was not found in the registry");
        return false;
    }

    return addPluginDeviceToTrack (trackId, *plugin, deviceKind);
}

bool AppServices::insertPluginDeviceToTrackByStableId (const std::string& trackId,
                                                       const std::string& pluginStableId,
                                                       core::sequencing::PluginKind deviceKind,
                                                       std::size_t insertIndex)
{
    if (trackId.empty())
    {
        reportWarning ("Device insert failed: no track selected");
        return false;
    }

    auto* track = project_.findTrackById (trackId);
    if (track == nullptr)
    {
        reportWarning ("Device insert failed: selected track was not found");
        return false;
    }

    const auto plugin = pluginRegistry_ == nullptr ? std::nullopt : pluginRegistry_->findByStableId (pluginStableId);
    if (! plugin.has_value())
    {
        reportWarning ("Device insert failed: plugin was not found in the registry");
        return false;
    }

    if (const auto validationError = pluginKindValidationError (*plugin, deviceKind))
    {
        reportWarning ("Device insert failed: " + *validationError);
        return false;
    }

    auto chain = track->deviceChain();
    if (chain.empty() && track->instrument().has_value() && deviceKind == core::sequencing::PluginKind::audioEffect)
        chain.appendSlot (legacyInstrumentSlot (*track->instrument(), chain));

    auto slot = deviceSlotFromPlugin (*plugin, deviceKind, nextDeviceSlotId (chain, *plugin));
    const auto result = [&]() -> core::commands::CommandResult
    {
        if (chain.size() == track->deviceChain().size())
        {
            return commandStack_.execute (
                std::make_unique<core::commands::AddTrackDeviceCommand> (trackId, std::move (slot), insertIndex));
        }

        chain.insertSlot (std::min (insertIndex, chain.size()), std::move (slot));
        return commandStack_.execute (
            std::make_unique<core::commands::SetTrackDeviceChainCommand> (trackId, std::move (chain)));
    }();

    if (result.failed())
    {
        reportWarning ("Device insert failed: " + result.error());
        return false;
    }

    return syncPlaybackAfterDeviceEdit ("Inserted plugin device on track: " + (plugin->name.empty() ? plugin->fileOrIdentifier : plugin->name),
                                        "Device insert failed");
}

bool AppServices::replaceTrackDeviceByStableId (const std::string& trackId,
                                                const core::sequencing::DeviceSlotId& slotId,
                                                const std::string& pluginStableId,
                                                core::sequencing::PluginKind deviceKind)
{
    if (trackId.empty() || slotId.empty())
    {
        reportWarning ("Device replacement failed: no device selected");
        return false;
    }

    auto* track = project_.findTrackById (trackId);
    if (track == nullptr)
    {
        reportWarning ("Device replacement failed: selected track was not found");
        return false;
    }

    const auto plugin = pluginRegistry_ == nullptr ? std::nullopt : pluginRegistry_->findByStableId (pluginStableId);
    if (! plugin.has_value())
    {
        reportWarning ("Device replacement failed: plugin was not found in the registry");
        return false;
    }

    if (const auto validationError = pluginKindValidationError (*plugin, deviceKind))
    {
        reportWarning ("Device replacement failed: " + *validationError);
        return false;
    }

    const auto* previousSlot = track->deviceChain().findSlot (slotId);
    const auto legacyInstrument = track->deviceChain().empty()
        && track->instrument().has_value()
        && slotId.value == "instrument";

    if (previousSlot == nullptr && ! legacyInstrument)
    {
        reportWarning ("Device replacement failed: selected device was not found");
        return false;
    }

    if (legacyInstrument)
    {
        if (deviceKind != core::sequencing::PluginKind::instrument)
        {
            reportWarning ("Device replacement failed: legacy instrument can only be replaced by an instrument");
            return false;
        }

        return assignInstrumentToTrack (trackId, *plugin);
    }

    auto replacement = deviceSlotFromPlugin (*plugin, deviceKind, slotId.value);
    replacement.setBypassed (previousSlot->bypassed());
    if (pluginReferenceMatches (previousSlot->plugin(), *plugin))
        replacement.setPluginStateFile (previousSlot->pluginStateFile());

    const auto result = commandStack_.execute (
        std::make_unique<core::commands::ReplaceTrackDeviceCommand> (trackId, std::move (replacement)));

    if (result.failed())
    {
        reportWarning ("Device replacement failed: " + result.error());
        return false;
    }

    return syncPlaybackAfterDeviceEdit ("Replaced plugin device on track: " + (plugin->name.empty() ? plugin->fileOrIdentifier : plugin->name),
                                        "Device replacement failed");
}

bool AppServices::moveTrackDevice (const std::string& trackId,
                                   const core::sequencing::DeviceSlotId& slotId,
                                   std::size_t targetIndex)
{
    if (trackId.empty() || slotId.empty())
    {
        reportWarning ("Device reorder failed: no device selected");
        return false;
    }

    const auto result = commandStack_.execute (
        std::make_unique<core::commands::MoveTrackDeviceCommand> (trackId, slotId, targetIndex));

    if (result.failed())
    {
        reportWarning ("Device reorder failed: " + result.error());
        return false;
    }

    return syncPlaybackAfterDeviceEdit ("Reordered device slot: " + trackId + "/" + slotId.value,
                                        "Device reorder failed");
}

bool AppServices::removeTrackDevice (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    if (trackId.empty() || slotId.empty())
    {
        reportWarning ("Device remove failed: no device selected");
        return false;
    }

    const auto result = commandStack_.execute (
        std::make_unique<core::commands::RemoveTrackDeviceCommand> (trackId, slotId));

    if (result.failed())
    {
        reportWarning ("Device remove failed: " + result.error());
        return false;
    }

    return syncPlaybackAfterDeviceEdit ("Removed device slot: " + trackId + "/" + slotId.value,
                                        "Device remove failed");
}

bool AppServices::setTrackDeviceBypassed (const std::string& trackId,
                                          const core::sequencing::DeviceSlotId& slotId,
                                          bool bypassed)
{
    if (trackId.empty() || slotId.empty())
    {
        reportWarning ("Device bypass failed: no device selected");
        return false;
    }

    const auto wasPlaybackProjectDirty = playbackProjectDirty_;
    const auto result = commandStack_.execute (
        std::make_unique<core::commands::SetTrackDeviceBypassCommand> (trackId, slotId, bypassed));

    if (result.failed())
    {
        reportWarning ("Device bypass failed: " + result.error());
        return false;
    }

    logger_.info (std::string { bypassed ? "Bypassed" : "Enabled" } + " device slot: " + trackId + "/" + slotId.value);

    if (! wasPlaybackProjectDirty
        && playbackEngine_ != nullptr
        && playbackEngine_->setTrackPluginBypassed (trackId, slotId.value, bypassed))
    {
        playbackProjectDirty_ = false;
        clearUserMessage();
        return true;
    }

    return syncPlaybackAfterDeviceEdit (std::string { bypassed ? "Bypassed" : "Enabled" } + " device slot: " + trackId + "/" + slotId.value,
                                        "Device bypass failed");
}

bool AppServices::openTrackPluginEditor (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId)
{
    if (trackId.empty() || slotId.empty())
    {
        reportWarning ("Plugin editor failed: no device selected");
        return false;
    }

    const auto* track = project_.findTrackById (trackId);
    const auto* slot = track == nullptr ? nullptr : track->deviceChain().findSlot (slotId);
    const auto isLegacyInstrumentSlot = track != nullptr
        && track->deviceChain().empty()
        && track->instrument().has_value()
        && slotId.value == "instrument";

    if (track == nullptr || (slot == nullptr && ! isLegacyInstrumentSlot))
    {
        reportWarning ("Plugin editor failed: selected device was not found");
        return false;
    }

    if (! syncPlaybackProjectIfNeeded())
        return false;

    if (playbackEngine_ == nullptr || ! playbackEngine_->openTrackPluginEditor (trackId, slotId.value))
    {
        reportWarning ("Plugin editor failed: " + (playbackEngine_ == nullptr ? std::string { "playback engine is not available" }
                                                                              : playbackEngine_->getCurrentStatus().message));
        return false;
    }

    logger_.info ("Opened plugin editor: " + trackId + "/" + slotId.value);
    clearUserMessage();
    return true;
}

bool AppServices::insertTrack (core::sequencing::TrackType trackType)
{
    if (trackType == core::sequencing::TrackType::master && project_.masterTrack() != nullptr)
    {
        reportWarning ("Track creation failed: project already has a master track");
        return false;
    }

    core::sequencing::Track track {
        nextTrackId (project_),
        nextTrackName (project_, trackType),
        trackType
    };

    const auto trackId = track.id();
    const auto result = commandStack_.execute (
        std::make_unique<core::commands::AddTrackCommand> (std::move (track)));

    if (result.failed())
    {
        reportWarning ("Track creation failed: " + result.error());
        return false;
    }

    setSelectedTrack (trackId);
    logger_.info ("Inserted " + trackTypePrefix (trackType) + " track: " + trackId);
    clearUserMessage();
    return true;
}

bool AppServices::createTrackFromPlugin (const engine::plugins::PluginDescription& plugin)
{
    const auto pluginName = displayNameForPlugin (plugin);
    auto trackType = core::sequencing::TrackType::audio;
    auto deviceKind = core::sequencing::PluginKind::audioEffect;

    if (plugin.isInstrument)
    {
        trackType = core::sequencing::TrackType::midi;
        deviceKind = core::sequencing::PluginKind::instrument;
    }
    else if (! plugin.isAudioEffect)
    {
        reportWarning ("Track creation failed: selected plugin is not an instrument or audio effect");
        return false;
    }

    try
    {
        core::sequencing::Track track { nextTrackId (project_), pluginName, trackType };
        auto chain = track.deviceChain();

        if (deviceKind == core::sequencing::PluginKind::instrument)
            track.setInstrument (toTrackInstrumentReference (plugin));

        chain.appendSlot (deviceSlotFromPlugin (plugin, deviceKind, nextDeviceSlotId (chain, plugin)));
        track.setDeviceChain (std::move (chain));

        const auto trackId = track.id();
        const auto result = commandStack_.execute (
            std::make_unique<core::commands::AddTrackCommand> (std::move (track)));

        if (result.failed())
        {
            reportWarning ("Track creation failed: " + result.error());
            return false;
        }

        setSelectedTrack (trackId);
        if (trackType == core::sequencing::TrackType::midi)
            setRecordArmedTrack (trackId);

        logger_.info ("Created track from plugin: " + pluginName);
        clearUserMessage();
        return true;
    }
    catch (const std::exception& exception)
    {
        reportWarning ("Track creation failed: " + std::string { exception.what() });
        return false;
    }
}

bool AppServices::createTrackFromPluginStableId (const std::string& pluginStableId)
{
    const auto plugin = pluginRegistry_ == nullptr ? std::nullopt : pluginRegistry_->findByStableId (pluginStableId);
    if (! plugin.has_value())
    {
        reportWarning ("Track creation failed: plugin was not found in the registry");
        return false;
    }

    return createTrackFromPlugin (*plugin);
}

bool AppServices::createAudioTrackFromFile (const std::filesystem::path& filePath, std::string displayName)
{
    std::error_code error;
    if (filePath.empty() || ! std::filesystem::is_regular_file (filePath, error))
    {
        reportWarning ("Audio import failed: file was not found");
        return false;
    }

    if (! isSupportedAudioFile (filePath))
    {
        reportWarning ("Audio import failed: unsupported audio format");
        return false;
    }

    try
    {
        const auto trackName = displayNameForPath (filePath, displayName);
        const auto trackId = nextTrackId (project_);
        const auto clipId = trackId + "-clip-1";

        core::sequencing::Track track { trackId, trackName, core::sequencing::TrackType::audio };
        track.addAudioClip (core::sequencing::AudioClip {
            clipId,
            trackName,
            core::sequencing::AudioSourceReference {
                sourceIdForAudioFile (filePath),
                filePath.string(),
                trackName,
                false
            },
            {},
            defaultTrackCreationClipLength (project_)
        });

        const auto result = commandStack_.execute (
            std::make_unique<core::commands::AddTrackCommand> (std::move (track)));

        if (result.failed())
        {
            reportWarning ("Audio import failed: " + result.error());
            return false;
        }

        setSelectedTrack (trackId);
        logger_.info ("Created audio track from file: " + filePath.string());
        clearUserMessage();
        return true;
    }
    catch (const std::exception& exception)
    {
        reportWarning ("Audio import failed: " + std::string { exception.what() });
        return false;
    }
}

bool AppServices::createMidiTrackFromFile (const std::filesystem::path& filePath, std::string displayName)
{
    std::error_code error;
    if (filePath.empty() || ! std::filesystem::is_regular_file (filePath, error))
    {
        reportWarning ("MIDI import failed: file was not found");
        return false;
    }

    if (! isSupportedMidiFile (filePath))
    {
        reportWarning ("MIDI import failed: unsupported MIDI file extension");
        return false;
    }

    try
    {
        const auto trackName = displayNameForPath (filePath, displayName);
        const auto trackId = nextTrackId (project_);
        const auto clipId = trackId + "-clip-1";
        auto importResult = core::midi::MidiImporter::importClipFromFile (filePath,
                                                                          core::midi::MidiImportOptions {
                                                                              clipId,
                                                                              trackName,
                                                                              {}
                                                                          });

        core::sequencing::Track track { trackId, trackName, core::sequencing::TrackType::midi };
        track.addClip (std::move (importResult.clip));

        const auto result = commandStack_.execute (
            std::make_unique<core::commands::AddTrackCommand> (std::move (track)));

        if (result.failed())
        {
            reportWarning ("MIDI import failed: " + result.error());
            return false;
        }

        setSelectedTrack (trackId);
        logger_.info ("Created MIDI track from file: " + filePath.string()
                      + " (" + std::to_string (importResult.importedNoteCount) + " notes)");
        clearUserMessage();
        return true;
    }
    catch (const std::exception& exception)
    {
        reportWarning ("MIDI import failed: " + std::string { exception.what() });
        return false;
    }
}

bool AppServices::syncPlaybackProjectIfNeeded()
{
    core::diagnostics::ScopedPerformanceTimer timer { "AppServices::syncPlaybackProjectIfNeeded" };

    if (playbackEngine_ == nullptr)
        return false;

    if (! playbackProjectDirty_)
        return true;

    tracePluginState ("app syncPlaybackProjectIfNeeded begin");
    if (! playbackEngine_->syncProject (project_))
    {
        tracePluginState ("app syncPlaybackProjectIfNeeded failed");
        reportWarning ("Project playback sync failed: " + playbackEngine_->getCurrentStatus().message);
        return false;
    }

    playbackProjectDirty_ = false;
    logger_.info ("Project playback synced: " + playbackEngine_->getCurrentStatus().message);
    tracePluginState ("app syncPlaybackProjectIfNeeded end");
    return true;
}

bool AppServices::startProjectPlayback()
{
    tracePluginState ("app startProjectPlayback begin");
    if (! syncPlaybackProjectIfNeeded())
        return false;

    if (! playbackEngine_->startPlayback())
    {
        tracePluginState ("app startProjectPlayback failed");
        reportWarning ("Project playback start failed: " + playbackEngine_->getCurrentStatus().message);
        return false;
    }

    logger_.info ("Project playback started");
    tracePluginState ("app startProjectPlayback end");
    clearUserMessage();
    return true;
}

void AppServices::stopProjectPlayback()
{
    if (playbackEngine_ != nullptr)
        playbackEngine_->stopPlayback();
}

core::time::TickPosition AppServices::playbackPlayheadPosition() const
{
    if (playbackEngine_ == nullptr)
        return {};

    return playbackEngine_->getPlayheadPosition();
}

bool AppServices::setPlaybackPlayheadPosition (core::time::TickPosition position)
{
    if (playbackEngine_ == nullptr)
        return false;

    tracePluginState ("app setPlaybackPlayheadPosition begin tick=" + std::to_string (position.ticks()));
    const auto moved = playbackEngine_->setPlayheadPosition (position);
    tracePluginState (moved ? "app setPlaybackPlayheadPosition end" : "app setPlaybackPlayheadPosition failed");
    return moved;
}

bool AppServices::returnPlaybackToStart()
{
    if (playbackEngine_ == nullptr)
        return false;

    tracePluginState ("app returnPlaybackToStart begin");
    const auto returned = playbackEngine_->returnToStart();
    tracePluginState (returned ? "app returnPlaybackToStart end" : "app returnPlaybackToStart failed");
    return returned;
}

bool AppServices::setPlaybackLoopEnabled (bool enabled)
{
    if (playbackEngine_ == nullptr)
        return false;

    return playbackEngine_->setLoopEnabled (enabled);
}

bool AppServices::isPlaybackLoopEnabled() const
{
    return playbackEngine_ != nullptr && playbackEngine_->isLoopEnabled();
}

void AppServices::markPlaybackProjectDirty() noexcept
{
    core::diagnostics::ScopedPerformanceTimer timer { "AppServices::markPlaybackProjectDirty" };

    tracePluginState ("app markPlaybackProjectDirty begin");
    playbackProjectDirty_ = true;
    restoreObservedPluginParameterStateSoon();
    tracePluginState ("app markPlaybackProjectDirty end");
}

void AppServices::observeLivePluginParameterState() noexcept
{
    try
    {
        if (playbackEngine_ != nullptr)
            playbackEngine_->observeLivePluginParameterState();
    }
    catch (...)
    {
    }
}

void AppServices::restoreObservedPluginParameterStateSoon() noexcept
{
    try
    {
        core::diagnostics::ScopedPerformanceTimer timer { "AppServices::restoreObservedPluginParameterStateSoon" };

        if (playbackEngine_ != nullptr)
        {
            tracePluginState ("app restoreObservedPluginParameterStateSoon begin");
            playbackEngine_->restoreObservedPluginParameterStateSoon();
            tracePluginState ("app restoreObservedPluginParameterStateSoon end");
        }
    }
    catch (...)
    {
    }
}

std::vector<MidiInputDeviceInfo> AppServices::availableMidiInputDevices() const
{
    return midiInputRecordingService_ == nullptr ? std::vector<MidiInputDeviceInfo> {} : midiInputRecordingService_->availableInputDevices();
}

bool AppServices::selectMidiInputDevice (const std::string& identifier)
{
    if (midiInputRecordingService_ == nullptr)
        return false;

    if (! midiInputRecordingService_->openInputDevice (identifier))
    {
        reportWarning ("MIDI input open failed");
        return false;
    }

    logger_.info ("MIDI input selected: " + midiInputRecordingService_->selectedInputName());
    clearUserMessage();
    return true;
}

void AppServices::closeMidiInputDevice()
{
    if (midiInputRecordingService_ != nullptr)
        midiInputRecordingService_->closeInputDevice();
}

const std::string& AppServices::selectedMidiInputIdentifier() const noexcept
{
    static const std::string empty;
    return midiInputRecordingService_ == nullptr ? empty : midiInputRecordingService_->selectedInputIdentifier();
}

const std::string& AppServices::selectedMidiInputName() const noexcept
{
    static const std::string empty;
    return midiInputRecordingService_ == nullptr ? empty : midiInputRecordingService_->selectedInputName();
}

bool AppServices::hasSelectedMidiInputDevice() const noexcept
{
    return midiInputRecordingService_ != nullptr && midiInputRecordingService_->hasOpenInputDevice();
}

bool AppServices::setMidiRecordingEnabled (bool enabled)
{
    if (enabled == midiRecordingEnabled_)
        return true;

    if (enabled)
    {
        if (! hasSelectedMidiInputDevice())
        {
            const auto devices = availableMidiInputDevices();
            if (devices.empty())
            {
                reportWarning ("MIDI recording cannot start: no MIDI input devices found");
                return false;
            }

            if (! selectMidiInputDevice (devices.front().identifier))
                return false;
        }

        if (! recordArmedTrackId_.has_value())
        {
            reportWarning ("MIDI recording cannot start: no track is record-armed");
            return false;
        }

        midiRecordingEnabled_ = true;
        recordingClockInitialized_ = false;
        recordingStartTick_ = playbackPlayheadPosition();
        activeRecordedNotes_.clear();
        logger_.info ("MIDI recording enabled");
        clearUserMessage();
        return true;
    }

    midiRecordingEnabled_ = false;
    recordingClockInitialized_ = false;
    activeRecordedNotes_.clear();
    activeRecordingClip_.reset();
    logger_.info ("MIDI recording disabled");
    return true;
}

bool AppServices::midiRecordingEnabled() const noexcept
{
    return midiRecordingEnabled_;
}

void AppServices::setInputQuantizationEnabled (bool enabled) noexcept
{
    inputQuantizationEnabled_ = enabled;
}

bool AppServices::inputQuantizationEnabled() const noexcept
{
    return inputQuantizationEnabled_;
}

void AppServices::setScaleLockMode (core::sequencing::ScaleLockMode mode) noexcept
{
    scaleLockMode_ = mode;
}

core::sequencing::ScaleLockMode AppServices::scaleLockMode() const noexcept
{
    return scaleLockMode_;
}

void AppServices::processMidiRecordingEvents()
{
    if (midiInputRecordingService_ == nullptr)
        return;

    auto events = midiInputRecordingService_->drainEvents();
    if (! midiRecordingEnabled_)
        return;

    std::stable_sort (events.begin(), events.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.timestampSeconds < rhs.timestampSeconds;
    });

    for (const auto& event : events)
    {
        if (! ensureRecordingClockInitialized (event.timestampSeconds))
            return;

        if (event.type == QueuedMidiInputEvent::Type::noteOn)
            handleRecordedNoteOn (event);
        else
            handleRecordedNoteOff (event);
    }
}

void AppServices::setRecordArmedTrack (std::string trackId)
{
    if (project_.findTrackById (trackId) == nullptr)
    {
        reportWarning ("Cannot arm missing track for MIDI recording: " + trackId);
        return;
    }

    recordArmedTrackId_ = std::move (trackId);
    activeRecordingClip_.reset();
}

void AppServices::clearRecordArmedTrack()
{
    recordArmedTrackId_.reset();
    activeRecordingClip_.reset();
}

bool AppServices::isTrackRecordArmed (const std::string& trackId) const noexcept
{
    return recordArmedTrackId_.has_value() && *recordArmedTrackId_ == trackId;
}

const std::optional<std::string>& AppServices::recordArmedTrackId() const noexcept
{
    return recordArmedTrackId_;
}

void AppServices::setSelectedTrack (std::string trackId)
{
    if (project_.findTrackById (trackId) == nullptr)
    {
        reportWarning ("Track selection failed: track was not found");
        return;
    }

    selectedTrackId_ = std::move (trackId);
}

void AppServices::clearSelectedTrack() noexcept
{
    selectedTrackId_.reset();
}

const std::optional<std::string>& AppServices::selectedTrackId() const noexcept
{
    return selectedTrackId_;
}

void AppServices::setSelectedRecordingClip (std::string trackId, std::string clipId)
{
    selectedTrackId_ = trackId;
    selectedRecordingClip_ = RecordingClipSelection { std::move (trackId), std::move (clipId) };
    activeRecordingClip_.reset();
}

void AppServices::clearSelectedRecordingClip()
{
    selectedRecordingClip_.reset();
    activeRecordingClip_.reset();
}

std::string AppServices::midiRecordingStatusText() const
{
    if (midiRecordingEnabled_)
        return "MIDI Recording / " + (selectedMidiInputName().empty() ? std::string { "Input open" } : selectedMidiInputName());

    if (hasSelectedMidiInputDevice())
        return "MIDI Input / " + selectedMidiInputName();

    return "MIDI Input / None";
}

bool AppServices::syncPlaybackAfterDeviceEdit (const std::string& successMessage, const std::string& failurePrefix)
{
    logger_.info (successMessage);
    if (syncPlaybackProjectIfNeeded())
    {
        clearUserMessage();
        return true;
    }

    const auto syncError = lastUserMessage_.empty()
        ? (playbackEngine_ == nullptr ? std::string { "playback engine is not available" } : playbackEngine_->getCurrentStatus().message)
        : lastUserMessage_;

    const auto rollback = commandStack_.rollbackLastExecuted();
    if (rollback.failed())
    {
        reportWarning (failurePrefix + ": playback sync failed (" + syncError + "); rollback failed: " + rollback.error());
        return false;
    }

    const auto restored = syncPlaybackProjectIfNeeded();
    auto message = failurePrefix + ": playback sync failed (" + syncError + "); edit was rolled back";
    if (! restored)
    {
        const auto restoreError = lastUserMessage_.empty()
            ? (playbackEngine_ == nullptr ? std::string { "playback engine is not available" } : playbackEngine_->getCurrentStatus().message)
            : lastUserMessage_;
        message += "; restoring playback also failed (" + restoreError + ")";
    }

    reportWarning (std::move (message));
    return false;
}

core::time::TickPosition AppServices::recordingTickForTimestamp (double timestampSeconds) const
{
    const auto offsetSeconds = std::max (0.0, timestampSeconds - recordingStartSeconds_);
    const auto projectStartSeconds = project_.tempoMap().secondsAt (recordingStartTick_);
    return project_.tempoMap().tickAtSeconds (projectStartSeconds + offsetSeconds);
}

core::music_theory::ScaleLibrary AppServices::scaleLibraryForRecording() const
{
    auto library = core::music_theory::ScaleLibrary::createBuiltInLibrary();
    for (const auto& customScale : project_.customScales())
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

bool AppServices::ensureRecordingClockInitialized (double timestampSeconds)
{
    if (recordingClockInitialized_)
        return true;

    recordingStartSeconds_ = timestampSeconds;
    recordingStartTick_ = playbackPlayheadPosition();
    recordingClockInitialized_ = true;
    return true;
}

std::optional<AppServices::RecordingClipSelection> AppServices::ensureRecordingClipFor (core::time::TickPosition projectTick)
{
    if (! recordArmedTrackId_.has_value())
        return std::nullopt;

    if (activeRecordingClip_.has_value())
    {
        if (const auto* track = project_.findTrackById (activeRecordingClip_->trackId))
        {
            if (track->findClipById (activeRecordingClip_->clipId) != nullptr)
                return activeRecordingClip_;
        }
    }

    if (selectedRecordingClip_.has_value() && selectedRecordingClip_->trackId == *recordArmedTrackId_)
    {
        if (const auto* track = project_.findTrackById (selectedRecordingClip_->trackId))
        {
            if (track->findClipById (selectedRecordingClip_->clipId) != nullptr)
            {
                activeRecordingClip_ = selectedRecordingClip_;
                return activeRecordingClip_;
            }
        }
    }

    auto* track = project_.findTrackById (*recordArmedTrackId_);
    if (track == nullptr)
        return std::nullopt;

    const auto clipLength = core::time::TickDuration::fromTicks (
        static_cast<std::int64_t> (defaultRecordingClipBars * beatsPerBar) * core::time::ticksPerQuarterNote);
    auto clip = core::sequencing::MidiClip {
        nextRecordingClipId (*track),
        "MIDI Recording",
        projectTick,
        clipLength
    };
    const auto clipId = clip.id();

    const auto result = commandStack_.execute (std::make_unique<core::commands::AddClipCommand> (*recordArmedTrackId_, std::move (clip)));
    if (result.failed())
    {
        reportWarning ("MIDI recording clip creation failed: " + result.error());
        return std::nullopt;
    }

    activeRecordingClip_ = RecordingClipSelection { *recordArmedTrackId_, clipId };
    selectedRecordingClip_ = activeRecordingClip_;
    logger_.info ("Created MIDI recording clip on armed track");
    return activeRecordingClip_;
}

bool AppServices::ensureClipContainsLocalEnd (const RecordingClipSelection& selection, core::time::TickPosition localEnd)
{
    auto* track = project_.findTrackById (selection.trackId);
    auto* clip = track == nullptr ? nullptr : track->findClipById (selection.clipId);
    if (clip == nullptr)
        return false;

    const auto requiredTicks = localEnd.ticks();
    if (requiredTicks <= clip->sourceLength().ticks())
        return true;

    const auto newLength = core::time::TickDuration::fromTicks (requiredTicks);
    const auto result = commandStack_.execute (std::make_unique<core::commands::ResizeClipCommand> (selection.trackId, selection.clipId, newLength));
    if (result.failed())
    {
        reportWarning ("MIDI recording clip extend failed: " + result.error());
        return false;
    }

    return true;
}

void AppServices::handleRecordedNoteOn (const QueuedMidiInputEvent& event)
{
    const auto exactProjectTick = recordingTickForTimestamp (event.timestampSeconds);
    const auto startProjectTick = inputQuantizationEnabled_
        ? core::sequencing::quantizeInputStart (exactProjectTick,
                                                project_.rhythmSettings().currentGridDivisionId(),
                                                project_.rhythmSettings())
        : exactProjectTick;
    const auto selection = ensureRecordingClipFor (startProjectTick);
    if (! selection.has_value())
        return;

    const auto* track = project_.findTrackById (selection->trackId);
    const auto* clip = track == nullptr ? nullptr : track->findClipById (selection->clipId);
    if (clip == nullptr)
        return;

    const auto localStart = core::time::TickPosition::fromTicks (std::max<std::int64_t> (0, startProjectTick.ticks() - clip->startInProject().ticks()));
    const auto performedPitch = core::music_theory::MidiPitch::fromValue (event.noteNumber);
    const core::sequencing::HarmonicContextResolver resolver { project_.musicalStructure() };
    const auto harmonicContext = resolver.resolveAt (exactProjectTick);
    const auto scaleLibrary = scaleLibraryForRecording();
    const auto recordedPitch = core::sequencing::applyScaleLock (performedPitch, harmonicContext, scaleLibrary, scaleLockMode_);

    activeRecordedNotes_[midiEventKey (event.channel, event.noteNumber)] = ActiveRecordedNote {
        selection->trackId,
        selection->clipId,
        localStart,
        recordedPitch.value(),
        std::max (1, event.velocity),
        core::sequencing::spellingForRecordedPitch (recordedPitch, harmonicContext, scaleLibrary)
    };
}

void AppServices::handleRecordedNoteOff (const QueuedMidiInputEvent& event)
{
    const auto key = midiEventKey (event.channel, event.noteNumber);
    const auto match = activeRecordedNotes_.find (key);
    if (match == activeRecordedNotes_.end())
        return;

    auto activeNote = match->second;
    activeRecordedNotes_.erase (match);

    const auto projectTick = recordingTickForTimestamp (event.timestampSeconds);
    const auto* track = project_.findTrackById (activeNote.trackId);
    const auto* clip = track == nullptr ? nullptr : track->findClipById (activeNote.clipId);
    if (clip == nullptr)
        return;

    auto localEndTicks = projectTick.ticks() - clip->startInProject().ticks();
    localEndTicks = std::max (activeNote.start.ticks() + 1, localEndTicks);
    const auto localEnd = core::time::TickPosition::fromTicks (localEndTicks);
    const auto duration = localEnd - activeNote.start;
    const auto selection = RecordingClipSelection { activeNote.trackId, activeNote.clipId };

    if (! ensureClipContainsLocalEnd (selection, localEnd))
        return;

    auto* mutableClip = project_.findTrackById (activeNote.trackId)->findClipById (activeNote.clipId);
    auto noteIndex = mutableClip->notes().size() + 1;
    std::string noteId;
    do
    {
        noteId = "recorded-note-" + std::to_string (noteIndex++);
    }
    while (mutableClip->findNoteById (noteId) != nullptr);

    const auto result = commandStack_.execute (std::make_unique<core::commands::AddNoteCommand> (
        activeNote.trackId,
        activeNote.clipId,
        core::sequencing::MidiNote {
            noteId,
            core::music_theory::MidiPitch::fromValue (activeNote.midiNote),
            activeNote.start,
            duration,
            activeNote.velocity,
            activeNote.spelling
        }));

    if (result.failed())
        reportWarning ("MIDI recorded note add failed: " + result.error());
}

void AppServices::resetToDefaultProject()
{
    project_ = core::sequencing::Project { "default-project", "Untitled Song" };

    try
    {
        project_.addTrack (core::sequencing::Track { "track-1", "MIDI 1" });
        selectedTrackId_ = "track-1";
    }
    catch (const std::exception& error)
    {
        reportWarning ("Default track creation failed: " + std::string { error.what() });
        selectedTrackId_.reset();
    }

    activeRecordedNotes_.clear();
    selectedRecordingClip_.reset();
    activeRecordingClip_.reset();
    recordArmedTrackId_.reset();
    recordingClockInitialized_ = false;
}

core::sequencing::Project AppServices::projectPreparedForSave (const std::vector<engine::TrackPluginState>& pluginStates) const
{
    auto projectToSave = project_;
    std::unordered_map<std::string, bool> stateByKey;

    for (const auto& state : pluginStates)
        stateByKey[state.trackId] = true;

    for (const auto& track : project_.tracks())
    {
        if (auto* targetTrack = projectToSave.findTrackById (track.id()))
        {
            if (track.instrument().has_value())
            {
                auto instrument = *track.instrument();
                const auto legacyInstrumentKey = pluginStateKeyForDevice (track.id(), "instrument");

                if (instrument.pluginStateFile.empty()
                    || stateByKey.find (track.id()) != stateByKey.end()
                    || stateByKey.find (legacyInstrumentKey) != stateByKey.end())
                    instrument.pluginStateFile = pluginStateFileNameForTrack (track.id());

                targetTrack->setInstrument (std::move (instrument));
            }

            auto deviceChain = track.deviceChain();
            auto changedDeviceChain = false;
            for (const auto& slot : track.deviceChain().slots())
            {
                auto replacement = slot;
                const auto stateKey = pluginStateKeyForDevice (track.id(), slot.id().value);

                if (replacement.pluginStateFile().empty() || stateByKey.find (stateKey) != stateByKey.end())
                {
                    replacement.setPluginStateFile (pluginStateFileNameForDevice (track.id(), slot.id().value));
                    deviceChain.replaceSlot (slot.id(), std::move (replacement));
                    changedDeviceChain = true;
                }
            }

            if (changedDeviceChain)
                targetTrack->setDeviceChain (std::move (deviceChain));
        }
    }

    return projectToSave;
}

bool AppServices::writePluginStateFiles (const std::filesystem::path& packagePath,
                                         const core::sequencing::Project& projectToSave,
                                         const std::vector<engine::TrackPluginState>& pluginStates)
{
    std::unordered_map<std::string, std::vector<std::uint8_t>> stateByKey;
    for (const auto& state : pluginStates)
        stateByKey[state.trackId] = state.data;

    try
    {
        std::set<std::string> writtenRelativePaths;

        const auto stateDataForKeys = [&stateByKey] (const std::string& primaryKey,
                                                     const std::string& fallbackKey) -> const std::vector<std::uint8_t>*
        {
            if (const auto state = stateByKey.find (primaryKey); state != stateByKey.end())
                return &state->second;

            if (! fallbackKey.empty())
                if (const auto state = stateByKey.find (fallbackKey); state != stateByKey.end())
                    return &state->second;

            return nullptr;
        };

        const auto writeStateFile = [this, &packagePath, &writtenRelativePaths] (
            const std::string& relativePathText,
            const std::vector<std::uint8_t>* stateData) -> bool
        {
            const auto relativePath = std::filesystem::path { relativePathText };
            if (relativePath.is_absolute())
            {
                reportWarning ("Skipped unsafe plugin state path: " + relativePathText);
                return true;
            }

            auto unsafe = false;
            for (const auto& part : relativePath)
                unsafe = unsafe || part == "..";

            if (unsafe)
            {
                reportWarning ("Skipped unsafe plugin state path: " + relativePathText);
                return true;
            }

            const auto relativePathKey = relativePath.generic_string();
            if (! writtenRelativePaths.insert (relativePathKey).second)
                return true;

            const auto outputPath = packagePath / relativePath;
            std::filesystem::create_directories (outputPath.parent_path());

            std::ofstream stream { outputPath, std::ios::binary };
            if (! stream)
            {
                reportError ("Could not write plugin state file: " + outputPath.string());
                return false;
            }

            if (stateData != nullptr && ! stateData->empty())
            {
                stream.write (reinterpret_cast<const char*> (stateData->data()),
                              static_cast<std::streamsize> (stateData->size()));
            }

            return true;
        };

        for (const auto& track : projectToSave.tracks())
        {
            if (track.instrument().has_value() && ! track.instrument()->pluginStateFile.empty())
            {
                const auto stateData = stateDataForKeys (pluginStateKeyForDevice (track.id(), "instrument"), track.id());
                if (! writeStateFile (track.instrument()->pluginStateFile, stateData))
                    return false;
            }

            for (const auto& slot : track.deviceChain().slots())
            {
                if (slot.pluginStateFile().empty())
                    continue;

                const auto stateData = stateDataForKeys (pluginStateKeyForDevice (track.id(), slot.id().value), {});
                if (! writeStateFile (slot.pluginStateFile(), stateData))
                    return false;
            }
        }
    }
    catch (const std::exception& error)
    {
        reportError ("Plugin state save failed: " + std::string { error.what() });
        return false;
    }

    return true;
}

void AppServices::warnAboutMissingPlugins()
{
    const auto plugins = pluginRegistry_ == nullptr ? std::vector<engine::plugins::PluginDescription> {} : pluginRegistry_->plugins();
    std::set<std::string> reported;

    for (const auto& track : project_.tracks())
    {
        if (track.instrument().has_value())
        {
            const auto& instrument = *track.instrument();
            const auto pluginReference = core::sequencing::PluginReference::fromTrackInstrumentReference (*track.instrument());
            const auto found = std::any_of (plugins.begin(),
                                            plugins.end(),
                                            [&instrument] (const auto& plugin)
                                            {
                                                return pluginReferenceMatches (instrument, plugin);
                                            });

            if (! found && reported.insert (pluginWarningKey (track.id(), pluginReference)).second)
                reportWarning ("Missing plugin for track '" + track.name() + "': " + pluginDisplayName (pluginReference));
        }

        for (const auto& slot : track.deviceChain().slots())
        {
            const auto& pluginReference = slot.plugin();
            const auto found = std::any_of (plugins.begin(),
                                            plugins.end(),
                                            [&pluginReference] (const auto& plugin)
                                            {
                                                return pluginReferenceMatches (pluginReference, plugin);
                                            });

            if (! found && reported.insert (pluginWarningKey (track.id(), pluginReference)).second)
                reportWarning ("Missing plugin for track '" + track.name() + "': " + pluginDisplayName (pluginReference));
        }
    }
}

engine::plugins::PluginRegistry& AppServices::pluginRegistry() noexcept
{
    return *pluginRegistry_;
}

const engine::plugins::PluginRegistry& AppServices::pluginRegistry() const noexcept
{
    return *pluginRegistry_;
}

engine::plugins::PluginScanService& AppServices::pluginScanService() noexcept
{
    return *pluginScanService_;
}

const engine::plugins::PluginScanService& AppServices::pluginScanService() const noexcept
{
    return *pluginScanService_;
}

core::sequencing::Project& AppServices::project() noexcept
{
    return project_;
}

const core::sequencing::Project& AppServices::project() const noexcept
{
    return project_;
}

core::commands::CommandStack& AppServices::commandStack() noexcept
{
    return commandStack_;
}

const core::commands::CommandStack& AppServices::commandStack() const noexcept
{
    return commandStack_;
}

core::time::Tempo AppServices::defaultTempo() const noexcept
{
    return defaultTempo_;
}

core::time::TimeSignature AppServices::defaultTimeSignature() const noexcept
{
    return defaultTimeSignature_;
}

std::string_view AppServices::buildType() const noexcept
{
    return buildType_;
}

std::string_view AppServices::platformString() const noexcept
{
    return platformString_;
}

std::string AppServices::diagnosticsLogFilePath() const
{
    return appSettingsService_ == nullptr ? std::string {} : appSettingsService_->diagnosticsLogFilePath();
}

std::vector<std::string> AppServices::diagnosticLines() const
{
    auto lines = logger_.formattedEntries();
    const auto logPath = diagnosticsLogFilePath();
    if (! logPath.empty())
        lines.insert (lines.begin(), "[info] Diagnostics log file: " + logPath);

    if (playbackEngine_ != nullptr)
    {
        const auto pluginLines = playbackEngine_->debugPluginParameterStateLines ("diagnostics panel refresh");
        for (const auto& line : pluginLines)
            lines.push_back ("[debug] " + line);
    }

    return lines;
}

}
