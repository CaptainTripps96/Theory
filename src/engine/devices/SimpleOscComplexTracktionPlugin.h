#pragma once

#include "core/devices/SimpleOscComplexSynth.h"

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace tsq::engine::devices
{
struct SimpleOscComplexModulationSegment
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double startValue = 0.0;
    double endValue = 0.0;
};

struct SimpleOscComplexModulationStream
{
    std::string parameterId;
    std::vector<SimpleOscComplexModulationSegment> segments;
};

enum class SimpleOscComplexModulationTarget
{
    unknown,
    pitch,
    ampLevel,
    pmAmount,
    modRatio,
    wavefolderAmount,
    wavefolderStages,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease
};

struct PreparedSimpleOscComplexModulationStream
{
    SimpleOscComplexModulationTarget target = SimpleOscComplexModulationTarget::unknown;
    std::vector<SimpleOscComplexModulationSegment> segments;
};

struct SimpleOscComplexScheduledNoteEvent
{
    double seconds = 0.0;
    core::devices::SimpleOscComplexNoteId noteId = 0;
    int midiNote = 60;
    float velocity = 0.0f;
    bool noteOn = true;
};

struct SimpleOscComplexScheduledSlurEvent
{
    double seconds = 0.0;
    core::devices::SimpleOscComplexNoteId sourceNoteId = 0;
    core::devices::SimpleOscComplexNoteId destinationNoteId = 0;
    int destinationMidiNote = 60;
    float destinationVelocity = 0.0f;
    double slurTimeSeconds = 0.0;
    core::sequencing::ExpressionCurveShape curveShape = core::sequencing::ExpressionCurveShape::linear;
    bool legatoNoRetrigger = true;
};

struct SimpleOscComplexScheduledPitchOffsetEvent
{
    double seconds = 0.0;
    core::devices::SimpleOscComplexNoteId noteId = 0;
    double offsetSemitones = 0.0;
};

class SimpleOscComplexTracktionPlugin final : public tracktion::engine::Plugin
{
public:
    explicit SimpleOscComplexTracktionPlugin (tracktion::engine::PluginCreationInfo info);
    ~SimpleOscComplexTracktionPlugin() override;

    static const char* getPluginName();
    static const char* xmlTypeName;
    static juce::ValueTree createState (const core::sequencing::FirstPartyDeviceState& deviceState);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName (int suggestedLength) override;
    juce::String getSelectableDescription() override;

    int getNumOutputChannelsGivenInputs (int numInputChannels) override;
    void getChannelNames (juce::StringArray* ins, juce::StringArray* outs) override;
    bool takesMidiInput() override;
    bool takesAudioInput() override;
    bool isSynth() override;
    bool producesAudioWhenNoAudioInput() override;
    bool noTail() override;
    double getTailLength() const override;

