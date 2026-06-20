#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <vector>

namespace tsq::engine::devices
{
struct NativePhaserParameters
{
    float amount = 1.0f;
    float speedHz = 0.5f;
};

class NativePhaserProcessor final
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    void setParameters (const NativePhaserParameters& parameters) noexcept;
    void processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    struct AllPassStage
    {
        double x1 = 0.0;
        double y1 = 0.0;

        void reset() noexcept;
        float processSample (float input, double coefficient) noexcept;
    };

    double sampleRate_ = 44100.0;
    double lfoPhase_ = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> speedHz_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amount_;
    std::array<std::array<AllPassStage, 4>, 2> stages_ {};

    float processSample (float input, int channel, double coefficient) noexcept;
    double renderLfo() const noexcept;
    double allPassCoefficientForCutoff (double cutoffHz) const noexcept;
};

struct NativeReverbParameters
{
    float mix = 0.25f;
    float decay = 0.65f;
};

class NativeReverbProcessor final
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setParameters (const NativeReverbParameters& parameters) noexcept;
    void processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    struct CombDelay
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        float damped = 0.0f;

        void prepare (int delaySamples);
        void reset() noexcept;
        float process (float input, float feedback, float damping) noexcept;
    };

    struct AllPassDelay
    {
        std::vector<float> buffer;
        int writeIndex = 0;

        void prepare (int delaySamples);
        void reset() noexcept;
        float process (float input, float feedback) noexcept;
    };

    struct ChannelTank
    {
        std::array<CombDelay, 4> combs;
        std::array<AllPassDelay, 2> allPasses;
        float preFilter = 0.0f;

        void prepare (double sampleRate, bool rightChannel);
        void reset() noexcept;
        float process (float input, float feedback, float damping) noexcept;
    };

    double sampleRate_ = 44100.0;
    ChannelTank left_;
    ChannelTank right_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> decay_;
};

struct NativeTapeParameters
{
    float drive = 0.25f;
    float instability = 0.0f;
    float wear = 0.0f;
    float noise = 0.0f;
    float mix = 0.45f;
};

class NativeTapeProcessor final
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    void setParameters (const NativeTapeParameters& parameters) noexcept;
    void processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    static constexpr int delaySize = 4096;

    double sampleRate_ = 44100.0;

    juce::SmoothedValue<float> drive_;
    juce::SmoothedValue<float> instability_;
    juce::SmoothedValue<float> wear_;
    juce::SmoothedValue<float> noise_;
    juce::SmoothedValue<float> mix_;

    float dcInL_ = 0.0f;
    float dcOutL_ = 0.0f;
    float dcInR_ = 0.0f;
    float dcOutR_ = 0.0f;
    float dcCoeff_ = 0.0f;

    std::array<float, delaySize> delayBufL_ {};
    std::array<float, delaySize> delayBufR_ {};
    int writePos_ = 0;
    double wowPhase_ = 0.0;
    double flutterPhase1_ = 0.0;
    double flutterPhase2_ = 0.0;

    std::uint32_t wowFlutterRng_ = 0x1a2b3c4du;
    float wowSHSmoothed_ = 0.5f;
    float wowSHTarget_ = 0.5f;
    float wowSHCoeff_ = 0.0f;
    double wowSHTimer_ = 0.0;
    float flutterSHSmoothed_ = 0.5f;
    float flutterSHTarget_ = 0.5f;
    float flutterSHCoeff_ = 0.0f;
    double flutterSHTimer_ = 0.0;

    float wearLpfL_ = 0.0f;
    float wearLpfR_ = 0.0f;

    std::uint32_t dropoutRng_ = 0xd3adb33f;
    float dropoutBrownL_ = 0.0f;
    float dropoutBrownR_ = 0.0f;
    float dropoutGainL_ = 1.0f;
    float dropoutGainR_ = 1.0f;

    std::uint32_t hissRng_ = 0x87654321;
    float hissLpfL_ = 0.0f;
    float hissLpfR_ = 0.0f;
    float hissBandL_ = 0.0f;
    float hissBandR_ = 0.0f;
    float hissLpfCoeff_ = 0.0f;
    float hissBandCoeff_ = 0.0f;

    double driftPhase_ = 0.0;
    float driftValue_ = 0.9f;
    float driftTarget_ = 0.9f;
    float driftSmoothCoeff_ = 0.0f;

    double humPhase_ = 0.0;

    float envL_ = 0.0f;
    float envR_ = 0.0f;
    float envAttackCoeff_ = 0.0f;
    float envReleaseCoeff_ = 0.0f;

    static float xorshift32 (std::uint32_t& state) noexcept;
    static float hermite (float x0, float x1, float x2, float x3, float t) noexcept;
    float readDelay (const std::array<float, delaySize>& buffer, int writePos, float delaySamples) const noexcept;
};
}
