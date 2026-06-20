#include "engine/devices/SimpleOscComplexTracktionPlugin.h"

#include "core/devices/FirstPartyDeviceRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>

namespace tsq::engine::devices
{
namespace te = tracktion::engine;

namespace
{
constexpr auto patchTypeProperty = "tsqFirstPartyTypeId";
constexpr auto patchVersionProperty = "tsqFirstPartyPatchVersion";
constexpr auto expressionModulationRenderChunkSamples = 64;

std::string statePropertyForParameter (const std::string& parameterId)
{
    return "tsqParam:" + parameterId;
}

double propertyOrDefault (const juce::ValueTree& state,
                          const core::devices::FirstPartyParameterDefinition& parameter)
{
    const auto propertyName = juce::Identifier { statePropertyForParameter (parameter.id) };
    if (! state.hasProperty (propertyName))
        return parameter.defaultNormalizedValue;

    return std::clamp (static_cast<double> (state.getProperty (propertyName)), 0.0, 1.0);
}

double normalizedValueOrDefault (const core::sequencing::FirstPartyDeviceState& deviceState,
                                 const core::devices::FirstPartyParameterDefinition& parameter)
{
    const auto match = std::find_if (deviceState.parameterValues.begin(),
                                     deviceState.parameterValues.end(),
                                     [&parameter] (const auto& value)
                                     {
                                         return value.parameterId == parameter.id;
                                     });

    if (match == deviceState.parameterValues.end())
        return parameter.defaultNormalizedValue;

    return std::clamp (match->normalizedValue, 0.0, 1.0);
}

core::sequencing::FirstPartyDeviceState deviceStateFromValueTree (const juce::ValueTree& state)
{
    const auto& definition = core::devices::simpleOscComplexDefinition();
    std::vector<core::sequencing::FirstPartyDeviceParameterValue> values;
    values.reserve (definition.parameters.size());

    for (const auto& parameter : definition.parameters)
    {
        values.push_back (core::sequencing::FirstPartyDeviceParameterValue {
            parameter.id,
            propertyOrDefault (state, parameter)
        });
    }

    return core::sequencing::FirstPartyDeviceState {
        definition.typeId,
        definition.patchVersion,
        std::move (values)
    };
}

double interpolate (double start, double end, double alpha) noexcept
{
    return start + ((end - start) * std::clamp (alpha, 0.0, 1.0));
}

SimpleOscComplexModulationTarget modulationTargetForParameterId (std::string_view parameterId) noexcept
{
    if (parameterId == "pitch")
        return SimpleOscComplexModulationTarget::pitch;
    if (parameterId == "amp.level")
        return SimpleOscComplexModulationTarget::ampLevel;
    if (parameterId == "osc.pm.amount")
        return SimpleOscComplexModulationTarget::pmAmount;
    if (parameterId == "osc.mod.ratio")
        return SimpleOscComplexModulationTarget::modRatio;
    if (parameterId == "wavefolder.amount")
        return SimpleOscComplexModulationTarget::wavefolderAmount;
    if (parameterId == "wavefolder.stages")
        return SimpleOscComplexModulationTarget::wavefolderStages;
    if (parameterId == "amp.attack")
        return SimpleOscComplexModulationTarget::ampAttack;
    if (parameterId == "amp.decay")
        return SimpleOscComplexModulationTarget::ampDecay;
    if (parameterId == "amp.sustain")
        return SimpleOscComplexModulationTarget::ampSustain;
    if (parameterId == "amp.release")
        return SimpleOscComplexModulationTarget::ampRelease;

    return SimpleOscComplexModulationTarget::unknown;
}

bool validModulationSegment (const SimpleOscComplexModulationSegment& segment) noexcept
{
    return std::isfinite (segment.startSeconds)
        && std::isfinite (segment.endSeconds)
        && std::isfinite (segment.startValue)
        && std::isfinite (segment.endValue)
        && segment.endSeconds >= segment.startSeconds;
}

std::optional<double> valueForSegmentsAt (const std::vector<SimpleOscComplexModulationSegment>& segments,
                                          double seconds) noexcept
{
    if (segments.empty() || ! std::isfinite (seconds))
        return std::nullopt;

    const auto firstAfterStart = std::upper_bound (segments.begin(),
                                                   segments.end(),
                                                   seconds,
                                                   [] (double value, const auto& segment)
                                                   {
                                                       return value < segment.startSeconds;
                                                   });
    if (firstAfterStart == segments.begin())
        return std::nullopt;

    const auto& segment = *std::prev (firstAfterStart);
    if (seconds > segment.endSeconds)
        return std::nullopt;

    const auto duration = segment.endSeconds - segment.startSeconds;
    const auto alpha = duration <= 0.0 ? 1.0 : (seconds - segment.startSeconds) / duration;
    return interpolate (segment.startValue, segment.endValue, alpha);
}

void sortModulationSegments (std::vector<SimpleOscComplexModulationSegment>& segments)
{
    segments.erase (std::remove_if (segments.begin(),
                                    segments.end(),
                                    [] (const auto& segment)
                                    {
                                        return ! validModulationSegment (segment);
                                    }),
                    segments.end());

    std::sort (segments.begin(), segments.end(), [] (const auto& lhs, const auto& rhs)
    {
        if (lhs.startSeconds != rhs.startSeconds)
            return lhs.startSeconds < rhs.startSeconds;
        return lhs.endSeconds < rhs.endSeconds;
    });
}

void applyModulationValue (core::devices::SimpleOscComplexPatch& patch,
                           SimpleOscComplexModulationTarget target,
                           double value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (target)
    {
        case SimpleOscComplexModulationTarget::pitch:
            patch.pitchSemitones = std::clamp (value, -48.0, 48.0);
            break;
        case SimpleOscComplexModulationTarget::ampLevel:
            patch.ampLevel = std::clamp (value, 0.0, 1.0);
            break;
        case SimpleOscComplexModulationTarget::pmAmount:
            patch.pmAmount = std::clamp (value, 0.0, 1.0);
            break;
        case SimpleOscComplexModulationTarget::modRatio:
            patch.modRatioIndex = std::clamp (static_cast<int> (std::llround (value)), 0, 7);
            break;
        case SimpleOscComplexModulationTarget::wavefolderAmount:
            patch.wavefolderAmount = std::clamp (value, 0.0, 1.0);
            break;
        case SimpleOscComplexModulationTarget::wavefolderStages:
            patch.wavefolderStages = std::clamp (static_cast<int> (std::llround (value)), 1, 5);
            break;
        case SimpleOscComplexModulationTarget::ampAttack:
            patch.attackSeconds = std::clamp (value, 0.0, 10.0);
            break;
        case SimpleOscComplexModulationTarget::ampDecay:
            patch.decaySeconds = std::clamp (value, 0.001, 10.0);
            break;
        case SimpleOscComplexModulationTarget::ampSustain:
            patch.sustainLevel = std::clamp (value, 0.0, 1.0);
            break;
        case SimpleOscComplexModulationTarget::ampRelease:
            patch.releaseSeconds = std::clamp (value, 0.0, 20.0);
            break;
        case SimpleOscComplexModulationTarget::unknown:
            break;
    }
}
}

const char* SimpleOscComplexTracktionPlugin::xmlTypeName = "tsq_simple_osc_complex";

const char* SimpleOscComplexTracktionPlugin::getPluginName()
{
    return "Simple Osc Complex";
}

SimpleOscComplexTracktionPlugin::SimpleOscComplexTracktionPlugin (te::PluginCreationInfo info)
    : Plugin (info),
      synth_ (16)
{
}

SimpleOscComplexTracktionPlugin::~SimpleOscComplexTracktionPlugin()
{
    notifyListenersOfDeletion();
}

juce::ValueTree SimpleOscComplexTracktionPlugin::createState (const core::sequencing::FirstPartyDeviceState& deviceState)
{
    juce::ValueTree state { te::IDs::PLUGIN };
    state.setProperty (te::IDs::type, xmlTypeName, nullptr);
    state.setProperty (patchTypeProperty, juce::String::fromUTF8 (deviceState.typeId.c_str()), nullptr);
    state.setProperty (patchVersionProperty, deviceState.patchVersion, nullptr);

    for (const auto& parameter : deviceState.parameterValues)
        if (! parameter.parameterId.empty())
            state.setProperty (juce::Identifier { statePropertyForParameter (parameter.parameterId) },
                               std::clamp (parameter.normalizedValue, 0.0, 1.0),
                               nullptr);

    return state;
}

juce::String SimpleOscComplexTracktionPlugin::getName() const
{
    return getPluginName();
}

juce::String SimpleOscComplexTracktionPlugin::getPluginType()
{
    return xmlTypeName;
}

juce::String SimpleOscComplexTracktionPlugin::getShortName (int)
{
    return "SOC";
}

juce::String SimpleOscComplexTracktionPlugin::getSelectableDescription()
{
    return "Simple Osc Complex native synth";
}

int SimpleOscComplexTracktionPlugin::getNumOutputChannelsGivenInputs (int)
{
    return 2;
}

void SimpleOscComplexTracktionPlugin::getChannelNames (juce::StringArray* ins, juce::StringArray* outs)
{
    if (ins != nullptr)
        ins->clear();

    if (outs != nullptr)
    {
        outs->clear();
        outs->add ("Left");
        outs->add ("Right");
    }
}

bool SimpleOscComplexTracktionPlugin::takesMidiInput()
{
    return true;
}

bool SimpleOscComplexTracktionPlugin::takesAudioInput()
{
    return false;
}

bool SimpleOscComplexTracktionPlugin::isSynth()
{
    return true;
}

bool SimpleOscComplexTracktionPlugin::producesAudioWhenNoAudioInput()
{
    return true;
}

bool SimpleOscComplexTracktionPlugin::noTail()
{
    return false;
}

double SimpleOscComplexTracktionPlugin::getTailLength() const
{
    return synth_.patch().releaseSeconds;
}

void SimpleOscComplexTracktionPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    currentSampleRate_ = info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
    synth_.prepare (currentSampleRate_);
    updatePatchFromState();
}

