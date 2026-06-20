#include "core/devices/SimpleOscComplexSynth.h"

#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/sequencing/ExpressionEvaluation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace tsq::core::devices
{
namespace
{
constexpr double defaultSampleRate = 44100.0;
constexpr double minimumFrequencyHz = 8.0;
constexpr double maximumFrequencyHz = 20000.0;

double clampNormalized (double value) noexcept
{
    return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
}

double actualParameterValue (const FirstPartyParameterDefinition& parameter, double normalizedValue)
{
    const auto normalized = clampNormalized (normalizedValue);
    const auto actual = parameter.minimumValue + (normalized * (parameter.maximumValue - parameter.minimumValue));
    if (parameter.valueType == FirstPartyParameterValueType::discrete)
        return std::round (actual);

    return actual;
}

double normalizedValueFor (const sequencing::FirstPartyDeviceState& state,
                           const char* parameterId,
                           double fallback)
{
    const auto match = std::find_if (state.parameterValues.begin(), state.parameterValues.end(), [parameterId] (const auto& value)
    {
        return value.parameterId == parameterId;
    });

    return match == state.parameterValues.end() ? fallback : clampNormalized (match->normalizedValue);
}

double defaultNormalizedValueFor (const char* parameterId)
{
    const auto& definition = simpleOscComplexDefinition();
    const auto match = std::find_if (definition.parameters.begin(), definition.parameters.end(), [parameterId] (const auto& parameter)
    {
        return parameter.id == parameterId;
    });

    return match == definition.parameters.end() ? 0.0 : clampNormalized (match->defaultNormalizedValue);
}

double pitchFrequency (double pitchSemitones)
{
    const auto frequency = 440.0 * std::pow (2.0, (pitchSemitones - 69.0) / 12.0);
    return std::clamp (frequency, minimumFrequencyHz, maximumFrequencyHz);
}

double wrapPhase (double phase) noexcept
{
    phase -= std::floor (phase);
    return phase < 0.0 ? phase + 1.0 : phase;
}

double triangleWave (double phase) noexcept
{
    const auto wrapped = wrapPhase (phase);
    return 4.0 * std::abs (wrapped - 0.5) - 1.0;
}

double foldSampleOnce (double sample) noexcept
{
    while (sample > 1.0 || sample < -1.0)
    {
        if (sample > 1.0)
            sample = 2.0 - sample;
        else if (sample < -1.0)
            sample = -2.0 - sample;
    }

    return sample;
}

double wavefold (double sample, double amount, int stages) noexcept
{
    const auto clampedAmount = std::clamp (amount, 0.0, 1.0);
    const auto clampedStages = std::clamp (stages, 1, 5);
    auto folded = sample * (1.0 + clampedAmount * 8.0);

    for (auto stage = 0; stage < clampedStages; ++stage)
        folded = foldSampleOnce (folded);

    return std::clamp (folded * (0.78 - clampedAmount * 0.18), -1.0, 1.0);
}

int samplesForStage (double seconds, double sampleRate) noexcept
{
    const auto clampedSeconds = std::max (0.0, std::isfinite (seconds) ? seconds : 0.0);
    if (clampedSeconds <= 0.0)
        return 0;

    return std::max (1, static_cast<int> (std::ceil (clampedSeconds * sampleRate)));
}

double parameterValueById (const char* parameterId, double normalizedValue)
{
    const auto& definition = simpleOscComplexDefinition();
    const auto match = std::find_if (definition.parameters.begin(), definition.parameters.end(), [parameterId] (const auto& parameter)
    {
        return parameter.id == parameterId;
    });

    if (match == definition.parameters.end())
        throw std::invalid_argument ("Unknown Simple Osc Complex parameter");

    return actualParameterValue (*match, normalizedValue);
}
}

bool SimpleOscComplexSynth::Voice::active() const noexcept
{
    return stage != EnvelopeStage::idle;
}

bool SimpleOscComplexSynth::Voice::slurring() const noexcept
{
    return slurTotalSamples > 0 && slurSampleIndex < slurTotalSamples;
}

SimpleOscComplexSynth::SimpleOscComplexSynth (std::size_t maxVoices)
    : voices_ (std::max<std::size_t> (1, maxVoices))
{
}

void SimpleOscComplexSynth::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : defaultSampleRate;
    reset();
}