    void initialise (const tracktion::engine::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void midiPanic() override;
    void applyToBuffer (const tracktion::engine::PluginRenderContext& context) override;

    void setExpressionModulationStreams (std::vector<SimpleOscComplexModulationStream> streams);
    void clearExpressionModulationStreams();
    std::size_t expressionModulationStreamCount() const noexcept;
    void setFirstPartyDeviceState (const core::sequencing::FirstPartyDeviceState& deviceState);
    void setExpressionPitchEvents (std::vector<SimpleOscComplexScheduledNoteEvent> noteEvents,
                                   std::vector<SimpleOscComplexScheduledSlurEvent> slurEvents,
                                   std::vector<SimpleOscComplexScheduledPitchOffsetEvent> pitchOffsetEvents = {});
    void clearExpressionPitchEvents();
    std::size_t expressionPitchEventCount() const noexcept;
    std::size_t expressionSlurEventCount() const noexcept;
    std::size_t expressionLegatoSlurEventCount() const noexcept;
    std::vector<core::devices::SimpleOscComplexNoteId> debugExpressionNoteOnEventIds() const;
    std::size_t debugActiveVoiceCount() const noexcept;
    std::size_t debugMidiNoteOnCount() const noexcept;
    std::size_t debugRenderCallbackCount() const noexcept;
    std::size_t debugRenderCallbackWithMidiCount() const noexcept;
    std::size_t debugRenderCallbackPlayingCount() const noexcept;
    std::size_t debugExpressionSlurFallbackCount() const noexcept;
    float debugMaxOutputPeak() const noexcept;
    float debugLastOutputPeak() const noexcept;
    std::vector<std::size_t> debugEventCounters() const;
    double debugLastRenderStartSeconds() const noexcept;
    double debugLastRenderEndSeconds() const noexcept;
    std::size_t debugPatchStateRefreshCount() const noexcept;
    void armExpressionPlaybackChase() noexcept;
    void debugChaseExpressionPlaybackAt (double seconds);

    void expressionNoteOn (core::devices::SimpleOscComplexNoteId noteId, int midiNote, float velocity);
    void expressionNoteOff (core::devices::SimpleOscComplexNoteId noteId);
    bool expressionStartLegatoSlur (core::devices::SimpleOscComplexNoteId sourceNoteId,
                                    core::devices::SimpleOscComplexNoteId destinationNoteId,
                                    int destinationMidiNote,
                                    float destinationVelocity,
                                    double slurTimeSeconds,
                                    core::sequencing::ExpressionCurveShape curveShape,
                                    bool legatoNoRetrigger);
    bool expressionSetVoicePitchOffset (core::devices::SimpleOscComplexNoteId noteId, double offsetSemitones) noexcept;

private:
    core::devices::SimpleOscComplexSynth synth_;
    core::devices::SimpleOscComplexPatch basePatch_;
    std::vector<PreparedSimpleOscComplexModulationStream> expressionModulationStreams_;
    std::vector<SimpleOscComplexScheduledNoteEvent> expressionNoteEvents_;
    std::vector<SimpleOscComplexScheduledSlurEvent> expressionSlurEvents_;
    std::vector<SimpleOscComplexScheduledPitchOffsetEvent> expressionPitchOffsetEvents_;
    std::vector<core::devices::SimpleOscComplexNoteId> completedLegatoSlurSourceIds_;
    double currentSampleRate_ = 44100.0;
    double lastRenderEndSeconds_ = 0.0;
    std::size_t patchStateRefreshCount_ = 0;
    std::atomic<std::size_t> debugMidiNoteOnCount_ { 0 };
    std::atomic<std::size_t> debugRenderCallbackCount_ { 0 };
    std::atomic<std::size_t> debugRenderCallbackWithMidiCount_ { 0 };
    std::atomic<std::size_t> debugRenderCallbackPlayingCount_ { 0 };
    std::atomic<std::size_t> debugExpressionSlurFallbackCount_ { 0 };
    std::atomic<std::size_t> debugExpressionNoteOnTriggerCount_ { 0 };
    std::atomic<std::size_t> debugExpressionNoteOffTriggerCount_ { 0 };
    std::atomic<std::size_t> debugMidiNoteOffCount_ { 0 };
    std::atomic<std::size_t> debugMidiAllNotesOffCount_ { 0 };
    std::atomic<std::size_t> debugGraphAllNotesOffAppliedCount_ { 0 };
    std::atomic<std::size_t> debugGraphAllNotesOffIgnoredCount_ { 0 };
    std::atomic<float> debugMaxOutputPeak_ { 0.0f };
    std::atomic<float> debugLastOutputPeak_ { 0.0f };
    std::atomic<double> debugLastRenderStartSeconds_ { 0.0 };
    std::atomic<double> debugLastRenderEndSeconds_ { 0.0 };
    bool hasLastRenderEndSeconds_ = false;

    void markExpressionPlaybackChasePending() noexcept;
    bool shouldChaseExpressionPlayback (double blockStartSeconds) const noexcept;
    void chaseExpressionPlaybackAt (double seconds);
    void updatePatchFromState();
    core::devices::SimpleOscComplexPatch patchForTime (double seconds) const;
    void handleMidiMessage (const juce::MidiMessage& message);
    void recordDebugOutputPeak (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleOscComplexTracktionPlugin)
};

void registerSimpleOscComplexTracktionPlugin (tracktion::engine::PluginManager& pluginManager);
}
