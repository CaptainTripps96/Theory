#pragma once

#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Region.h"
#include "core/time/Tick.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::sequencing
{
struct ExpressionLaneId
{
    std::string value;

    ExpressionLaneId() = default;
    explicit ExpressionLaneId (std::string id);
    bool empty() const noexcept;
};

struct ExpressionClipId
{
    std::string value;

    ExpressionClipId() = default;
    explicit ExpressionClipId (std::string id);
    bool empty() const noexcept;
};

struct ExpressionRouteId
{
    std::string value;

    ExpressionRouteId() = default;
    explicit ExpressionRouteId (std::string id);
    bool empty() const noexcept;
};

struct ExpressionBlockId
{
    std::string value;

    ExpressionBlockId() = default;
    explicit ExpressionBlockId (std::string id);
    bool empty() const noexcept;
};

bool operator== (const ExpressionLaneId& lhs, const ExpressionLaneId& rhs) noexcept;
bool operator!= (const ExpressionLaneId& lhs, const ExpressionLaneId& rhs) noexcept;
bool operator== (const ExpressionClipId& lhs, const ExpressionClipId& rhs) noexcept;
bool operator!= (const ExpressionClipId& lhs, const ExpressionClipId& rhs) noexcept;
bool operator== (const ExpressionRouteId& lhs, const ExpressionRouteId& rhs) noexcept;
bool operator!= (const ExpressionRouteId& lhs, const ExpressionRouteId& rhs) noexcept;
bool operator== (const ExpressionBlockId& lhs, const ExpressionBlockId& rhs) noexcept;
bool operator!= (const ExpressionBlockId& lhs, const ExpressionBlockId& rhs) noexcept;

enum class ExpressionLanePolarity
{
    unipolar,
    bipolar
};

enum class ExpressionCurveShape
{
    linear,
    logarithmic,
    exponential
};

enum class ExpressionDestinationKind
{
    trackVolume,
    trackPan,
    pitch,
    pitchBend,
    firstPartyParameter,
    pluginParameter,
    midiCc,
    sendLevel
};

struct ExpressionDestination
{
    ExpressionDestinationKind kind = ExpressionDestinationKind::trackVolume;
    std::string trackId;
    std::string sendTargetTrackId;
    DeviceSlotId deviceSlotId;
    std::string parameterId;
    int midiCcNumber = -1;

    static ExpressionDestination trackVolume (std::string trackId);
    static ExpressionDestination trackPan (std::string trackId);
    static ExpressionDestination pitch (std::string trackId, DeviceSlotId deviceSlotId = {});
    static ExpressionDestination pitchBend (std::string trackId);
    static ExpressionDestination firstPartyParameter (std::string trackId,
                                                      DeviceSlotId deviceSlotId,
                                                      std::string parameterId);
    static ExpressionDestination pluginParameter (std::string trackId,
                                                  DeviceSlotId deviceSlotId,
                                                  std::string parameterId);
    static ExpressionDestination midiCc (std::string trackId, int ccNumber);
    static ExpressionDestination sendLevel (std::string trackId, std::string returnTrackId);

    bool isValid() const noexcept;
    std::string stableId() const;
};

bool operator== (const ExpressionDestination& lhs, const ExpressionDestination& rhs) noexcept;
bool operator!= (const ExpressionDestination& lhs, const ExpressionDestination& rhs) noexcept;

class ExpressionRoute
{
public:
    ExpressionRoute (ExpressionRouteId id,
                     ExpressionDestination destination,
                     double outputMin,
                     double outputMax);

    const ExpressionRouteId& id() const noexcept;
    const ExpressionDestination& destination() const noexcept;
    double outputMin() const noexcept;
    double outputMax() const noexcept;
    bool enabled() const noexcept;
    void setEnabled (bool enabled) noexcept;
    void setOutputRange (double outputMin, double outputMax) noexcept;
    double mapLaneValue (double laneValue, ExpressionLanePolarity polarity) const noexcept;

private:
    ExpressionRouteId id_;
    ExpressionDestination destination_;
    double outputMin_ = 0.0;
    double outputMax_ = 1.0;
    bool enabled_ = true;
};

enum class EnvelopeStageType
{
    attack,
    decay,
    release
};

struct EnvelopeStage
{
    EnvelopeStageType stageType = EnvelopeStageType::attack;
    time::TickDuration duration {};
    double startLevel = 0.0;
    double endLevel = 1.0;
    ExpressionCurveShape curveShape = ExpressionCurveShape::linear;

    EnvelopeStage() = default;
    EnvelopeStage (EnvelopeStageType stageType,
                   time::TickDuration duration,
                   double startLevel,
                   double endLevel,
                   ExpressionCurveShape curveShape = ExpressionCurveShape::linear);
};

class PhraseEnvelopeClip
{
public:
    PhraseEnvelopeClip (ExpressionClipId id,
                        std::vector<std::string> sourceNoteIds,
                        Region phraseRegion,
                        double storedLevel,
                        EnvelopeStage attackStage);

    const ExpressionClipId& id() const noexcept;
    const std::vector<std::string>& sourceNoteIds() const noexcept;
    const Region& phraseRegion() const noexcept;
    double storedLevel() const noexcept;
    void setStoredLevel (double storedLevel, ExpressionLanePolarity polarity) noexcept;

    const EnvelopeStage& attackStage() const noexcept;
    const std::optional<EnvelopeStage>& decayStage() const noexcept;
    const std::optional<EnvelopeStage>& releaseStage() const noexcept;
    const std::optional<double>& peakLevel() const noexcept;
    const std::optional<double>& sustainLevel() const noexcept;
    const std::optional<time::TickDuration>& tailExtension() const noexcept;

    void setAttackStage (EnvelopeStage stage);
    void setDecayStage (std::optional<EnvelopeStage> stage);
    void setReleaseStage (std::optional<EnvelopeStage> stage);
    void setPeakLevel (std::optional<double> level, ExpressionLanePolarity polarity);
    void setSustainLevel (std::optional<double> level, ExpressionLanePolarity polarity);
    void setTailExtension (std::optional<time::TickDuration> tailExtension);

private:
    ExpressionClipId id_;
    std::vector<std::string> sourceNoteIds_;
    Region phraseRegion_;
    double storedLevel_ = 0.0;
    EnvelopeStage attackStage_;
    std::optional<EnvelopeStage> decayStage_;
    std::optional<EnvelopeStage> releaseStage_;
    std::optional<double> peakLevel_;
    std::optional<double> sustainLevel_;
    std::optional<time::TickDuration> tailExtension_;

    void validateTiming() const;
};

enum class CyclicWaveShape
{
    sine,
    triangle,
    rampUp,
    rampDown,
    square
};

enum class CyclicBlendMode
{
    additive,
    multiplicative
};

enum class CyclicWavePolarityMode
{
    positiveOscillator,
    halfWaveRectified
};

class CyclicExpressionClip
{
public:
    CyclicExpressionClip (ExpressionClipId id,
                          std::vector<std::string> sourceNoteIds,
                          Region phraseRegion);

    const ExpressionClipId& id() const noexcept;
    const std::vector<std::string>& sourceNoteIds() const noexcept;
    const Region& phraseRegion() const noexcept;

    time::TickDuration attackTime() const noexcept;
    time::TickDuration releaseTime() const noexcept;
    double maxAmplitude() const noexcept;
    const std::string& frequencyDivisionId() const noexcept;
    CyclicWaveShape waveShape() const noexcept;
    CyclicBlendMode blendMode() const noexcept;
    CyclicWavePolarityMode wavePolarityMode() const noexcept;
    double phase() const noexcept;

    void setAttackTime (time::TickDuration attackTime);
    void setReleaseTime (time::TickDuration releaseTime);
    void setMaxAmplitude (double maxAmplitude) noexcept;
    void setFrequencyDivisionId (std::string frequencyDivisionId);
    void setWaveShape (CyclicWaveShape waveShape) noexcept;
    void setBlendMode (CyclicBlendMode blendMode) noexcept;
    void setWavePolarityMode (CyclicWavePolarityMode wavePolarityMode) noexcept;
    void setPhase (double phase) noexcept;

private:
    ExpressionClipId id_;
    std::vector<std::string> sourceNoteIds_;
    Region phraseRegion_;
    time::TickDuration attackTime_ {};
    time::TickDuration releaseTime_ {};
    double maxAmplitude_ = 0.0;
    std::string frequencyDivisionId_ = "sixteenth";
    CyclicWaveShape waveShape_ = CyclicWaveShape::sine;
    CyclicBlendMode blendMode_ = CyclicBlendMode::additive;
    CyclicWavePolarityMode wavePolarityMode_ = CyclicWavePolarityMode::positiveOscillator;
    double phase_ = 0.0;

    void validateTiming() const;
};

class PitchSlur
{
public:
    PitchSlur (ExpressionClipId id, std::string sourceNoteId, std::string destinationNoteId);

    const ExpressionClipId& id() const noexcept;
    const std::string& sourceNoteId() const noexcept;
    const std::string& destinationNoteId() const noexcept;
    time::TickDuration slurTime() const noexcept;
    ExpressionCurveShape curveShape() const noexcept;
    bool legatoNoRetrigger() const noexcept;
    const std::optional<ExpressionBlockId>& blockId() const noexcept;
    bool hasVoiceOverride() const noexcept;

    void setSlurTime (time::TickDuration slurTime);
    void setCurveShape (ExpressionCurveShape curveShape) noexcept;
    void setLegatoNoRetrigger (bool legatoNoRetrigger) noexcept;
    void setBlockId (std::optional<ExpressionBlockId> blockId);
    void setHasVoiceOverride (bool hasVoiceOverride) noexcept;

private:
    ExpressionClipId id_;
    std::string sourceNoteId_;
    std::string destinationNoteId_;
    time::TickDuration slurTime_ {};
    ExpressionCurveShape curveShape_ = ExpressionCurveShape::linear;
    bool legatoNoRetrigger_ = true;
    std::optional<ExpressionBlockId> blockId_;
    bool hasVoiceOverride_ = false;
};

struct VibratoVoiceOverride
{
    std::string noteId;
    double amplitudeSemitones = 0.0;
    time::TickDuration attackTime {};
    time::TickDuration releaseTime {};
    std::string frequencyDivisionId = "sixteenth";
    CyclicWaveShape waveShape = CyclicWaveShape::sine;
    double phase = 0.0;
};

class VibratoExpression
{
public:
    VibratoExpression (ExpressionClipId id,
                       std::vector<std::string> sourceNoteIds,
                       Region phraseRegion);

    const ExpressionClipId& id() const noexcept;
    const std::vector<std::string>& sourceNoteIds() const noexcept;
    const Region& phraseRegion() const noexcept;
    time::TickDuration attackTime() const noexcept;
    time::TickDuration releaseTime() const noexcept;
    double amplitudeSemitones() const noexcept;
    const std::string& frequencyDivisionId() const noexcept;
    CyclicWaveShape waveShape() const noexcept;
    double phase() const noexcept;
    const std::optional<ExpressionBlockId>& blockId() const noexcept;
    const std::vector<VibratoVoiceOverride>& voiceOverrides() const noexcept;

    void setAttackTime (time::TickDuration attackTime);
    void setReleaseTime (time::TickDuration releaseTime);
    void setAmplitudeSemitones (double amplitudeSemitones) noexcept;
    void setFrequencyDivisionId (std::string frequencyDivisionId);
    void setWaveShape (CyclicWaveShape waveShape) noexcept;
    void setPhase (double phase) noexcept;
    void setBlockId (std::optional<ExpressionBlockId> blockId);
    void setVoiceOverrides (std::vector<VibratoVoiceOverride> voiceOverrides);

private:
    ExpressionClipId id_;
    std::vector<std::string> sourceNoteIds_;
    Region phraseRegion_;
    time::TickDuration attackTime_ {};
    time::TickDuration releaseTime_ {};
    double amplitudeSemitones_ = 0.0;
    std::string frequencyDivisionId_ = "sixteenth";
    CyclicWaveShape waveShape_ = CyclicWaveShape::sine;
    double phase_ = 0.0;
    std::optional<ExpressionBlockId> blockId_;
    std::vector<VibratoVoiceOverride> voiceOverrides_;

    void validateTiming() const;
};

class ExpressionLane
{
public:
    ExpressionLane (ExpressionLaneId id, std::string name, ExpressionLanePolarity polarity);

    const ExpressionLaneId& id() const noexcept;
    const std::string& name() const noexcept;
    ExpressionLanePolarity polarity() const noexcept;
    bool enabled() const noexcept;
    void rename (std::string name);
    void setPolarity (ExpressionLanePolarity polarity) noexcept;
    void setEnabled (bool enabled) noexcept;

    double clampValue (double value) const noexcept;

    const std::vector<ExpressionRoute>& routes() const noexcept;
    ExpressionRoute* findRoute (const ExpressionRouteId& routeId) noexcept;
    const ExpressionRoute* findRoute (const ExpressionRouteId& routeId) const noexcept;
    void addRoute (ExpressionRoute route);
    ExpressionRoute removeRoute (const ExpressionRouteId& routeId);

    const std::vector<PhraseEnvelopeClip>& phraseEnvelopeClips() const noexcept;
    PhraseEnvelopeClip* findPhraseEnvelopeClip (const ExpressionClipId& clipId) noexcept;
    const PhraseEnvelopeClip* findPhraseEnvelopeClip (const ExpressionClipId& clipId) const noexcept;
    void addPhraseEnvelopeClip (PhraseEnvelopeClip clip);
    PhraseEnvelopeClip removePhraseEnvelopeClip (const ExpressionClipId& clipId);

    const std::vector<CyclicExpressionClip>& cyclicClips() const noexcept;
    void addCyclicClip (CyclicExpressionClip clip);
    CyclicExpressionClip removeCyclicClip (const ExpressionClipId& clipId);

    const std::vector<PitchSlur>& pitchSlurs() const noexcept;
    PitchSlur* findPitchSlur (const ExpressionClipId& slurId) noexcept;
    const PitchSlur* findPitchSlur (const ExpressionClipId& slurId) const noexcept;
    void addPitchSlur (PitchSlur slur);
    PitchSlur removePitchSlur (const ExpressionClipId& slurId);

    const std::vector<VibratoExpression>& vibratoExpressions() const noexcept;
    VibratoExpression* findVibratoExpression (const ExpressionClipId& vibratoId) noexcept;
    const VibratoExpression* findVibratoExpression (const ExpressionClipId& vibratoId) const noexcept;
    void addVibratoExpression (VibratoExpression vibrato);
    VibratoExpression removeVibratoExpression (const ExpressionClipId& vibratoId);

private:
    ExpressionLaneId id_;
    std::string name_;
    ExpressionLanePolarity polarity_ = ExpressionLanePolarity::unipolar;
    bool enabled_ = true;
    std::vector<ExpressionRoute> routes_;
    std::vector<PhraseEnvelopeClip> phraseEnvelopeClips_;
    std::vector<CyclicExpressionClip> cyclicClips_;
    std::vector<PitchSlur> pitchSlurs_;
    std::vector<VibratoExpression> vibratoExpressions_;
};

class ExpressionState
{
public:
    ExpressionState();

    static ExpressionLaneId defaultVolumeLaneId();
    static ExpressionLaneId defaultPitchLaneId();

    const std::vector<ExpressionLane>& lanes() const noexcept;
    ExpressionLane* findLane (const ExpressionLaneId& laneId) noexcept;
    const ExpressionLane* findLane (const ExpressionLaneId& laneId) const noexcept;
    void addLane (ExpressionLane lane);
    ExpressionLane removeLane (const ExpressionLaneId& laneId);

private:
    std::vector<ExpressionLane> lanes_;
};
}