void SimpleOscComplexSynth::setPatch (SimpleOscComplexPatch patch)
{
    patch.pitchSemitones = std::clamp (std::isfinite (patch.pitchSemitones) ? patch.pitchSemitones : 0.0, -48.0, 48.0);
    patch.ampLevel = std::clamp (std::isfinite (patch.ampLevel) ? patch.ampLevel : 0.75, 0.0, 1.0);
    patch.pmAmount = std::clamp (std::isfinite (patch.pmAmount) ? patch.pmAmount : 0.0, 0.0, 1.0);
    patch.modRatioIndex = std::clamp (patch.modRatioIndex, 0, 7);
    patch.wavefolderAmount = std::clamp (std::isfinite (patch.wavefolderAmount) ? patch.wavefolderAmount : 0.0, 0.0, 1.0);
    patch.wavefolderStages = std::clamp (patch.wavefolderStages, 1, 5);
    patch.attackSeconds = std::clamp (std::isfinite (patch.attackSeconds) ? patch.attackSeconds : 0.0, 0.0, 10.0);
    patch.decaySeconds = std::clamp (std::isfinite (patch.decaySeconds) ? patch.decaySeconds : 0.18, 0.0, 10.0);
    patch.sustainLevel = std::clamp (std::isfinite (patch.sustainLevel) ? patch.sustainLevel : 0.75, 0.0, 1.0);
    patch.releaseSeconds = std::clamp (std::isfinite (patch.releaseSeconds) ? patch.releaseSeconds : 0.0, 0.0, 20.0);
    patch_ = patch;
}

const SimpleOscComplexPatch& SimpleOscComplexSynth::patch() const noexcept
{
    return patch_;
}

void SimpleOscComplexSynth::noteOn (int midiNote, float velocity)
{
    noteOn (0, midiNote, velocity);
}

void SimpleOscComplexSynth::noteOn (SimpleOscComplexNoteId noteId, int midiNote, float velocity)
{
    if (velocity <= 0.0f)
    {
        if (noteId != 0)
            noteOff (noteId);
        else
            noteOff (midiNote);
        return;
    }

    auto& voice = allocateVoice();
    voice.noteId = noteId;
    voice.midiNote = std::clamp (midiNote, 0, 127);
    voice.velocity = std::clamp (velocity, 0.0f, 1.0f);
    voice.carrierPhase = 0.0;
    voice.modulatorPhase = 0.0;
    voice.envelopeLevel = 0.0;
    voice.releaseStartLevel = 0.0;
    voice.pitchOffsetSemitones = 0.0;
    voice.slurStartPitchSemitones = static_cast<double> (voice.midiNote);
    voice.currentPitchSemitones = static_cast<double> (voice.midiNote);
    voice.slurSampleIndex = 0;
    voice.slurTotalSamples = 0;
    voice.slurCurveShape = sequencing::ExpressionCurveShape::linear;
    voice.stageSampleIndex = 0;
    voice.stage = EnvelopeStage::attack;
    voice.age = ++voiceAgeCounter_;
}

void SimpleOscComplexSynth::noteOff (int midiNote)
{
    for (auto& voice : voices_)
    {
        if (voice.midiNote != midiNote || ! voice.active() || voice.stage == EnvelopeStage::release)
            continue;

        voice.releaseStartLevel = voice.envelopeLevel;
        voice.stageSampleIndex = 0;
        voice.stage = EnvelopeStage::release;
    }
}

void SimpleOscComplexSynth::noteOff (SimpleOscComplexNoteId noteId)
{
    if (noteId == 0)
        return;

    for (auto& voice : voices_)
    {
        if (! voice.active() || voice.noteId != noteId || voice.stage == EnvelopeStage::release)
            continue;

        voice.releaseStartLevel = voice.envelopeLevel;
        voice.stageSampleIndex = 0;
        voice.stage = EnvelopeStage::release;
    }
}

