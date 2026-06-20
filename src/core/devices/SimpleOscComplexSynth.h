#pragma once

#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Expression.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tsq::core::devices
{
struct SimpleOscComplexPatch
{
    double pitchSemitones = 0.0;
    double ampLevel = 0.75;
    double pmAmount = 0.0;
    int modRatioIndex = 2;
    double wavefolderAmount = 0.0;
    int wavefolderStages = 2;
    double attackSeconds = 0.0;
    double decaySeconds = 0.18;
    double sustainLevel = 0.75;
    double releaseSeconds = 0.0;
};

using SimpleOscComplexNoteId = std::uint64_t;

class SimpleOscComplexSynth final
{
public:
    explicit SimpleOscComplexSynth (std::size_t maxVoices = 16);

    void prepare (double sampleRate);
    void setPatch (SimpleOscComplexPatch patch);
    const SimpleOscComplexPatch& patch() const noexcept;

    void noteOn (int midiNote, float velocity);
    void noteOn (SimpleOscComplexNoteId noteId, int midiNote, float velocity);
    void noteOff (int midiNote);
    void noteOff (SimpleOscComplexNoteId noteId);
    bool startLegatoSlur (SimpleOscComplexNoteId sourceNoteId,
                          SimpleOscComplexNoteId destinationNoteId,
                          int destinationMidiNote,
                          float destinationVelocity,
                          double slurTimeSeconds,
                          sequencing::ExpressionCurveShape curveShape,
                          bool legatoNoRetrigger);
    bool setVoicePitchOffset (SimpleOscComplexNoteId noteId, double offsetSemitones) noexcept;
    void allNotesOff();
    void reset();

    void render (float* left, float* right, int numSamples);
    std::size_t activeVoiceCount() const noexcept;
    std::size_t activeVoiceCountForNoteId (SimpleOscComplexNoteId noteId) const noexcept;
    double debugEnvelopeLevelForNoteId (SimpleOscComplexNoteId noteId) const noexcept;
    double debugCurrentPitchSemitonesForNoteId (SimpleOscComplexNoteId noteId) const noexcept;

private:
    enum class EnvelopeStage
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    struct Voice
    {
        SimpleOscComplexNoteId noteId = 0;
        int midiNote = -1;
        float velocity = 0.0f;
        double carrierPhase = 0.0;
        double modulatorPhase = 0.0;
        double envelopeLevel = 0.0;
        double releaseStartLevel = 0.0;
        double pitchOffsetSemitones = 0.0;
        double slurStartPitchSemitones = 0.0;
        double currentPitchSemitones = 0.0;
        int slurSampleIndex = 0;
        int slurTotalSamples = 0;
        sequencing::ExpressionCurveShape slurCurveShape = sequencing::ExpressionCurveShape::linear;
        int stageSampleIndex = 0;
        std::size_t age = 0;
        EnvelopeStage stage = EnvelopeStage::idle;

        bool active() const noexcept;
        bool slurring() const noexcept;
    };

    Voice& allocateVoice();
    Voice* findVoiceByNoteId (SimpleOscComplexNoteId noteId) noexcept;
    const Voice* findVoiceByNoteId (SimpleOscComplexNoteId noteId) const noexcept;
    double voicePitchSemitones (Voice& voice) noexcept;
    double renderVoice (Voice& voice);
    double envelopeForVoice (Voice& voice);

    std::vector<Voice> voices_;
    SimpleOscComplexPatch patch_;
    double sampleRate_ = 44100.0;
    std::size_t voiceAgeCounter_ = 0;
};

SimpleOscComplexPatch simpleOscComplexPatchFromState (const sequencing::FirstPartyDeviceState& state);
double simpleOscComplexActualParameterValue (const char* parameterId, double normalizedValue);
double simpleOscComplexModRatioForIndex (int index) noexcept;
}
