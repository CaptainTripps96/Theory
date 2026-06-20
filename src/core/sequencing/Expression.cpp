#include "core/sequencing/Expression.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
void requireNonEmpty (const std::string& value, const char* message)
{
    if (value.empty())
        throw std::invalid_argument (message);
}

void requireFinite (double value, const char* message)
{
    if (! std::isfinite (value))
        throw std::invalid_argument (message);
}

void requireNonNegative (time::TickDuration duration, const char* message)
{
    if (duration.ticks() < 0)
        throw std::invalid_argument (message);
}

void requirePositiveRegion (const Region& region, const char* message)
{
    if (region.duration().ticks() <= 0)
        throw std::invalid_argument (message);
}

double clampUnipolar (double value) noexcept
{
    if (! std::isfinite (value))
        return 0.0;

    return std::clamp (value, 0.0, 1.0);
}

double clampForPolarity (double value, ExpressionLanePolarity polarity) noexcept
{
    if (! std::isfinite (value))
        return 0.0;

    if (polarity == ExpressionLanePolarity::bipolar)
        return std::clamp (value, -1.0, 1.0);

    return std::clamp (value, 0.0, 1.0);
}

void requireUniqueNonEmptyIds (const std::vector<std::string>& ids, const char* emptyMessage, const char* duplicateMessage)
{
    if (ids.empty())
        throw std::invalid_argument (emptyMessage);

    auto sortedIds = ids;
    std::sort (sortedIds.begin(), sortedIds.end());

    if (std::binary_search (sortedIds.begin(), sortedIds.end(), std::string {}))
        throw std::invalid_argument (emptyMessage);

    if (std::adjacent_find (sortedIds.begin(), sortedIds.end()) != sortedIds.end())
        throw std::invalid_argument (duplicateMessage);
}

time::TickDuration envelopeDuration (const EnvelopeStage& attack,
                                     const std::optional<EnvelopeStage>& decay,
                                     const std::optional<EnvelopeStage>& release)
{
    auto duration = attack.duration;
    if (decay.has_value())
        duration = duration + decay->duration;
    if (release.has_value())
        duration = duration + release->duration;
    return duration;
}

void requireStageType (const EnvelopeStage& stage, EnvelopeStageType expected, const char* message)
{
    if (stage.stageType != expected)
        throw std::invalid_argument (message);
}

template <typename Item, typename Id, typename Getter>
auto findById (std::vector<Item>& items, const Id& id, Getter getter)
{
    return std::find_if (items.begin(), items.end(), [&] (const auto& item) {
        return getter (item) == id;
    });
}

template <typename Item, typename Id, typename Getter>
auto findById (const std::vector<Item>& items, const Id& id, Getter getter)
{
    return std::find_if (items.begin(), items.end(), [&] (const auto& item) {
        return getter (item) == id;
    });
}

template <typename Item, typename Id, typename Getter>
Item removeById (std::vector<Item>& items, const Id& id, Getter getter, const char* missingMessage)
{
    const auto match = findById (items, id, getter);
    if (match == items.end())
        throw std::invalid_argument (missingMessage);

    auto removed = std::move (*match);
    items.erase (match);
    return removed;
}

std::string destinationKindPrefix (ExpressionDestinationKind kind)
{
    switch (kind)
    {
        case ExpressionDestinationKind::trackVolume: return "track-volume";
        case ExpressionDestinationKind::trackPan: return "track-pan";
        case ExpressionDestinationKind::pitch: return "pitch";
        case ExpressionDestinationKind::pitchBend: return "pitch-bend";
        case ExpressionDestinationKind::firstPartyParameter: return "first-party-parameter";
        case ExpressionDestinationKind::pluginParameter: return "plugin-parameter";
        case ExpressionDestinationKind::midiCc: return "midi-cc";
        case ExpressionDestinationKind::sendLevel: return "send-level";
    }

    return "unknown";
}
}

ExpressionLaneId::ExpressionLaneId (std::string id)
    : value (std::move (id))
{
    requireNonEmpty (value, "ExpressionLaneId requires a non-empty value");
}

bool ExpressionLaneId::empty() const noexcept { return value.empty(); }

ExpressionClipId::ExpressionClipId (std::string id)
    : value (std::move (id))
{
    requireNonEmpty (value, "ExpressionClipId requires a non-empty value");
}

bool ExpressionClipId::empty() const noexcept { return value.empty(); }

ExpressionRouteId::ExpressionRouteId (std::string id)
    : value (std::move (id))
{
    requireNonEmpty (value, "ExpressionRouteId requires a non-empty value");
}

bool ExpressionRouteId::empty() const noexcept { return value.empty(); }