bool SimpleOscComplexSynth::startLegatoSlur (SimpleOscComplexNoteId sourceNoteId,
                                             SimpleOscComplexNoteId destinationNoteId,
                                             int destinationMidiNote,
                                             float destinationVelocity,
                                             double slurTimeSeconds,
                                             sequencing::ExpressionCurveShape curveShape,
                                             bool legatoNoRetrigger)
{
    if (sourceNoteId == 0 || destinationNoteId == 0)
        return false;

    auto* sourceVoice = findVoiceByNoteId (sourceNoteId);
    if (sourceVoice == nullptr || ! sourceVoice->active())
    {
        if (legatoNoRetrigger)
            if (auto* destinationVoice = findVoiceByNoteId (destinationNoteId);
                destinationVoice != nullptr && destinationVoice->active())
            {
                return true;
            }

        noteOn (destinationNoteId, destinationMidiNote, destinationVelocity);
        return false;
    }

    if (! legatoNoRetrigger)
    {
        noteOn (destinationNoteId, destinationMidiNote, destinationVelocity);
        return true;
    }

    const auto startPitch = voicePitchSemitones (*sourceVoice);
    sourceVoice->noteId = destinationNoteId;
    sourceVoice->midiNote = std::clamp (destinationMidiNote, 0, 127);
    sourceVoice->velocity = std::clamp (destinationVelocity, 0.0f, 1.0f);
    sourceVoice->pitchOffsetSemitones = 0.0;
    sourceVoice->slurStartPitchSemitones = startPitch;
    sourceVoice->slurCurveShape = curveShape;
    sourceVoice->slurSampleIndex = 0;
    sourceVoice->slurTotalSamples = std::max (0, static_cast<int> (std::llround (std::max (0.0, slurTimeSeconds) * sampleRate_)));
    sourceVoice->currentPitchSemitones = sourceVoice->slurTotalSamples == 0 ? static_cast<double> (sourceVoice->midiNote)
                                                                            : sourceVoice->slurStartPitchSemitones;
    sourceVoice->age = ++voiceAgeCounter_;
    return true;
}

bool SimpleOscComplexSynth::setVoicePitchOffset (SimpleOscComplexNoteId noteId, double offsetSemitones) noexcept
{
    auto* voice = findVoiceByNoteId (noteId);
    if (voice == nullptr)
        return false;

    voice->pitchOffsetSemitones = std::clamp (std::isfinite (offsetSemitones) ? offsetSemitones : 0.0, -48.0, 48.0);
    return true;
}

void SimpleOscComplexSynth::allNotesOff()
{
    for (auto& voice : voices_)
        if (voice.active())
        {
            voice.releaseStartLevel = voice.envelopeLevel;
            voice.stageSampleIndex = 0;
            voice.stage = EnvelopeStage::release;
        }
}

void SimpleOscComplexSynth::reset()
{
    for (auto& voice : voices_)
        voice = Voice {};
}

void SimpleOscComplexSynth::render (float* left, float* right, int numSamples)
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (auto sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        auto sample = 0.0;
        for (auto& voice : voices_)
            if (voice.active())
                sample += renderVoice (voice);

        sample = std::clamp (sample, -1.0, 1.0);
        left[sampleIndex] += static_cast<float> (sample);
        right[sampleIndex] += static_cast<float> (sample);
    }
}

std::size_t SimpleOscComplexSynth::activeVoiceCount() const noexcept
{
    return static_cast<std::size_t> (std::count_if (voices_.begin(), voices_.end(), [] (const auto& voice)
    {
        return voice.active();
    }));
}

std::size_t SimpleOscComplexSynth::activeVoiceCountForNoteId (SimpleOscComplexNoteId noteId) const noexcept
{
    return static_cast<std::size_t> (std::count_if (voices_.begin(), voices_.end(), [noteId] (const auto& voice)
    {
        return voice.active() && voice.noteId == noteId;
    }));
}

double SimpleOscComplexSynth::debugEnvelopeLevelForNoteId (SimpleOscComplexNoteId noteId) const noexcept
{
    if (const auto* voice = findVoiceByNoteId (noteId))
        return voice->envelopeLevel;

    return 0.0;
}

double SimpleOscComplexSynth::debugCurrentPitchSemitonesForNoteId (SimpleOscComplexNoteId noteId) const noexcept
{
    if (const auto* voice = findVoiceByNoteId (noteId))
        return voice->currentPitchSemitones;

    return 0.0;
}