void SimpleOscComplexTracktionPlugin::deinitialise()
{
    synth_.reset();
    completedLegatoSlurSourceIds_.clear();
    markExpressionPlaybackChasePending();
}

void SimpleOscComplexTracktionPlugin::reset()
{
    synth_.reset();
    completedLegatoSlurSourceIds_.clear();
    markExpressionPlaybackChasePending();
}

void SimpleOscComplexTracktionPlugin::midiPanic()
{
    synth_.allNotesOff();
    completedLegatoSlurSourceIds_.clear();
    markExpressionPlaybackChasePending();
}

void SimpleOscComplexTracktionPlugin::applyToBuffer (const te::PluginRenderContext& context)
{
    debugRenderCallbackCount_.fetch_add (1, std::memory_order_relaxed);
    if (context.bufferForMidiMessages != nullptr && context.bufferForMidiMessages->size() > 0)
        debugRenderCallbackWithMidiCount_.fetch_add (1, std::memory_order_relaxed);
    if (context.isPlaying)
        debugRenderCallbackPlayingCount_.fetch_add (1, std::memory_order_relaxed);
    debugLastRenderStartSeconds_.store (context.editTime.getStart().inSeconds(), std::memory_order_relaxed);
    debugLastRenderEndSeconds_.store (context.editTime.getEnd().inSeconds(), std::memory_order_relaxed);

    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0)
        return;

    SCOPED_REALTIME_CHECK

    const auto blockStartSeconds = context.editTime.getStart().inSeconds();
    const auto timelineRangeSeconds = context.editTime.getLength().inSeconds();
    const auto hasTimelineRange = std::isfinite (timelineRangeSeconds) && timelineRangeSeconds > 0.0;
    const auto shouldProcessTimelineEvents = context.isPlaying || (context.isRendering && hasTimelineRange);
    if (shouldProcessTimelineEvents && shouldChaseExpressionPlayback (blockStartSeconds))
        chaseExpressionPlaybackAt (blockStartSeconds);

    auto& buffer = *context.destBuffer;
    if (buffer.getNumChannels() < 2)
    {
        buffer.clear (context.bufferStartSample, context.bufferNumSamples);
        return;
    }

    buffer.clear (0, context.bufferStartSample, context.bufferNumSamples);
    buffer.clear (1, context.bufferStartSample, context.bufferNumSamples);

    const auto hasNativeScheduledTimelineEvents = shouldProcessTimelineEvents
        && (! expressionNoteEvents_.empty()
            || ! expressionSlurEvents_.empty()
            || ! expressionPitchOffsetEvents_.empty());
    if (context.bufferForMidiMessages != nullptr
        && context.bufferForMidiMessages->isAllNotesOff
        && ! hasNativeScheduledTimelineEvents)
    {
        debugGraphAllNotesOffAppliedCount_.fetch_add (1, std::memory_order_relaxed);
        synth_.allNotesOff();
    }
    else if (context.bufferForMidiMessages != nullptr
             && context.bufferForMidiMessages->isAllNotesOff
             && hasNativeScheduledTimelineEvents)
    {
        debugGraphAllNotesOffIgnoredCount_.fetch_add (1, std::memory_order_relaxed);
    }

    auto renderedSamples = 0;
    const auto renderSegment = [&] (int segmentSamples)
    {
        auto remainingSamples = segmentSamples;
        while (remainingSamples > 0)
        {
            const auto chunkSamples = expressionModulationStreams_.empty()
                ? remainingSamples
                : std::min (remainingSamples, expressionModulationRenderChunkSamples);
            const auto segmentStartSeconds = context.editTime.getStart().inSeconds()
                + (static_cast<double> (renderedSamples) / currentSampleRate_);

            synth_.setPatch (patchForTime (segmentStartSeconds));
            synth_.render (buffer.getWritePointer (0, context.bufferStartSample + renderedSamples),
                           buffer.getWritePointer (1, context.bufferStartSample + renderedSamples),
                           chunkSamples);

            renderedSamples += chunkSamples;
            remainingSamples -= chunkSamples;
        }
    };

    auto noteEventIndex = std::lower_bound (expressionNoteEvents_.begin(),
                                            expressionNoteEvents_.end(),
                                            blockStartSeconds,
                                            [] (const auto& event, double seconds)
                                            {
                                                return event.seconds < seconds;
                                            });
    auto slurEventIndex = std::lower_bound (expressionSlurEvents_.begin(),
                                            expressionSlurEvents_.end(),
                                            blockStartSeconds,
                                            [] (const auto& event, double seconds)
                                            {
                                                return event.seconds < seconds;
                                            });
    auto pitchOffsetEventIndex = std::lower_bound (expressionPitchOffsetEvents_.begin(),
                                                   expressionPitchOffsetEvents_.end(),
                                                   blockStartSeconds,
                                                   [] (const auto& event, double seconds)
                                                   {
                                                       return event.seconds < seconds;
                                                   });

    const auto localSampleForSeconds = [&] (double seconds)
    {
        return std::clamp (juce::roundToInt ((seconds - blockStartSeconds) * currentSampleRate_),
                           0,
                           context.bufferNumSamples);
    };

    const auto triggerNoteEvent = [this] (const SimpleOscComplexScheduledNoteEvent& event)
    {
        if (event.noteOn)
            expressionNoteOn (event.noteId, event.midiNote, event.velocity);
        else
            expressionNoteOff (event.noteId);
    };

    const auto triggerSlurEvent = [this] (const SimpleOscComplexScheduledSlurEvent& event)
    {
        expressionStartLegatoSlur (event.sourceNoteId,
                                   event.destinationNoteId,
                                   event.destinationMidiNote,
                                   event.destinationVelocity,
                                   event.slurTimeSeconds,
                                   event.curveShape,
                                   event.legatoNoRetrigger);
    };

    const auto triggerPitchOffsetEvent = [this] (const SimpleOscComplexScheduledPitchOffsetEvent& event)
    {
        expressionSetVoicePitchOffset (event.noteId, event.offsetSemitones);
    };

    const auto processScheduledEventsUntil = [&] (int localSampleLimit)
    {
        const auto limit = std::clamp (localSampleLimit, 0, context.bufferNumSamples);
        while (true)
        {
            enum class EventKind
            {
                none,
                note,
                slur,
                pitchOffset
            };

            auto nextKind = EventKind::none;
            auto nextPriority = std::numeric_limits<int>::max();
            auto nextSeconds = std::numeric_limits<double>::infinity();
            constexpr auto eventTimeEpsilon = 1.0e-9;
            const auto limitSeconds = blockStartSeconds
                + (static_cast<double> (limit) / currentSampleRate_);

            const auto considerEvent = [&] (double seconds, int priority, EventKind kind)
            {
                if (seconds > limitSeconds + eventTimeEpsilon)
                    return;

                const auto localSample = localSampleForSeconds (seconds);
                if (localSample <= limit)
                {
                    const auto earlier = seconds < nextSeconds - eventTimeEpsilon;
                    const auto sameTime = std::abs (seconds - nextSeconds) <= eventTimeEpsilon;
                    if (earlier || (sameTime && priority < nextPriority))
                    {
                        nextKind = kind;
                        nextPriority = priority;
                        nextSeconds = seconds;
                    }
                }
            };

            if (noteEventIndex != expressionNoteEvents_.end())
            {
                considerEvent (noteEventIndex->seconds,
                               noteEventIndex->noteOn ? 0 : 3,
                               EventKind::note);
            }

            if (slurEventIndex != expressionSlurEvents_.end())
            {
                considerEvent (slurEventIndex->seconds,
                               1,
                               EventKind::slur);
            }

            if (pitchOffsetEventIndex != expressionPitchOffsetEvents_.end())
            {
                considerEvent (pitchOffsetEventIndex->seconds,
                               2,
                               EventKind::pitchOffset);
            }

            if (! std::isfinite (nextSeconds))
                break;

            const auto eventSample = localSampleForSeconds (nextSeconds);
            renderSegment (eventSample - renderedSamples);

            switch (nextKind)
            {
                case EventKind::note:
                    triggerNoteEvent (*noteEventIndex++);
                    break;
                case EventKind::slur:
                    triggerSlurEvent (*slurEventIndex++);
                    break;
                case EventKind::pitchOffset:
                    triggerPitchOffsetEvent (*pitchOffsetEventIndex++);
                    break;
                case EventKind::none:
                    break;
            }
        }
    };

    if (context.bufferForMidiMessages != nullptr)
    {
        for (const auto& messageWithSource : *context.bufferForMidiMessages)
        {
            const auto midiSample = juce::roundToInt (messageWithSource.getTimeStamp() * currentSampleRate_);
            const auto localSample = std::clamp (midiSample - context.bufferStartSample, 0, context.bufferNumSamples);
            if (shouldProcessTimelineEvents)
                processScheduledEventsUntil (localSample);
            renderSegment (localSample - renderedSamples);
            handleMidiMessage (messageWithSource);
        }
    }

    if (shouldProcessTimelineEvents)
        processScheduledEventsUntil (context.bufferNumSamples);
    renderSegment (context.bufferNumSamples - renderedSamples);
    if (shouldProcessTimelineEvents)
    {
        lastRenderEndSeconds_ = blockStartSeconds + (static_cast<double> (context.bufferNumSamples) / currentSampleRate_);
        hasLastRenderEndSeconds_ = true;
    }
    else
    {
        markExpressionPlaybackChasePending();
    }

    recordDebugOutputPeak (buffer, context.bufferStartSample, context.bufferNumSamples);

    for (auto channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, context.bufferStartSample, context.bufferNumSamples);
}