ExpressionBlockId::ExpressionBlockId (std::string id)
    : value (std::move (id))
{
    requireNonEmpty (value, "ExpressionBlockId requires a non-empty value");
}

bool ExpressionBlockId::empty() const noexcept { return value.empty(); }

bool operator== (const ExpressionLaneId& lhs, const ExpressionLaneId& rhs) noexcept { return lhs.value == rhs.value; }
bool operator!= (const ExpressionLaneId& lhs, const ExpressionLaneId& rhs) noexcept { return ! (lhs == rhs); }
bool operator== (const ExpressionClipId& lhs, const ExpressionClipId& rhs) noexcept { return lhs.value == rhs.value; }
bool operator!= (const ExpressionClipId& lhs, const ExpressionClipId& rhs) noexcept { return ! (lhs == rhs); }
bool operator== (const ExpressionRouteId& lhs, const ExpressionRouteId& rhs) noexcept { return lhs.value == rhs.value; }
bool operator!= (const ExpressionRouteId& lhs, const ExpressionRouteId& rhs) noexcept { return ! (lhs == rhs); }
bool operator== (const ExpressionBlockId& lhs, const ExpressionBlockId& rhs) noexcept { return lhs.value == rhs.value; }
bool operator!= (const ExpressionBlockId& lhs, const ExpressionBlockId& rhs) noexcept { return ! (lhs == rhs); }

ExpressionDestination ExpressionDestination::trackVolume (std::string trackId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::trackVolume;
    destination.trackId = std::move (trackId);
    return destination;
}

ExpressionDestination ExpressionDestination::trackPan (std::string trackId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::trackPan;
    destination.trackId = std::move (trackId);
    return destination;
}

ExpressionDestination ExpressionDestination::pitch (std::string trackId, DeviceSlotId deviceSlotId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::pitch;
    destination.trackId = std::move (trackId);
    destination.deviceSlotId = std::move (deviceSlotId);
    return destination;
}

ExpressionDestination ExpressionDestination::pitchBend (std::string trackId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::pitchBend;
    destination.trackId = std::move (trackId);
    return destination;
}

ExpressionDestination ExpressionDestination::firstPartyParameter (std::string trackId,
                                                                  DeviceSlotId deviceSlotId,
                                                                  std::string parameterId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::firstPartyParameter;
    destination.trackId = std::move (trackId);
    destination.deviceSlotId = std::move (deviceSlotId);
    destination.parameterId = std::move (parameterId);
    return destination;
}

ExpressionDestination ExpressionDestination::pluginParameter (std::string trackId,
                                                              DeviceSlotId deviceSlotId,
                                                              std::string parameterId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::pluginParameter;
    destination.trackId = std::move (trackId);
    destination.deviceSlotId = std::move (deviceSlotId);
    destination.parameterId = std::move (parameterId);
    return destination;
}

ExpressionDestination ExpressionDestination::midiCc (std::string trackId, int ccNumber)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::midiCc;
    destination.trackId = std::move (trackId);
    destination.midiCcNumber = ccNumber;
    return destination;
}

ExpressionDestination ExpressionDestination::sendLevel (std::string trackId, std::string returnTrackId)
{
    ExpressionDestination destination;
    destination.kind = ExpressionDestinationKind::sendLevel;
    destination.trackId = std::move (trackId);
    destination.sendTargetTrackId = std::move (returnTrackId);
    return destination;
}

bool ExpressionDestination::isValid() const noexcept
{
    switch (kind)
    {
        case ExpressionDestinationKind::trackVolume:
        case ExpressionDestinationKind::trackPan:
        case ExpressionDestinationKind::pitch:
        case ExpressionDestinationKind::pitchBend:
            return ! trackId.empty();

        case ExpressionDestinationKind::firstPartyParameter:
        case ExpressionDestinationKind::pluginParameter:
            return ! trackId.empty() && ! deviceSlotId.empty() && ! parameterId.empty();

        case ExpressionDestinationKind::midiCc:
            return ! trackId.empty() && midiCcNumber >= 0 && midiCcNumber <= 127;

        case ExpressionDestinationKind::sendLevel:
            return ! trackId.empty() && ! sendTargetTrackId.empty();
    }

    return false;
}

std::string ExpressionDestination::stableId() const
{
    std::ostringstream stream;
    stream << destinationKindPrefix (kind) << ':' << trackId;

    if (! deviceSlotId.empty())
        stream << ':' << deviceSlotId.value;
    if (! parameterId.empty())
        stream << ':' << parameterId;
    if (kind == ExpressionDestinationKind::midiCc)
        stream << ':' << midiCcNumber;
    if (kind == ExpressionDestinationKind::sendLevel)
        stream << ':' << sendTargetTrackId;

    return stream.str();
}