SimpleOscComplexSynth::Voice& SimpleOscComplexSynth::allocateVoice()
{
    if (auto idle = std::find_if (voices_.begin(), voices_.end(), [] (const auto& voice) { return ! voice.active(); });
        idle != voices_.end())
        return *idle;

    const auto stageRank = [] (EnvelopeStage stage) noexcept
    {
        switch (stage)
        {
            case EnvelopeStage::release: return 0;
            case EnvelopeStage::sustain: return 1;
            case EnvelopeStage::decay: return 2;
            case EnvelopeStage::attack: return 3;
            case EnvelopeStage::idle: return 4;
        }

        return 4;
    };

    return *std::min_element (voices_.begin(), voices_.end(), [&stageRank] (const auto& lhs, const auto& rhs)
    {
        const auto lhsRank = stageRank (lhs.stage);
        const auto rhsRank = stageRank (rhs.stage);
        if (lhsRank != rhsRank)
            return lhsRank < rhsRank;

        return lhs.age < rhs.age;
    });
}

SimpleOscComplexSynth::Voice* SimpleOscComplexSynth::findVoiceByNoteId (SimpleOscComplexNoteId noteId) noexcept
{
    if (noteId == 0)
        return nullptr;

    const auto match = std::find_if (voices_.begin(), voices_.end(), [noteId] (const auto& voice)
    {
        return voice.active() && voice.noteId == noteId;
    });

    return match == voices_.end() ? nullptr : &*match;
}

const SimpleOscComplexSynth::Voice* SimpleOscComplexSynth::findVoiceByNoteId (SimpleOscComplexNoteId noteId) const noexcept
{
    if (noteId == 0)
        return nullptr;

    const auto match = std::find_if (voices_.begin(), voices_.end(), [noteId] (const auto& voice)
    {
        return voice.active() && voice.noteId == noteId;
    });

    return match == voices_.end() ? nullptr : &*match;
}

double SimpleOscComplexSynth::voicePitchSemitones (Voice& voice) noexcept
{
    const auto destinationPitch = static_cast<double> (voice.midiNote);
    auto slurOffset = 0.0;
    if (voice.slurring())
    {
        const auto alpha = static_cast<double> (voice.slurSampleIndex) / static_cast<double> (voice.slurTotalSamples);
        const auto curved = sequencing::evaluateExpressionCurve (voice.slurCurveShape, alpha);
        slurOffset = (voice.slurStartPitchSemitones - destinationPitch) * (1.0 - curved);
        ++voice.slurSampleIndex;
    }

    voice.currentPitchSemitones = destinationPitch + slurOffset + voice.pitchOffsetSemitones;
    return voice.currentPitchSemitones;
}

double SimpleOscComplexSynth::renderVoice (Voice& voice)
{
    const auto envelope = envelopeForVoice (voice);
    if (! voice.active())
        return 0.0;

    const auto voicePitch = voicePitchSemitones (voice);
    const auto frequencyHz = pitchFrequency (voicePitch + patch_.pitchSemitones);
    const auto ratio = simpleOscComplexModRatioForIndex (patch_.modRatioIndex);
    const auto modulator = std::sin (voice.modulatorPhase * 2.0 * std::numbers::pi);
    const auto phaseOffset = modulator * patch_.pmAmount * 0.24;
    const auto carrier = triangleWave (voice.carrierPhase + phaseOffset);
    const auto folded = wavefold (carrier, patch_.wavefolderAmount, patch_.wavefolderStages);
    const auto gain = patch_.ampLevel * static_cast<double> (voice.velocity) * 0.32;

    voice.carrierPhase = wrapPhase (voice.carrierPhase + (frequencyHz / sampleRate_));
    voice.modulatorPhase = wrapPhase (voice.modulatorPhase + ((frequencyHz * ratio) / sampleRate_));

    return folded * envelope * gain;
}