void SimpleOscComplexTracktionPlugin::markExpressionPlaybackChasePending() noexcept
{
    hasLastRenderEndSeconds_ = false;
}

bool SimpleOscComplexTracktionPlugin::shouldChaseExpressionPlayback (double blockStartSeconds) const noexcept
{
    const auto hasExpressionEvents = ! expressionNoteEvents_.empty()
        || ! expressionSlurEvents_.empty()
        || ! expressionPitchOffsetEvents_.empty();
    if (! hasExpressionEvents || ! std::isfinite (blockStartSeconds))
        return false;

    if (! hasLastRenderEndSeconds_)
        return true;

    const auto sampleToleranceSeconds = 2.0 / std::max (1.0, currentSampleRate_);
    return std::abs (blockStartSeconds - lastRenderEndSeconds_) > sampleToleranceSeconds;
}

void SimpleOscComplexTracktionPlugin::chaseExpressionPlaybackAt (double seconds)
{
    if (! std::isfinite (seconds))
        return;

    synth_.reset();
    completedLegatoSlurSourceIds_.clear();

    auto noteEventIndex = expressionNoteEvents_.begin();
    auto slurEventIndex = expressionSlurEvents_.begin();
    auto pitchOffsetEventIndex = expressionPitchOffsetEvents_.begin();
    constexpr auto eventTimeEpsilon = 1.0e-9;

    while (true)
    {
        enum class EventKind
        {
            none,
            note,
            slur,
            pitchOffset
        };

        auto nextKind = EventKind::none;
        auto nextPriority = std::numeric_limits<int>::max();
        auto nextSeconds = std::numeric_limits<double>::infinity();

        const auto considerEvent = [&] (double eventSeconds, int priority, EventKind kind)
        {
            if (eventSeconds >= seconds - eventTimeEpsilon)
                return;

            const auto earlier = eventSeconds < nextSeconds - eventTimeEpsilon;
            const auto sameTime = std::abs (eventSeconds - nextSeconds) <= eventTimeEpsilon;
            if (earlier || (sameTime && priority < nextPriority))
            {
                nextKind = kind;
                nextPriority = priority;
                nextSeconds = eventSeconds;
            }
        };

        if (noteEventIndex != expressionNoteEvents_.end())
            considerEvent (noteEventIndex->seconds, noteEventIndex->noteOn ? 0 : 3, EventKind::note);
        if (slurEventIndex != expressionSlurEvents_.end())
            considerEvent (slurEventIndex->seconds, 1, EventKind::slur);
        if (pitchOffsetEventIndex != expressionPitchOffsetEvents_.end())
            considerEvent (pitchOffsetEventIndex->seconds, 2, EventKind::pitchOffset);

        if (! std::isfinite (nextSeconds))
            break;

        switch (nextKind)
        {
            case EventKind::note:
                if (noteEventIndex->noteOn)
                    expressionNoteOn (noteEventIndex->noteId, noteEventIndex->midiNote, noteEventIndex->velocity);
                else
                    expressionNoteOff (noteEventIndex->noteId);
                ++noteEventIndex;
                break;

            case EventKind::slur:
                expressionStartLegatoSlur (slurEventIndex->sourceNoteId,
                                           slurEventIndex->destinationNoteId,
                                           slurEventIndex->destinationMidiNote,
                                           slurEventIndex->destinationVelocity,
                                           slurEventIndex->slurTimeSeconds,
                                           slurEventIndex->curveShape,
                                           slurEventIndex->legatoNoRetrigger);
                ++slurEventIndex;
                break;

            case EventKind::pitchOffset:
                expressionSetVoicePitchOffset (pitchOffsetEventIndex->noteId, pitchOffsetEventIndex->offsetSemitones);
                ++pitchOffsetEventIndex;
                break;

            case EventKind::none:
                break;
        }
    }
}