bool operator== (const ExpressionDestination& lhs, const ExpressionDestination& rhs) noexcept
{
    return lhs.kind == rhs.kind
        && lhs.trackId == rhs.trackId
        && lhs.sendTargetTrackId == rhs.sendTargetTrackId
        && lhs.deviceSlotId == rhs.deviceSlotId
        && lhs.parameterId == rhs.parameterId
        && lhs.midiCcNumber == rhs.midiCcNumber;
}

bool operator!= (const ExpressionDestination& lhs, const ExpressionDestination& rhs) noexcept
{
    return ! (lhs == rhs);
}

ExpressionRoute::ExpressionRoute (ExpressionRouteId id,
                                  ExpressionDestination destination,
                                  double outputMin,
                                  double outputMax)
    : id_ (std::move (id)),
      destination_ (std::move (destination)),
      outputMin_ (outputMin),
      outputMax_ (outputMax)
{
    if (id_.empty())
        throw std::invalid_argument ("ExpressionRoute requires a non-empty ID");
    if (! destination_.isValid())
        throw std::invalid_argument ("ExpressionRoute requires a valid destination");
    requireFinite (outputMin, "ExpressionRoute output minimum must be finite");
    requireFinite (outputMax, "ExpressionRoute output maximum must be finite");
    setOutputRange (outputMin, outputMax);
}

const ExpressionRouteId& ExpressionRoute::id() const noexcept { return id_; }
const ExpressionDestination& ExpressionRoute::destination() const noexcept { return destination_; }
double ExpressionRoute::outputMin() const noexcept { return outputMin_; }
double ExpressionRoute::outputMax() const noexcept { return outputMax_; }
bool ExpressionRoute::enabled() const noexcept { return enabled_; }
void ExpressionRoute::setEnabled (bool enabled) noexcept { enabled_ = enabled; }

void ExpressionRoute::setOutputRange (double outputMin, double outputMax) noexcept
{
    if (! std::isfinite (outputMin) || ! std::isfinite (outputMax))
        return;

    outputMin_ = outputMin;
    outputMax_ = outputMax;
}

double ExpressionRoute::mapLaneValue (double laneValue, ExpressionLanePolarity polarity) const noexcept
{
    const auto clamped = clampForPolarity (laneValue, polarity);
    const auto normalized = polarity == ExpressionLanePolarity::bipolar ? (clamped + 1.0) * 0.5 : clamped;
    return outputMin_ + ((outputMax_ - outputMin_) * normalized);
}

EnvelopeStage::EnvelopeStage (EnvelopeStageType stageTypeToUse,
                              time::TickDuration durationToUse,
                              double startLevelToUse,
                              double endLevelToUse,
                              ExpressionCurveShape curveShapeToUse)
    : stageType (stageTypeToUse),
      duration (durationToUse),
      startLevel (startLevelToUse),
      endLevel (endLevelToUse),
      curveShape (curveShapeToUse)
{
    requireNonNegative (duration, "EnvelopeStage duration must not be negative");
    requireFinite (startLevel, "EnvelopeStage start level must be finite");
    requireFinite (endLevel, "EnvelopeStage end level must be finite");
}

PhraseEnvelopeClip::PhraseEnvelopeClip (ExpressionClipId id,
                                        std::vector<std::string> sourceNoteIds,
                                        Region phraseRegion,
                                        double storedLevel,
                                        EnvelopeStage attackStage)
    : id_ (std::move (id)),
      sourceNoteIds_ (std::move (sourceNoteIds)),
      phraseRegion_ (phraseRegion),
      storedLevel_ (storedLevel),
      attackStage_ (attackStage)
{
    if (id_.empty())
        throw std::invalid_argument ("PhraseEnvelopeClip requires a non-empty ID");
    requireUniqueNonEmptyIds (sourceNoteIds_,
                              "PhraseEnvelopeClip requires at least one source note ID",
                              "PhraseEnvelopeClip source note IDs must be unique");
    requirePositiveRegion (phraseRegion_, "PhraseEnvelopeClip phrase region must be positive");
    requireFinite (storedLevel_, "PhraseEnvelopeClip stored level must be finite");
    validateTiming();
}