double SimpleOscComplexSynth::envelopeForVoice (Voice& voice)
{
    switch (voice.stage)
    {
        case EnvelopeStage::idle:
            voice.envelopeLevel = 0.0;
            return 0.0;

        case EnvelopeStage::attack:
        {
            const auto samples = samplesForStage (patch_.attackSeconds, sampleRate_);
            if (samples == 0)
            {
                voice.envelopeLevel = 1.0;
                voice.stageSampleIndex = 0;
                voice.stage = EnvelopeStage::decay;
                return voice.envelopeLevel;
            }

            voice.envelopeLevel = std::min (1.0, static_cast<double> (++voice.stageSampleIndex) / static_cast<double> (samples));
            if (voice.stageSampleIndex >= samples)
            {
                voice.envelopeLevel = 1.0;
                voice.stageSampleIndex = 0;
                voice.stage = EnvelopeStage::decay;
            }
            return voice.envelopeLevel;
        }

        case EnvelopeStage::decay:
        {
            const auto samples = samplesForStage (patch_.decaySeconds, sampleRate_);
            if (samples == 0)
            {
                voice.envelopeLevel = patch_.sustainLevel;
                voice.stageSampleIndex = 0;
                voice.stage = EnvelopeStage::sustain;
                return voice.envelopeLevel;
            }

            const auto progress = std::min (1.0, static_cast<double> (++voice.stageSampleIndex) / static_cast<double> (samples));
            voice.envelopeLevel = 1.0 + ((patch_.sustainLevel - 1.0) * progress);
            if (voice.stageSampleIndex >= samples)
            {
                voice.envelopeLevel = patch_.sustainLevel;
                voice.stageSampleIndex = 0;
                voice.stage = EnvelopeStage::sustain;
            }
            return voice.envelopeLevel;
        }

        case EnvelopeStage::sustain:
            voice.envelopeLevel = patch_.sustainLevel;
            return voice.envelopeLevel;

        case EnvelopeStage::release:
        {
            const auto samples = samplesForStage (patch_.releaseSeconds, sampleRate_);
            if (samples == 0)
            {
                voice = Voice {};
                return 0.0;
            }

            const auto progress = std::min (1.0, static_cast<double> (++voice.stageSampleIndex) / static_cast<double> (samples));
            voice.envelopeLevel = voice.releaseStartLevel * (1.0 - progress);
            if (voice.stageSampleIndex >= samples || voice.envelopeLevel <= 0.000001)
                voice = Voice {};
            return std::max (0.0, voice.envelopeLevel);
        }
    }

    return 0.0;
}

SimpleOscComplexPatch simpleOscComplexPatchFromState (const sequencing::FirstPartyDeviceState& state)
{
    SimpleOscComplexPatch patch;
    patch.pitchSemitones = parameterValueById ("pitch", normalizedValueFor (state, "pitch", defaultNormalizedValueFor ("pitch")));
    patch.ampLevel = parameterValueById ("amp.level", normalizedValueFor (state, "amp.level", defaultNormalizedValueFor ("amp.level")));
    patch.pmAmount = parameterValueById ("osc.pm.amount", normalizedValueFor (state, "osc.pm.amount", defaultNormalizedValueFor ("osc.pm.amount")));
    patch.modRatioIndex = static_cast<int> (parameterValueById ("osc.mod.ratio", normalizedValueFor (state, "osc.mod.ratio", defaultNormalizedValueFor ("osc.mod.ratio"))));
    patch.wavefolderAmount = parameterValueById ("wavefolder.amount", normalizedValueFor (state, "wavefolder.amount", defaultNormalizedValueFor ("wavefolder.amount")));
    patch.wavefolderStages = static_cast<int> (parameterValueById ("wavefolder.stages", normalizedValueFor (state, "wavefolder.stages", defaultNormalizedValueFor ("wavefolder.stages"))));
    patch.attackSeconds = parameterValueById ("amp.attack", normalizedValueFor (state, "amp.attack", defaultNormalizedValueFor ("amp.attack")));
    patch.decaySeconds = parameterValueById ("amp.decay", normalizedValueFor (state, "amp.decay", defaultNormalizedValueFor ("amp.decay")));
    patch.sustainLevel = parameterValueById ("amp.sustain", normalizedValueFor (state, "amp.sustain", defaultNormalizedValueFor ("amp.sustain")));
    patch.releaseSeconds = parameterValueById ("amp.release", normalizedValueFor (state, "amp.release", defaultNormalizedValueFor ("amp.release")));
    return patch;
}

double simpleOscComplexActualParameterValue (const char* parameterId, double normalizedValue)
{
    return parameterValueById (parameterId, normalizedValue);
}

double simpleOscComplexModRatioForIndex (int index) noexcept
{
    static constexpr std::array<double, 8> ratios { 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0 };
    return ratios[static_cast<std::size_t> (std::clamp (index, 0, static_cast<int> (ratios.size()) - 1))];
}
}