void SimpleOscComplexTracktionPlugin::updatePatchFromState()
{
    basePatch_ = core::devices::simpleOscComplexPatchFromState (deviceStateFromValueTree (state));
    synth_.setPatch (basePatch_);
    ++patchStateRefreshCount_;
}

core::devices::SimpleOscComplexPatch SimpleOscComplexTracktionPlugin::patchForTime (double seconds) const
{
    auto patch = basePatch_;
    for (const auto& stream : expressionModulationStreams_)
        if (const auto value = valueForSegmentsAt (stream.segments, seconds); value.has_value())
            applyModulationValue (patch, stream.target, *value);

    return patch;
}

void SimpleOscComplexTracktionPlugin::handleMidiMessage (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        debugMidiNoteOnCount_.fetch_add (1, std::memory_order_relaxed);
        synth_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        debugMidiNoteOffCount_.fetch_add (1, std::memory_order_relaxed);
        synth_.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        debugMidiAllNotesOffCount_.fetch_add (1, std::memory_order_relaxed);
        synth_.allNotesOff();
    }
}

void SimpleOscComplexTracktionPlugin::recordDebugOutputPeak (const juce::AudioBuffer<float>& buffer,
                                                            int startSample,
                                                            int numSamples) noexcept
{
    if (startSample < 0 || numSamples <= 0)
        return;

    auto blockPeak = 0.0f;
    const auto channelCount = std::min (2, buffer.getNumChannels());
    for (auto channel = 0; channel < channelCount; ++channel)
    {
        const auto* samples = buffer.getReadPointer (channel, startSample);
        for (auto index = 0; index < numSamples; ++index)
            blockPeak = std::max (blockPeak, std::abs (samples[index]));
    }

    auto observedPeak = debugMaxOutputPeak_.load (std::memory_order_relaxed);
    debugLastOutputPeak_.store (blockPeak, std::memory_order_relaxed);
    while (blockPeak > observedPeak
           && ! debugMaxOutputPeak_.compare_exchange_weak (observedPeak,
                                                           blockPeak,
                                                           std::memory_order_relaxed,
                                                           std::memory_order_relaxed))
    {
    }
}