const ExpressionClipId& PhraseEnvelopeClip::id() const noexcept { return id_; }
const std::vector<std::string>& PhraseEnvelopeClip::sourceNoteIds() const noexcept { return sourceNoteIds_; }
const Region& PhraseEnvelopeClip::phraseRegion() const noexcept { return phraseRegion_; }
double PhraseEnvelopeClip::storedLevel() const noexcept { return storedLevel_; }
void PhraseEnvelopeClip::setStoredLevel (double storedLevel, ExpressionLanePolarity polarity) noexcept { storedLevel_ = clampForPolarity (storedLevel, polarity); }
const EnvelopeStage& PhraseEnvelopeClip::attackStage() const noexcept { return attackStage_; }
const std::optional<EnvelopeStage>& PhraseEnvelopeClip::decayStage() const noexcept { return decayStage_; }
const std::optional<EnvelopeStage>& PhraseEnvelopeClip::releaseStage() const noexcept { return releaseStage_; }
const std::optional<double>& PhraseEnvelopeClip::peakLevel() const noexcept { return peakLevel_; }
const std::optional<double>& PhraseEnvelopeClip::sustainLevel() const noexcept { return sustainLevel_; }
const std::optional<time::TickDuration>& PhraseEnvelopeClip::tailExtension() const noexcept { return tailExtension_; }

void PhraseEnvelopeClip::setAttackStage (EnvelopeStage stage)
{
    const auto previous = attackStage_;
    attackStage_ = stage;
    try { validateTiming(); } catch (...) { attackStage_ = previous; throw; }
}

void PhraseEnvelopeClip::setDecayStage (std::optional<EnvelopeStage> stage)
{
    const auto previous = decayStage_;
    decayStage_ = stage;
    try { validateTiming(); } catch (...) { decayStage_ = previous; throw; }
}

void PhraseEnvelopeClip::setReleaseStage (std::optional<EnvelopeStage> stage)
{
    const auto previous = releaseStage_;
    releaseStage_ = stage;
    try { validateTiming(); } catch (...) { releaseStage_ = previous; throw; }
}

void PhraseEnvelopeClip::setPeakLevel (std::optional<double> level, ExpressionLanePolarity polarity)
{
    peakLevel_ = level.has_value() ? std::optional<double> { clampForPolarity (*level, polarity) } : std::nullopt;
}

void PhraseEnvelopeClip::setSustainLevel (std::optional<double> level, ExpressionLanePolarity polarity)
{
    sustainLevel_ = level.has_value() ? std::optional<double> { clampForPolarity (*level, polarity) } : std::nullopt;
}

void PhraseEnvelopeClip::setTailExtension (std::optional<time::TickDuration> tailExtension)
{
    if (tailExtension.has_value())
        requireNonNegative (*tailExtension, "PhraseEnvelopeClip tail extension must not be negative");

    tailExtension_ = tailExtension;
}

void PhraseEnvelopeClip::validateTiming() const
{
    requireStageType (attackStage_, EnvelopeStageType::attack, "PhraseEnvelopeClip attack stage has the wrong type");
    if (decayStage_.has_value())
        requireStageType (*decayStage_, EnvelopeStageType::decay, "PhraseEnvelopeClip decay stage has the wrong type");
    if (releaseStage_.has_value())
        requireStageType (*releaseStage_, EnvelopeStageType::release, "PhraseEnvelopeClip release stage has the wrong type");
    if (envelopeDuration (attackStage_, decayStage_, releaseStage_) > phraseRegion_.duration())
        throw std::invalid_argument ("PhraseEnvelopeClip envelope stages must fit inside the phrase region");
}

CyclicExpressionClip::CyclicExpressionClip (ExpressionClipId id,
                                            std::vector<std::string> sourceNoteIds,
                                            Region phraseRegion)
    : id_ (std::move (id)),
      sourceNoteIds_ (std::move (sourceNoteIds)),
      phraseRegion_ (phraseRegion)
{
    if (id_.empty())
        throw std::invalid_argument ("CyclicExpressionClip requires a non-empty ID");
    requireUniqueNonEmptyIds (sourceNoteIds_,
                              "CyclicExpressionClip requires at least one source note ID",
                              "CyclicExpressionClip source note IDs must be unique");
    requirePositiveRegion (phraseRegion_, "CyclicExpressionClip phrase region must be positive");
}

const ExpressionClipId& CyclicExpressionClip::id() const noexcept { return id_; }
const std::vector<std::string>& CyclicExpressionClip::sourceNoteIds() const noexcept { return sourceNoteIds_; }
const Region& CyclicExpressionClip::phraseRegion() const noexcept { return phraseRegion_; }
time::TickDuration CyclicExpressionClip::attackTime() const noexcept { return attackTime_; }
time::TickDuration CyclicExpressionClip::releaseTime() const noexcept { return releaseTime_; }
double CyclicExpressionClip::maxAmplitude() const noexcept { return maxAmplitude_; }
const std::string& CyclicExpressionClip::frequencyDivisionId() const noexcept { return frequencyDivisionId_; }
CyclicWaveShape CyclicExpressionClip::waveShape() const noexcept { return waveShape_; }
CyclicBlendMode CyclicExpressionClip::blendMode() const noexcept { return blendMode_; }
CyclicWavePolarityMode CyclicExpressionClip::wavePolarityMode() const noexcept { return wavePolarityMode_; }
double CyclicExpressionClip::phase() const noexcept { return phase_; }

