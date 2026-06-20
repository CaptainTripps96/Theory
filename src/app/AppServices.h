#pragma once

#include "app/AppSettings.h"
#include "app/MidiInputTypes.h"
#include "core/commands/CommandStack.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/music_theory/NoteName.h"
#include "core/diagnostics/Logger.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/RecordingInputTransform.h"
#include "core/time/Tempo.h"
#include "core/time/TimeSignature.h"
#include "engine/EngineTypes.h"

#include <filesystem>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::engine
{
class PlaybackEngine;

namespace plugins
{
struct PluginDescription;
class PluginRegistry;
class PluginScanService;
}
}

namespace tsq::app
{
class AppSettingsService;
class MidiInputRecordingService;

class AppServices final
{
public:
    AppServices();
    ~AppServices();

    core::diagnostics::Logger& logger() noexcept;
    const core::diagnostics::Logger& logger() const noexcept;
    void tracePluginState (std::string_view event) noexcept;
    std::string_view lastUserMessage() const noexcept;
    void clearUserMessage() noexcept;
    void reportWarning (std::string message);
    void reportError (std::string message);
    engine::PlaybackEngine& playbackEngine() noexcept;
    const engine::PlaybackEngine& playbackEngine() const noexcept;
    const AppSettings& appSettings() const noexcept;
    bool persistAudioSettings();
    bool newProject();
    bool saveProject();
    bool saveProjectAs (const std::filesystem::path& packagePath);
    bool loadProject (const std::filesystem::path& packagePath);
    const std::optional<std::filesystem::path>& currentProjectPackagePath() const noexcept;
    bool loadTestInstrument (const engine::plugins::PluginDescription& plugin);
    bool assignInstrumentToTrack (const std::string& trackId, const engine::plugins::PluginDescription& plugin);
    bool addPluginDeviceToTrack (const std::string& trackId,
                                 const engine::plugins::PluginDescription& plugin,
                                 core::sequencing::PluginKind deviceKind);
    bool addPluginDeviceToTrackByStableId (const std::string& trackId,
                                           const std::string& pluginStableId,
                                           core::sequencing::PluginKind deviceKind);
    bool insertPluginDeviceToTrackByStableId (const std::string& trackId,
                                              const std::string& pluginStableId,
                                              core::sequencing::PluginKind deviceKind,
                                              std::size_t insertIndex);
    bool insertFirstPartyDeviceToTrack (const std::string& trackId,
                                        const std::string& deviceTypeId,
                                        std::size_t insertIndex);
    bool replaceTrackDeviceByStableId (const std::string& trackId,
                                       const core::sequencing::DeviceSlotId& slotId,
                                       const std::string& pluginStableId,
                                       core::sequencing::PluginKind deviceKind);
    bool moveTrackDevice (const std::string& trackId,
                          const core::sequencing::DeviceSlotId& slotId,
                          std::size_t targetIndex);
    bool removeTrackDevice (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId);
    bool setTrackDeviceBypassed (const std::string& trackId,
                                 const core::sequencing::DeviceSlotId& slotId,
                                 bool bypassed);
    bool setFirstPartyDeviceParameterNormalized (const std::string& trackId,
                                                 const core::sequencing::DeviceSlotId& slotId,
                                                 const std::string& parameterId,
                                                 double normalizedValue);
    bool setClipExpressionState (const std::string& trackId,
                                 const std::string& clipId,
                                 core::sequencing::ExpressionState expressionState);
    bool createExpressionLane (const std::string& trackId,
                               const std::string& clipId,
                               core::sequencing::ExpressionLane lane);
    bool renameExpressionLane (const std::string& trackId,
                               const std::string& clipId,
                               core::sequencing::ExpressionLaneId laneId,
                               std::string name);
    bool setExpressionLaneEnabled (const std::string& trackId,
                                   const std::string& clipId,
                                   core::sequencing::ExpressionLaneId laneId,
                                   bool enabled);
    bool setExpressionLanePolarity (const std::string& trackId,
                                    const std::string& clipId,
                                    core::sequencing::ExpressionLaneId laneId,
                                    core::sequencing::ExpressionLanePolarity polarity);
    bool addExpressionRoute (const std::string& trackId,
                             const std::string& clipId,
                             core::sequencing::ExpressionLaneId laneId,
                             core::sequencing::ExpressionRoute route);
    bool removeExpressionRoute (const std::string& trackId,
                                const std::string& clipId,
                                core::sequencing::ExpressionLaneId laneId,
                                core::sequencing::ExpressionRouteId routeId);
    bool addPhraseEnvelopeClip (const std::string& trackId,
                                const std::string& clipId,
                                core::sequencing::ExpressionLaneId laneId,
                                core::sequencing::PhraseEnvelopeClip envelope);
    bool replacePhraseEnvelopeClip (const std::string& trackId,
                                    const std::string& clipId,
                                    core::sequencing::ExpressionLaneId laneId,
                                    std::optional<core::sequencing::ExpressionClipId> previousEnvelopeId,
                                    core::sequencing::PhraseEnvelopeClip envelope);
    bool removePhraseEnvelopeClip (const std::string& trackId,
                                   const std::string& clipId,
                                   core::sequencing::ExpressionLaneId laneId,
                                   core::sequencing::ExpressionClipId envelopeId);
    bool addCyclicExpressionClip (const std::string& trackId,
                                  const std::string& clipId,
                                  core::sequencing::ExpressionLaneId laneId,
                                  core::sequencing::CyclicExpressionClip cyclic);
    bool replaceCyclicExpressionClip (const std::string& trackId,
                                      const std::string& clipId,
                                      core::sequencing::ExpressionLaneId laneId,
                                      std::optional<core::sequencing::ExpressionClipId> previousCyclicId,
                                      core::sequencing::CyclicExpressionClip cyclic);
    bool removeCyclicExpressionClip (const std::string& trackId,
                                     const std::string& clipId,
                                     core::sequencing::ExpressionLaneId laneId,
                                     core::sequencing::ExpressionClipId cyclicId);
    bool addPitchSlur (const std::string& trackId,
                       const std::string& clipId,
                       core::sequencing::ExpressionLaneId laneId,
                       core::sequencing::PitchSlur slur);
    bool addPitchSlurs (const std::string& trackId,
                        const std::string& clipId,
                        core::sequencing::ExpressionLaneId laneId,
                        std::vector<core::sequencing::PitchSlur> slurs);
    bool replacePitchSlurs (const std::string& trackId,
                            const std::string& clipId,
                            core::sequencing::ExpressionLaneId laneId,
                            std::vector<core::sequencing::PitchSlur> slurs);
    bool removePitchSlur (const std::string& trackId,
                          const std::string& clipId,
                          core::sequencing::ExpressionLaneId laneId,
                          core::sequencing::ExpressionClipId slurId);
    bool addVibratoExpression (const std::string& trackId,
                               const std::string& clipId,
                               core::sequencing::ExpressionLaneId laneId,
                               core::sequencing::VibratoExpression vibrato);
    bool replaceVibratoExpression (const std::string& trackId,
                                   const std::string& clipId,
                                   core::sequencing::ExpressionLaneId laneId,
                                   core::sequencing::VibratoExpression vibrato);
    bool removeVibratoExpression (const std::string& trackId,
                                  const std::string& clipId,
                                  core::sequencing::ExpressionLaneId laneId,
                                  core::sequencing::ExpressionClipId vibratoId);
    bool openTrackPluginEditor (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId);
    bool insertTrack (core::sequencing::TrackType trackType);
    bool createTrackFromPlugin (const engine::plugins::PluginDescription& plugin);
    bool createTrackFromPluginStableId (const std::string& pluginStableId);
    bool createAudioTrackFromFile (const std::filesystem::path& filePath, std::string displayName = {});
    bool createMidiTrackFromFile (const std::filesystem::path& filePath, std::string displayName = {});
    bool syncPlaybackProjectIfNeeded();
    bool startProjectPlayback();
    void stopProjectPlayback();
    core::time::TickPosition playbackPlayheadPosition() const;
    bool setPlaybackPlayheadPosition (core::time::TickPosition position);
    bool returnPlaybackToStart();
    bool setPlaybackLoopEnabled (bool enabled);
    bool isPlaybackLoopEnabled() const;
    void markPlaybackProjectDirty() noexcept;
    void markPlaybackProjectDirty (core::commands::PlaybackSyncCategory category) noexcept;
    bool playbackProjectDirty() const noexcept;
    core::commands::PlaybackSyncCategory playbackProjectDirtyCategories() const noexcept;
    void observeLivePluginParameterState() noexcept;
    void restoreObservedPluginParameterStateSoon() noexcept;
    std::vector<MidiInputDeviceInfo> availableMidiInputDevices() const;
    bool selectMidiInputDevice (const std::string& identifier);
    void closeMidiInputDevice();
    const std::string& selectedMidiInputIdentifier() const noexcept;
    const std::string& selectedMidiInputName() const noexcept;
    bool hasSelectedMidiInputDevice() const noexcept;
    bool setMidiRecordingEnabled (bool enabled);
    bool midiRecordingEnabled() const noexcept;
    void setInputQuantizationEnabled (bool enabled) noexcept;
    bool inputQuantizationEnabled() const noexcept;
    void setScaleLockMode (core::sequencing::ScaleLockMode mode) noexcept;
    core::sequencing::ScaleLockMode scaleLockMode() const noexcept;
    void processMidiRecordingEvents();
    void setRecordArmedTrack (std::string trackId);
    void clearRecordArmedTrack();
    bool isTrackRecordArmed (const std::string& trackId) const noexcept;
    const std::optional<std::string>& recordArmedTrackId() const noexcept;
    void setSelectedTrack (std::string trackId);
    void clearSelectedTrack() noexcept;
    const std::optional<std::string>& selectedTrackId() const noexcept;
    void setSelectedRecordingClip (std::string trackId, std::string clipId);
    void clearSelectedRecordingClip();
    std::string midiRecordingStatusText() const;
    engine::plugins::PluginRegistry& pluginRegistry() noexcept;
    const engine::plugins::PluginRegistry& pluginRegistry() const noexcept;
    engine::plugins::PluginScanService& pluginScanService() noexcept;
    const engine::plugins::PluginScanService& pluginScanService() const noexcept;
    core::sequencing::Project& project() noexcept;
    const core::sequencing::Project& project() const noexcept;
    core::commands::CommandStack& commandStack() noexcept;
    const core::commands::CommandStack& commandStack() const noexcept;
    core::time::Tempo defaultTempo() const noexcept;
    core::time::TimeSignature defaultTimeSignature() const noexcept;