void SimpleOscComplexTracktionPlugin::setExpressionModulationStreams (std::vector<SimpleOscComplexModulationStream> streams)
{
    expressionModulationStreams_.clear();
    expressionModulationStreams_.reserve (streams.size());

    for (auto& stream : streams)
    {
        const auto target = modulationTargetForParameterId (stream.parameterId);
        if (target == SimpleOscComplexModulationTarget::unknown)
            continue;

        sortModulationSegments (stream.segments);
        if (stream.segments.empty())
            continue;

        expressionModulationStreams_.push_back (PreparedSimpleOscComplexModulationStream {
            target,
            std::move (stream.segments)
        });
    }
}

void SimpleOscComplexTracktionPlugin::setFirstPartyDeviceState (const core::sequencing::FirstPartyDeviceState& deviceState)
{
    const auto& definition = core::devices::simpleOscComplexDefinition();
    if (deviceState.typeId != definition.typeId)
        return;

    state.setProperty (patchTypeProperty, juce::String::fromUTF8 (deviceState.typeId.c_str()), nullptr);
    state.setProperty (patchVersionProperty, deviceState.patchVersion, nullptr);

    for (const auto& parameter : definition.parameters)
    {
        state.setProperty (juce::Identifier { statePropertyForParameter (parameter.id) },
                           normalizedValueOrDefault (deviceState, parameter),
                           nullptr);
    }

    basePatch_ = core::devices::simpleOscComplexPatchFromState (deviceState);
    synth_.setPatch (basePatch_);
    ++patchStateRefreshCount_;
}