void CyclicExpressionClip::setAttackTime (time::TickDuration attackTime)
{
    const auto previous = attackTime_;
    attackTime_ = attackTime;
    try { validateTiming(); } catch (...) { attackTime_ = previous; throw; }
}

void CyclicExpressionClip::setReleaseTime (time::TickDuration releaseTime)
{
    const auto previous = releaseTime_;
    releaseTime_ = releaseTime;
    try { validateTiming(); } catch (...) { releaseTime_ = previous; throw; }
}

void CyclicExpressionClip::setMaxAmplitude (double maxAmplitude) noexcept { maxAmplitude_ = clampUnipolar (maxAmplitude); }

void CyclicExpressionClip::setFrequencyDivisionId (std::string frequencyDivisionId)
{
    requireNonEmpty (frequencyDivisionId, "CyclicExpressionClip frequency division ID must not be empty");
    frequencyDivisionId_ = std::move (frequencyDivisionId);
}

void CyclicExpressionClip::setWaveShape (CyclicWaveShape waveShape) noexcept { waveShape_ = waveShape; }
void CyclicExpressionClip::setBlendMode (CyclicBlendMode blendMode) noexcept { blendMode_ = blendMode; }
void CyclicExpressionClip::setWavePolarityMode (CyclicWavePolarityMode wavePolarityMode) noexcept { wavePolarityMode_ = wavePolarityMode; }
void CyclicExpressionClip::setPhase (double phase) noexcept { phase_ = std::isfinite (phase) ? phase : 0.0; }

void CyclicExpressionClip::validateTiming() const
{
    requireNonNegative (attackTime_, "CyclicExpressionClip attack time must not be negative");
    requireNonNegative (releaseTime_, "CyclicExpressionClip release time must not be negative");
    if (attackTime_ + releaseTime_ > phraseRegion_.duration())
        throw std::invalid_argument ("CyclicExpressionClip attack and release must fit inside the phrase region");
}

PitchSlur::PitchSlur (ExpressionClipId id, std::string sourceNoteId, std::string destinationNoteId)
    : id_ (std::move (id)),
      sourceNoteId_ (std::move (sourceNoteId)),
      destinationNoteId_ (std::move (destinationNoteId))
{
    if (id_.empty())
        throw std::invalid_argument ("PitchSlur requires a non-empty ID");
    requireNonEmpty (sourceNoteId_, "PitchSlur source note ID must not be empty");
    requireNonEmpty (destinationNoteId_, "PitchSlur destination note ID must not be empty");
    if (sourceNoteId_ == destinationNoteId_)
        throw std::invalid_argument ("PitchSlur source and destination notes must differ");
}

const ExpressionClipId& PitchSlur::id() const noexcept { return id_; }
const std::string& PitchSlur::sourceNoteId() const noexcept { return sourceNoteId_; }
const std::string& PitchSlur::destinationNoteId() const noexcept { return destinationNoteId_; }
time::TickDuration PitchSlur::slurTime() const noexcept { return slurTime_; }
ExpressionCurveShape PitchSlur::curveShape() const noexcept { return curveShape_; }
bool PitchSlur::legatoNoRetrigger() const noexcept { return legatoNoRetrigger_; }
const std::optional<ExpressionBlockId>& PitchSlur::blockId() const noexcept { return blockId_; }
bool PitchSlur::hasVoiceOverride() const noexcept { return hasVoiceOverride_; }

void PitchSlur::setSlurTime (time::TickDuration slurTime)
{
    requireNonNegative (slurTime, "PitchSlur slur time must not be negative");
    slurTime_ = slurTime;
}

void PitchSlur::setCurveShape (ExpressionCurveShape curveShape) noexcept { curveShape_ = curveShape; }
void PitchSlur::setLegatoNoRetrigger (bool legatoNoRetrigger) noexcept { legatoNoRetrigger_ = legatoNoRetrigger; }

void PitchSlur::setBlockId (std::optional<ExpressionBlockId> blockId)
{
    if (blockId.has_value() && blockId->empty())
        throw std::invalid_argument ("PitchSlur block ID must not be empty");

    blockId_ = std::move (blockId);
}

void PitchSlur::setHasVoiceOverride (bool hasVoiceOverride) noexcept { hasVoiceOverride_ = hasVoiceOverride; }