    std::string_view buildType() const noexcept;
    std::string_view platformString() const noexcept;
    std::string diagnosticsLogFilePath() const;
    std::vector<std::string> diagnosticLines() const;

private:
    core::diagnostics::Logger logger_;
    core::sequencing::Project project_;
    core::commands::ProjectCommandContext projectCommandContext_;
    core::commands::CommandStack commandStack_;
    core::time::Tempo defaultTempo_;
    core::time::TimeSignature defaultTimeSignature_;
    std::unique_ptr<AppSettingsService> appSettingsService_;
    AppSettings appSettings_;
    std::unique_ptr<engine::plugins::PluginRegistry> pluginRegistry_;
    std::unique_ptr<engine::plugins::PluginScanService> pluginScanService_;
    std::unique_ptr<engine::PlaybackEngine> playbackEngine_;
    std::unique_ptr<MidiInputRecordingService> midiInputRecordingService_;
    std::string buildType_;
    std::string platformString_;
    std::string lastUserMessage_;
    bool playbackProjectDirty_ = true;
    core::commands::PlaybackSyncCategory playbackProjectDirtyCategories_ = core::commands::PlaybackSyncCategory::unknown;
    std::optional<std::filesystem::path> currentProjectPackagePath_;

    struct RecordingClipSelection
    {
        std::string trackId;
        std::string clipId;
    };