void SimpleOscComplexTracktionPlugin::clearExpressionModulationStreams()
{
    expressionModulationStreams_.clear();
}

std::size_t SimpleOscComplexTracktionPlugin::expressionModulationStreamCount() const noexcept
{
    return expressionModulationStreams_.size();
}

void SimpleOscComplexTracktionPlugin::setExpressionPitchEvents (std::vector<SimpleOscComplexScheduledNoteEvent> noteEvents,
                                                               std::vector<SimpleOscComplexScheduledSlurEvent> slurEvents,
                                                               std::vector<SimpleOscComplexScheduledPitchOffsetEvent> pitchOffsetEvents)
{
    std::sort (noteEvents.begin(), noteEvents.end(), [] (const auto& lhs, const auto& rhs)
    {
        if (lhs.seconds != rhs.seconds)
            return lhs.seconds < rhs.seconds;
        return lhs.noteOn && ! rhs.noteOn;
    });
    std::sort (slurEvents.begin(), slurEvents.end(), [] (const auto& lhs, const auto& rhs)
    {
        return lhs.seconds < rhs.seconds;
    });
    std::sort (pitchOffsetEvents.begin(), pitchOffsetEvents.end(), [] (const auto& lhs, const auto& rhs)
    {
        return lhs.seconds < rhs.seconds;
    });
    expressionNoteEvents_ = std::move (noteEvents);
    expressionSlurEvents_ = std::move (slurEvents);
    expressionPitchOffsetEvents_ = std::move (pitchOffsetEvents);
    completedLegatoSlurSourceIds_.clear();
    completedLegatoSlurSourceIds_.reserve (expressionSlurEvents_.size());
    markExpressionPlaybackChasePending();
}