VibratoExpression::VibratoExpression (ExpressionClipId id,
                                      std::vector<std::string> sourceNoteIds,
                                      Region phraseRegion)
    : id_ (std::move (id)),
      sourceNoteIds_ (std::move (sourceNoteIds)),
      phraseRegion_ (phraseRegion)
{
    if (id_.empty())
        throw std::invalid_argument ("VibratoExpression requires a non-empty ID");
    requireUniqueNonEmptyIds (sourceNoteIds_,
                              "VibratoExpression requires at least one source note ID",
                              "VibratoExpression source note IDs must be unique");
    requirePositiveRegion (phraseRegion_, "VibratoExpression phrase region must be positive");
}

const ExpressionClipId& VibratoExpression::id() const noexcept { return id_; }
const std::vector<std::string>& VibratoExpression::sourceNoteIds() const noexcept { return sourceNoteIds_; }
const Region& VibratoExpression::phraseRegion() const noexcept { return phraseRegion_; }
time::TickDuration VibratoExpression::attackTime() const noexcept { return attackTime_; }
time::TickDuration VibratoExpression::releaseTime() const noexcept { return releaseTime_; }
double VibratoExpression::amplitudeSemitones() const noexcept { return amplitudeSemitones_; }
const std::string& VibratoExpression::frequencyDivisionId() const noexcept { return frequencyDivisionId_; }
CyclicWaveShape VibratoExpression::waveShape() const noexcept { return waveShape_; }
double VibratoExpression::phase() const noexcept { return phase_; }
const std::optional<ExpressionBlockId>& VibratoExpression::blockId() const noexcept { return blockId_; }
const std::vector<VibratoVoiceOverride>& VibratoExpression::voiceOverrides() const noexcept { return voiceOverrides_; }

void VibratoExpression::setAttackTime (time::TickDuration attackTime)
{
    const auto previous = attackTime_;
    attackTime_ = attackTime;
    try { validateTiming(); } catch (...) { attackTime_ = previous; throw; }
}

void VibratoExpression::setReleaseTime (time::TickDuration releaseTime)
{
    const auto previous = releaseTime_;
    releaseTime_ = releaseTime;
    try { validateTiming(); } catch (...) { releaseTime_ = previous; throw; }
}

void VibratoExpression::setAmplitudeSemitones (double amplitudeSemitones) noexcept
{
    amplitudeSemitones_ = std::isfinite (amplitudeSemitones) ? std::max (0.0, amplitudeSemitones) : 0.0;
}

void VibratoExpression::setFrequencyDivisionId (std::string frequencyDivisionId)
{
    requireNonEmpty (frequencyDivisionId, "VibratoExpression frequency division ID must not be empty");
    frequencyDivisionId_ = std::move (frequencyDivisionId);
}

void VibratoExpression::setWaveShape (CyclicWaveShape waveShape) noexcept { waveShape_ = waveShape; }
void VibratoExpression::setPhase (double phase) noexcept { phase_ = std::isfinite (phase) ? phase : 0.0; }

void VibratoExpression::setBlockId (std::optional<ExpressionBlockId> blockId)
{
    if (blockId.has_value() && blockId->empty())
        throw std::invalid_argument ("VibratoExpression block ID must not be empty");

    blockId_ = std::move (blockId);
}

void VibratoExpression::setVoiceOverrides (std::vector<VibratoVoiceOverride> voiceOverrides)
{
    std::vector<std::string> overrideNoteIds;
    overrideNoteIds.reserve (voiceOverrides.size());

    for (const auto& override : voiceOverrides)
    {
        overrideNoteIds.push_back (override.noteId);
        requireFinite (override.amplitudeSemitones, "VibratoExpression voice override amplitude must be finite");
        requireNonNegative (override.attackTime, "VibratoExpression voice override attack must not be negative");
        requireNonNegative (override.releaseTime, "VibratoExpression voice override release must not be negative");
        requireNonEmpty (override.frequencyDivisionId, "VibratoExpression voice override frequency division ID must not be empty");
    }

    if (! overrideNoteIds.empty())
        requireUniqueNonEmptyIds (overrideNoteIds,
                                  "VibratoExpression voice override note ID must not be empty",
                                  "VibratoExpression voice override note IDs must be unique");

    voiceOverrides_ = std::move (voiceOverrides);
}

void VibratoExpression::validateTiming() const
{
    requireNonNegative (attackTime_, "VibratoExpression attack time must not be negative");
    requireNonNegative (releaseTime_, "VibratoExpression release time must not be negative");
    if (attackTime_ + releaseTime_ > phraseRegion_.duration())
        throw std::invalid_argument ("VibratoExpression attack and release must fit inside the phrase region");
}

ExpressionLane::ExpressionLane (ExpressionLaneId id, std::string name, ExpressionLanePolarity polarity)
    : id_ (std::move (id)),
      name_ (std::move (name)),
      polarity_ (polarity)
{
    if (id_.empty())
        throw std::invalid_argument ("ExpressionLane requires a non-empty ID");
    requireNonEmpty (name_, "ExpressionLane requires a non-empty name");
}