    struct ActiveRecordedNote
    {
        std::string trackId;
        std::string clipId;
        core::time::TickPosition start {};
        int midiNote = 60;
        int velocity = 100;
        core::music_theory::NoteName spelling { core::music_theory::LetterName::c };
    };

    bool midiRecordingEnabled_ = false;
    bool inputQuantizationEnabled_ = false;
    core::sequencing::ScaleLockMode scaleLockMode_ = core::sequencing::ScaleLockMode::off;
    bool recordingClockInitialized_ = false;
    double recordingStartSeconds_ = 0.0;
    core::time::TickPosition recordingStartTick_ {};
    std::optional<std::string> recordArmedTrackId_;
    std::optional<std::string> selectedTrackId_;
    std::optional<RecordingClipSelection> selectedRecordingClip_;
    std::optional<RecordingClipSelection> activeRecordingClip_;
    std::map<int, ActiveRecordedNote> activeRecordedNotes_;

    core::time::TickPosition recordingTickForTimestamp (double timestampSeconds) const;
    core::music_theory::ScaleLibrary scaleLibraryForRecording() const;
    bool ensureRecordingClockInitialized (double timestampSeconds);
    std::optional<RecordingClipSelection> ensureRecordingClipFor (core::time::TickPosition projectTick);
    bool ensureClipContainsLocalEnd (const RecordingClipSelection& selection, core::time::TickPosition localEnd);
    void handleRecordedNoteOn (const QueuedMidiInputEvent& event);
    void handleRecordedNoteOff (const QueuedMidiInputEvent& event);
    void resetToDefaultProject();
    bool executeExpressionCommand (std::unique_ptr<core::commands::Command> command, const std::string& failurePrefix);
    bool syncPlaybackAfterDeviceEdit (const std::string& successMessage, const std::string& failurePrefix);
    core::sequencing::Project projectPreparedForSave (const std::vector<engine::TrackPluginState>& pluginStates) const;
    bool writePluginStateFiles (const std::filesystem::path& packagePath,
                                const core::sequencing::Project& projectToSave,
                                const std::vector<engine::TrackPluginState>& pluginStates);
    void warnAboutMissingPlugins();
};
}