void SimpleOscComplexTracktionPlugin::clearExpressionPitchEvents()
{
    expressionNoteEvents_.clear();
    expressionSlurEvents_.clear();
    expressionPitchOffsetEvents_.clear();
    completedLegatoSlurSourceIds_.clear();
    markExpressionPlaybackChasePending();
}

std::size_t SimpleOscComplexTracktionPlugin::expressionPitchEventCount() const noexcept
{
    return expressionNoteEvents_.size() + expressionSlurEvents_.size() + expressionPitchOffsetEvents_.size();
}

std::size_t SimpleOscComplexTracktionPlugin::expressionSlurEventCount() const noexcept
{
    return expressionSlurEvents_.size();
}

std::size_t SimpleOscComplexTracktionPlugin::expressionLegatoSlurEventCount() const noexcept
{
    return static_cast<std::size_t> (std::count_if (expressionSlurEvents_.begin(),
                                                   expressionSlurEvents_.end(),
                                                   [] (const auto& event)
                                                   {
                                                       return event.legatoNoRetrigger;
                                                   }));
}

std::vector<core::devices::SimpleOscComplexNoteId> SimpleOscComplexTracktionPlugin::debugExpressionNoteOnEventIds() const
{
    std::vector<core::devices::SimpleOscComplexNoteId> ids;
    ids.reserve (expressionNoteEvents_.size());
    for (const auto& event : expressionNoteEvents_)
        if (event.noteOn)
            ids.push_back (event.noteId);

    return ids;
}

std::size_t SimpleOscComplexTracktionPlugin::debugActiveVoiceCount() const noexcept
{
    return synth_.activeVoiceCount();
}

std::size_t SimpleOscComplexTracktionPlugin::debugMidiNoteOnCount() const noexcept
{
    return debugMidiNoteOnCount_.load (std::memory_order_relaxed);
}

std::size_t SimpleOscComplexTracktionPlugin::debugRenderCallbackCount() const noexcept
{
    return debugRenderCallbackCount_.load (std::memory_order_relaxed);
}

std::size_t SimpleOscComplexTracktionPlugin::debugRenderCallbackWithMidiCount() const noexcept
{
    return debugRenderCallbackWithMidiCount_.load (std::memory_order_relaxed);
}

std::size_t SimpleOscComplexTracktionPlugin::debugRenderCallbackPlayingCount() const noexcept
{
    return debugRenderCallbackPlayingCount_.load (std::memory_order_relaxed);
}

std::size_t SimpleOscComplexTracktionPlugin::debugExpressionSlurFallbackCount() const noexcept
{
    return debugExpressionSlurFallbackCount_.load (std::memory_order_relaxed);
}