const ExpressionLaneId& ExpressionLane::id() const noexcept { return id_; }
const std::string& ExpressionLane::name() const noexcept { return name_; }
ExpressionLanePolarity ExpressionLane::polarity() const noexcept { return polarity_; }
bool ExpressionLane::enabled() const noexcept { return enabled_; }

void ExpressionLane::rename (std::string name)
{
    requireNonEmpty (name, "ExpressionLane name must not be empty");
    name_ = std::move (name);
}

void ExpressionLane::setPolarity (ExpressionLanePolarity polarity) noexcept { polarity_ = polarity; }
void ExpressionLane::setEnabled (bool enabled) noexcept { enabled_ = enabled; }
double ExpressionLane::clampValue (double value) const noexcept { return clampForPolarity (value, polarity_); }
const std::vector<ExpressionRoute>& ExpressionLane::routes() const noexcept { return routes_; }

ExpressionRoute* ExpressionLane::findRoute (const ExpressionRouteId& routeId) noexcept
{
    const auto match = findById (routes_, routeId, [] (const auto& route) -> const auto& { return route.id(); });
    return match == routes_.end() ? nullptr : &*match;
}

const ExpressionRoute* ExpressionLane::findRoute (const ExpressionRouteId& routeId) const noexcept
{
    const auto match = findById (routes_, routeId, [] (const auto& route) -> const auto& { return route.id(); });
    return match == routes_.end() ? nullptr : &*match;
}

void ExpressionLane::addRoute (ExpressionRoute route)
{
    if (findRoute (route.id()) != nullptr)
        throw std::invalid_argument ("ExpressionLane already contains a route with this ID");

    routes_.push_back (std::move (route));
}

ExpressionRoute ExpressionLane::removeRoute (const ExpressionRouteId& routeId)
{
    return removeById (routes_, routeId, [] (const auto& route) -> const auto& { return route.id(); }, "ExpressionLane does not contain a route with this ID");
}

const std::vector<PhraseEnvelopeClip>& ExpressionLane::phraseEnvelopeClips() const noexcept { return phraseEnvelopeClips_; }

PhraseEnvelopeClip* ExpressionLane::findPhraseEnvelopeClip (const ExpressionClipId& clipId) noexcept
{
    const auto match = findById (phraseEnvelopeClips_, clipId, [] (const auto& clip) -> const auto& { return clip.id(); });
    return match == phraseEnvelopeClips_.end() ? nullptr : &*match;
}

const PhraseEnvelopeClip* ExpressionLane::findPhraseEnvelopeClip (const ExpressionClipId& clipId) const noexcept
{
    const auto match = findById (phraseEnvelopeClips_, clipId, [] (const auto& clip) -> const auto& { return clip.id(); });
    return match == phraseEnvelopeClips_.end() ? nullptr : &*match;
}

void ExpressionLane::addPhraseEnvelopeClip (PhraseEnvelopeClip clip)
{
    if (findPhraseEnvelopeClip (clip.id()) != nullptr)
        throw std::invalid_argument ("ExpressionLane already contains a phrase envelope clip with this ID");

    phraseEnvelopeClips_.push_back (std::move (clip));
}

PhraseEnvelopeClip ExpressionLane::removePhraseEnvelopeClip (const ExpressionClipId& clipId)
{
    return removeById (phraseEnvelopeClips_, clipId, [] (const auto& clip) -> const auto& { return clip.id(); }, "ExpressionLane does not contain a phrase envelope clip with this ID");
}

const std::vector<CyclicExpressionClip>& ExpressionLane::cyclicClips() const noexcept { return cyclicClips_; }

void ExpressionLane::addCyclicClip (CyclicExpressionClip clip)
{
    const auto duplicate = findById (cyclicClips_, clip.id(), [] (const auto& existingClip) -> const auto& { return existingClip.id(); });
    if (duplicate != cyclicClips_.end())
        throw std::invalid_argument ("ExpressionLane already contains a cyclic clip with this ID");

    const auto overlap = std::any_of (cyclicClips_.begin(), cyclicClips_.end(), [&] (const auto& existingClip) {
        return existingClip.phraseRegion().intersects (clip.phraseRegion());
    });
    if (overlap)
        throw std::invalid_argument ("ExpressionLane cyclic clips must not overlap");

    cyclicClips_.push_back (std::move (clip));
}

CyclicExpressionClip ExpressionLane::removeCyclicClip (const ExpressionClipId& clipId)
{
    return removeById (cyclicClips_, clipId, [] (const auto& clip) -> const auto& { return clip.id(); }, "ExpressionLane does not contain a cyclic clip with this ID");
}