float SimpleOscComplexTracktionPlugin::debugMaxOutputPeak() const noexcept
{
    return debugMaxOutputPeak_.load (std::memory_order_relaxed);
}

float SimpleOscComplexTracktionPlugin::debugLastOutputPeak() const noexcept
{
    return debugLastOutputPeak_.load (std::memory_order_relaxed);
}

std::vector<std::size_t> SimpleOscComplexTracktionPlugin::debugEventCounters() const
{
    return {
        debugExpressionNoteOnTriggerCount_.load (std::memory_order_relaxed),
        debugExpressionNoteOffTriggerCount_.load (std::memory_order_relaxed),
        debugMidiNoteOffCount_.load (std::memory_order_relaxed),
        debugMidiAllNotesOffCount_.load (std::memory_order_relaxed),
        debugGraphAllNotesOffAppliedCount_.load (std::memory_order_relaxed),
        debugGraphAllNotesOffIgnoredCount_.load (std::memory_order_relaxed)
    };
}

double SimpleOscComplexTracktionPlugin::debugLastRenderStartSeconds() const noexcept
{
    return debugLastRenderStartSeconds_.load (std::memory_order_relaxed);
}

double SimpleOscComplexTracktionPlugin::debugLastRenderEndSeconds() const noexcept
{
    return debugLastRenderEndSeconds_.load (std::memory_order_relaxed);
}

std::size_t SimpleOscComplexTracktionPlugin::debugPatchStateRefreshCount() const noexcept
{
    return patchStateRefreshCount_;
}

void SimpleOscComplexTracktionPlugin::armExpressionPlaybackChase() noexcept
{
    markExpressionPlaybackChasePending();
}

void SimpleOscComplexTracktionPlugin::debugChaseExpressionPlaybackAt (double seconds)
{
    chaseExpressionPlaybackAt (seconds);
}

void SimpleOscComplexTracktionPlugin::expressionNoteOn (core::devices::SimpleOscComplexNoteId noteId,
                                                        int midiNote,
                                                        float velocity)
{
    debugExpressionNoteOnTriggerCount_.fetch_add (1, std::memory_order_relaxed);
    if (noteId != 0 && synth_.activeVoiceCountForNoteId (noteId) > 0)
        return;

    synth_.noteOn (noteId, midiNote, velocity);
}

void SimpleOscComplexTracktionPlugin::expressionNoteOff (core::devices::SimpleOscComplexNoteId noteId)
{
    debugExpressionNoteOffTriggerCount_.fetch_add (1, std::memory_order_relaxed);
    synth_.noteOff (noteId);
}

bool SimpleOscComplexTracktionPlugin::expressionStartLegatoSlur (core::devices::SimpleOscComplexNoteId sourceNoteId,
                                                                 core::devices::SimpleOscComplexNoteId destinationNoteId,
                                                                 int destinationMidiNote,
                                                                 float destinationVelocity,
                                                                 double slurTimeSeconds,
                                                                 core::sequencing::ExpressionCurveShape curveShape,
                                                                 bool legatoNoRetrigger)
{
    const auto sourceAlreadyCompleted = [this, sourceNoteId]
    {
        return std::find (completedLegatoSlurSourceIds_.begin(),
                          completedLegatoSlurSourceIds_.end(),
                          sourceNoteId) != completedLegatoSlurSourceIds_.end();
    };

    if (legatoNoRetrigger && sourceNoteId != 0 && sourceAlreadyCompleted())
        return true;

    const auto slurred = synth_.startLegatoSlur (sourceNoteId,
                                                 destinationNoteId,
                                                 destinationMidiNote,
                                                 destinationVelocity,
                                                 slurTimeSeconds,
                                                 curveShape,
                                                 legatoNoRetrigger);
    if (slurred && legatoNoRetrigger && sourceNoteId != 0 && ! sourceAlreadyCompleted())
        completedLegatoSlurSourceIds_.push_back (sourceNoteId);

    if (! slurred && legatoNoRetrigger)
        debugExpressionSlurFallbackCount_.fetch_add (1, std::memory_order_relaxed);

    return slurred;
}

bool SimpleOscComplexTracktionPlugin::expressionSetVoicePitchOffset (core::devices::SimpleOscComplexNoteId noteId,
                                                                     double offsetSemitones) noexcept
{
    return synth_.setVoicePitchOffset (noteId, offsetSemitones);
}

void registerSimpleOscComplexTracktionPlugin (te::PluginManager& pluginManager)
{
    pluginManager.createBuiltInType<SimpleOscComplexTracktionPlugin>();
}
}