const std::vector<PitchSlur>& ExpressionLane::pitchSlurs() const noexcept { return pitchSlurs_; }

PitchSlur* ExpressionLane::findPitchSlur (const ExpressionClipId& slurId) noexcept
{
    const auto match = findById (pitchSlurs_, slurId, [] (const auto& slur) -> const auto& { return slur.id(); });
    return match == pitchSlurs_.end() ? nullptr : &*match;
}

const PitchSlur* ExpressionLane::findPitchSlur (const ExpressionClipId& slurId) const noexcept
{
    const auto match = findById (pitchSlurs_, slurId, [] (const auto& slur) -> const auto& { return slur.id(); });
    return match == pitchSlurs_.end() ? nullptr : &*match;
}

void ExpressionLane::addPitchSlur (PitchSlur slur)
{
    if (findPitchSlur (slur.id()) != nullptr)
        throw std::invalid_argument ("ExpressionLane already contains a pitch slur with this ID");

    pitchSlurs_.push_back (std::move (slur));
}

PitchSlur ExpressionLane::removePitchSlur (const ExpressionClipId& slurId)
{
    return removeById (pitchSlurs_, slurId, [] (const auto& slur) -> const auto& { return slur.id(); }, "ExpressionLane does not contain a pitch slur with this ID");
}

const std::vector<VibratoExpression>& ExpressionLane::vibratoExpressions() const noexcept { return vibratoExpressions_; }

VibratoExpression* ExpressionLane::findVibratoExpression (const ExpressionClipId& vibratoId) noexcept
{
    const auto match = findById (vibratoExpressions_, vibratoId, [] (const auto& vibrato) -> const auto& { return vibrato.id(); });
    return match == vibratoExpressions_.end() ? nullptr : &*match;
}

const VibratoExpression* ExpressionLane::findVibratoExpression (const ExpressionClipId& vibratoId) const noexcept
{
    const auto match = findById (vibratoExpressions_, vibratoId, [] (const auto& vibrato) -> const auto& { return vibrato.id(); });
    return match == vibratoExpressions_.end() ? nullptr : &*match;
}

void ExpressionLane::addVibratoExpression (VibratoExpression vibrato)
{
    if (findVibratoExpression (vibrato.id()) != nullptr)
        throw std::invalid_argument ("ExpressionLane already contains a vibrato expression with this ID");

    vibratoExpressions_.push_back (std::move (vibrato));
}

VibratoExpression ExpressionLane::removeVibratoExpression (const ExpressionClipId& vibratoId)
{
    return removeById (vibratoExpressions_, vibratoId, [] (const auto& vibrato) -> const auto& { return vibrato.id(); }, "ExpressionLane does not contain a vibrato expression with this ID");
}

ExpressionState::ExpressionState()
{
    lanes_.emplace_back (defaultVolumeLaneId(), "Volume", ExpressionLanePolarity::unipolar);
    lanes_.emplace_back (defaultPitchLaneId(), "Pitch", ExpressionLanePolarity::bipolar);
}

ExpressionLaneId ExpressionState::defaultVolumeLaneId()
{
    return ExpressionLaneId { "expr-volume" };
}

ExpressionLaneId ExpressionState::defaultPitchLaneId()
{
    return ExpressionLaneId { "expr-pitch" };
}

const std::vector<ExpressionLane>& ExpressionState::lanes() const noexcept { return lanes_; }

ExpressionLane* ExpressionState::findLane (const ExpressionLaneId& laneId) noexcept
{
    const auto match = findById (lanes_, laneId, [] (const auto& lane) -> const auto& { return lane.id(); });
    return match == lanes_.end() ? nullptr : &*match;
}

const ExpressionLane* ExpressionState::findLane (const ExpressionLaneId& laneId) const noexcept
{
    const auto match = findById (lanes_, laneId, [] (const auto& lane) -> const auto& { return lane.id(); });
    return match == lanes_.end() ? nullptr : &*match;
}

void ExpressionState::addLane (ExpressionLane lane)
{
    if (findLane (lane.id()) != nullptr)
        throw std::invalid_argument ("ExpressionState already contains a lane with this ID");

    lanes_.push_back (std::move (lane));
}

ExpressionLane ExpressionState::removeLane (const ExpressionLaneId& laneId)
{
    if (laneId == defaultVolumeLaneId() || laneId == defaultPitchLaneId())
        throw std::invalid_argument ("ExpressionState default lanes cannot be removed");

    return removeById (lanes_, laneId, [] (const auto& lane) -> const auto& { return lane.id(); }, "ExpressionState does not contain a lane with this ID");
}
}
