#include "engine/devices/NativeEffectProcessors.h"

#include <algorithm>
#include <cmath>

namespace tsq::engine::devices
{
namespace
{
double safeSampleRate (double sampleRate) noexcept
{
    return sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;
}

int scaledDelaySamples (double sampleRate, int samplesAt48k) noexcept
{
    return std::max (1, static_cast<int> (std::round (static_cast<double> (samplesAt48k) * sampleRate / 48000.0)));
}
}

void NativePhaserProcessor::AllPassStage::reset() noexcept
{
    x1 = 0.0;
    y1 = 0.0;
}

float NativePhaserProcessor::AllPassStage::processSample (float input, double coefficient) noexcept
{
    const auto x = static_cast<double> (input);
    const auto y = coefficient * x + x1 - coefficient * y1;
    x1 = x;
    y1 = y;
    return static_cast<float> (y);
}

void NativePhaserProcessor::prepare (double sampleRate, int) noexcept
{
    sampleRate_ = safeSampleRate (sampleRate);
    speedHz_.reset (sampleRate_, 0.050);
    speedHz_.setCurrentAndTargetValue (0.5f);
    amount_.reset (sampleRate_, 0.010);
    amount_.setCurrentAndTargetValue (1.0f);
    reset();
}

void NativePhaserProcessor::reset() noexcept
{
    lfoPhase_ = 0.0;
    for (auto& channelStages : stages_)
        for (auto& stage : channelStages)
            stage.reset();
}

void NativePhaserProcessor::setParameters (const NativePhaserParameters& parameters) noexcept
{
    amount_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.amount));
    speedHz_.setTargetValue (juce::jlimit (0.1f, 10.0f, parameters.speedHz));
}

double NativePhaserProcessor::renderLfo() const noexcept
{
    const auto phase = lfoPhase_ - std::floor (lfoPhase_);
    const auto triangle = 1.0 - std::abs (2.0 * phase - 1.0);
    const auto smoothedTriangle = triangle * triangle * (3.0 - 2.0 * triangle);
    const auto sineLike = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * phase);
    return juce::jlimit (0.0, 1.0, smoothedTriangle * 0.70 + sineLike * 0.30);
}

double NativePhaserProcessor::allPassCoefficientForCutoff (double cutoffHz) const noexcept
{
    const auto safeCutoff = juce::jlimit (20.0, sampleRate_ * 0.45, cutoffHz);
    const auto t = std::tan (juce::MathConstants<double>::pi * safeCutoff / sampleRate_);
    return (t - 1.0) / (t + 1.0);
}

float NativePhaserProcessor::processSample (float input, int channel, double coefficient) noexcept
{
    auto wet = input;
    for (auto& stage : stages_[static_cast<std::size_t> (std::clamp (channel, 0, 1))])
        wet = stage.processSample (wet, coefficient);

    return 0.5f * input + 0.5f * wet;
}

void NativePhaserProcessor::processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const auto channelCount = std::min (2, buffer.getNumChannels());
    if (channelCount <= 0 || numSamples <= 0)
        return;

    startSample = std::clamp (startSample, 0, buffer.getNumSamples());
    numSamples = std::clamp (numSamples, 0, buffer.getNumSamples() - startSample);
    if (numSamples <= 0)
        return;

    std::array<float*, 2> channels {};
    for (auto channel = 0; channel < channelCount; ++channel)
        channels[static_cast<std::size_t> (channel)] = buffer.getWritePointer (channel, startSample);

    for (auto sample = 0; sample < numSamples; ++sample)
    {
        const auto lfo = renderLfo();
        const auto mappedLfo = std::pow (lfo, 2.5);
        const auto cutoffHz = 200.0 + mappedLfo * (3000.0 - 200.0);
        const auto coefficient = allPassCoefficientForCutoff (cutoffHz);
        const auto mix = amount_.getNextValue();

        for (auto channel = 0; channel < channelCount; ++channel)
        {
            auto* samples = channels[static_cast<std::size_t> (channel)];
            const auto dry = samples[sample];
            const auto phased = processSample (dry, channel, coefficient);
            samples[sample] = dry + mix * (phased - dry);
        }

        lfoPhase_ += static_cast<double> (speedHz_.getNextValue()) / sampleRate_;
        lfoPhase_ -= std::floor (lfoPhase_);
    }
}

void NativeReverbProcessor::CombDelay::prepare (int delaySamples)
{
    buffer.assign (static_cast<std::size_t> (std::max (1, delaySamples)), 0.0f);
    writeIndex = 0;
    damped = 0.0f;
}

void NativeReverbProcessor::CombDelay::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    damped = 0.0f;
}

float NativeReverbProcessor::CombDelay::process (float input, float feedback, float damping) noexcept
{
    if (buffer.empty())
        return input;

    const auto delayed = buffer[static_cast<std::size_t> (writeIndex)];
    damped += damping * (delayed - damped);
    buffer[static_cast<std::size_t> (writeIndex)] = input + damped * feedback;
    if (++writeIndex >= static_cast<int> (buffer.size()))
        writeIndex = 0;

    return delayed;
}

void NativeReverbProcessor::AllPassDelay::prepare (int delaySamples)
{
    buffer.assign (static_cast<std::size_t> (std::max (1, delaySamples)), 0.0f);
    writeIndex = 0;
}

void NativeReverbProcessor::AllPassDelay::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

float NativeReverbProcessor::AllPassDelay::process (float input, float feedback) noexcept
{
    if (buffer.empty())
        return input;

    const auto delayed = buffer[static_cast<std::size_t> (writeIndex)];
    const auto output = -input + delayed;
    buffer[static_cast<std::size_t> (writeIndex)] = input + delayed * feedback;
    if (++writeIndex >= static_cast<int> (buffer.size()))
        writeIndex = 0;

    return output;
}

void NativeReverbProcessor::ChannelTank::prepare (double sampleRate, bool rightChannel)
{
    static constexpr std::array<int, 4> leftCombDelays { 1557, 1617, 1491, 1422 };
    static constexpr std::array<int, 4> rightCombDelays { 1589, 1663, 1511, 1447 };
    static constexpr std::array<int, 2> leftAllPassDelays { 225, 556 };
    static constexpr std::array<int, 2> rightAllPassDelays { 248, 579 };

    const auto& combDelays = rightChannel ? rightCombDelays : leftCombDelays;
    const auto& allPassDelays = rightChannel ? rightAllPassDelays : leftAllPassDelays;

    for (std::size_t index = 0; index < combs.size(); ++index)
        combs[index].prepare (scaledDelaySamples (sampleRate, combDelays[index]));

    for (std::size_t index = 0; index < allPasses.size(); ++index)
        allPasses[index].prepare (scaledDelaySamples (sampleRate, allPassDelays[index]));

    reset();
}

void NativeReverbProcessor::ChannelTank::reset() noexcept
{
    for (auto& comb : combs)
        comb.reset();
    for (auto& allPass : allPasses)
        allPass.reset();
    preFilter = 0.0f;
}

float NativeReverbProcessor::ChannelTank::process (float input, float feedback, float damping) noexcept
{
    preFilter += 0.18f * (input - preFilter);
    const auto tankInput = preFilter;

    auto wet = 0.0f;
    for (auto& comb : combs)
        wet += comb.process (tankInput, feedback, damping);
    wet *= 0.25f;

    for (auto& allPass : allPasses)
        wet = allPass.process (wet, 0.58f);

    return std::clamp (wet, -2.0f, 2.0f);
}

void NativeReverbProcessor::prepare (double sampleRate, int)
{
    sampleRate_ = safeSampleRate (sampleRate);
    left_.prepare (sampleRate_, false);
    right_.prepare (sampleRate_, true);
    mix_.reset (sampleRate_, 0.030);
    mix_.setCurrentAndTargetValue (0.0f);
    decay_.reset (sampleRate_, 0.050);
    decay_.setCurrentAndTargetValue (0.65f);
}

void NativeReverbProcessor::reset() noexcept
{
    left_.reset();
    right_.reset();
}

void NativeReverbProcessor::setParameters (const NativeReverbParameters& parameters) noexcept
{
    mix_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.mix));
    decay_.setTargetValue (juce::jlimit (0.05f, 1.0f, parameters.decay));
}

void NativeReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const auto channelCount = std::min (2, buffer.getNumChannels());
    if (channelCount <= 0 || numSamples <= 0)
        return;

    startSample = std::clamp (startSample, 0, buffer.getNumSamples());
    numSamples = std::clamp (numSamples, 0, buffer.getNumSamples() - startSample);
    if (numSamples <= 0)
        return;

    if (mix_.getTargetValue() <= 0.001f && mix_.getCurrentValue() <= 0.001f)
    {
        mix_.skip (numSamples);
        decay_.skip (numSamples);
        return;
    }

    auto* left = buffer.getWritePointer (0, startSample);
    auto* right = channelCount > 1 ? buffer.getWritePointer (1, startSample) : left;

    for (auto sample = 0; sample < numSamples; ++sample)
    {
        const auto inL = left[sample];
        const auto inR = channelCount > 1 ? right[sample] : inL;
        const auto decay = decay_.getNextValue();
        const auto feedback = 0.54f + decay * 0.38f;
        const auto damping = 0.36f - decay * 0.24f;
        const auto monoInput = (inL + inR) * 0.30f;
        const auto wetL = left_.process (monoInput + inR * 0.08f, feedback, damping);
        const auto wetR = right_.process (monoInput + inL * 0.08f, feedback, damping);
        const auto wetMix = mix_.getNextValue();

        left[sample] = inL + wetMix * (wetL - inL);
        if (channelCount > 1)
            right[sample] = inR + wetMix * (wetR - inR);
    }
}

float NativeTapeProcessor::xorshift32 (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (static_cast<std::int32_t> (state)) * (1.0f / static_cast<float> (0x80000000u));
}

float NativeTapeProcessor::hermite (float x0, float x1, float x2, float x3, float t) noexcept
{
    const auto c1 = 0.5f * (x2 - x0);
    const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

float NativeTapeProcessor::readDelay (const std::array<float, delaySize>& buffer,
                                      int writePos,
                                      float delaySamples) const noexcept
{
    const auto readIndex = static_cast<float> (writePos) - delaySamples;
    const auto readInteger = static_cast<int> (std::floor (readIndex));
    const auto t = readIndex - static_cast<float> (readInteger);
    constexpr auto mask = delaySize - 1;
    const auto x0 = buffer[static_cast<std::size_t> ((readInteger - 1) & mask)];
    const auto x1 = buffer[static_cast<std::size_t> (readInteger & mask)];
    const auto x2 = buffer[static_cast<std::size_t> ((readInteger + 1) & mask)];
    const auto x3 = buffer[static_cast<std::size_t> ((readInteger + 2) & mask)];
    return hermite (x0, x1, x2, x3, t);
}

void NativeTapeProcessor::prepare (double sampleRate, int) noexcept
{
    sampleRate_ = safeSampleRate (sampleRate);
    const auto twoPi = juce::MathConstants<double>::twoPi;

    drive_.reset (sampleRate_, 0.050);
    instability_.reset (sampleRate_, 0.050);
    wear_.reset (sampleRate_, 0.050);
    noise_.reset (sampleRate_, 0.050);
    mix_.reset (sampleRate_, 0.050);

    drive_.setCurrentAndTargetValue (0.25f);
    instability_.setCurrentAndTargetValue (0.0f);
    wear_.setCurrentAndTargetValue (0.0f);
    noise_.setCurrentAndTargetValue (0.0f);
    mix_.setCurrentAndTargetValue (0.45f);

    dcCoeff_ = 1.0f - static_cast<float> (twoPi * 10.0 / sampleRate_);
    hissLpfCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi * 8000.0 / sampleRate_));
    hissBandCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi * 400.0 / sampleRate_));
    wowSHCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi * (1.0 / 1.5) / sampleRate_));
    flutterSHCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi * (1.0 / 0.25) / sampleRate_));
    driftSmoothCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi * (1.0 / 3.0) / sampleRate_));
    envAttackCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi / (0.005 * sampleRate_)));
    envReleaseCoeff_ = 1.0f - std::exp (-static_cast<float> (twoPi / (0.300 * sampleRate_)));

    reset();
}

void NativeTapeProcessor::reset() noexcept
{
    delayBufL_.fill (0.0f);
    delayBufR_.fill (0.0f);
    writePos_ = 0;
    wowPhase_ = 0.0;
    flutterPhase1_ = 0.0;
    flutterPhase2_ = 0.0;
    dcInL_ = dcOutL_ = dcInR_ = dcOutR_ = 0.0f;
    wearLpfL_ = wearLpfR_ = 0.0f;
    dropoutBrownL_ = dropoutBrownR_ = 0.0f;
    dropoutGainL_ = dropoutGainR_ = 1.0f;
    hissLpfL_ = hissLpfR_ = 0.0f;
    hissBandL_ = hissBandR_ = 0.0f;
    driftPhase_ = 0.0;
    driftValue_ = 0.9f;
    driftTarget_ = 0.9f;
    humPhase_ = 0.0;
    envL_ = envR_ = 0.0f;
    wowSHSmoothed_ = wowSHTarget_ = 0.5f;
    flutterSHSmoothed_ = flutterSHTarget_ = 0.5f;
    wowSHTimer_ = 0.0;
    flutterSHTimer_ = 0.0;
}

void NativeTapeProcessor::setParameters (const NativeTapeParameters& parameters) noexcept
{
    drive_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.drive));
    instability_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.instability));
    wear_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.wear));
    noise_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.noise));
    mix_.setTargetValue (juce::jlimit (0.0f, 1.0f, parameters.mix));
}

void NativeTapeProcessor::processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const auto channelCount = std::min (2, buffer.getNumChannels());
    if (channelCount <= 0 || numSamples <= 0)
        return;

    startSample = std::clamp (startSample, 0, buffer.getNumSamples());
    numSamples = std::clamp (numSamples, 0, buffer.getNumSamples() - startSample);
    if (numSamples <= 0)
        return;

    if (mix_.getTargetValue() <= 0.001f && mix_.getCurrentValue() <= 0.001f)
    {
        drive_.skip (numSamples);
        instability_.skip (numSamples);
        wear_.skip (numSamples);
        noise_.skip (numSamples);
        mix_.skip (numSamples);
        return;
    }

    auto* channelL = buffer.getWritePointer (0, startSample);
    auto* channelR = channelCount > 1 ? buffer.getWritePointer (1, startSample) : channelL;

    const auto twoPi = juce::MathConstants<double>::twoPi;
    const auto maxWowSamples = 0.030f * static_cast<float> (sampleRate_);
    const auto maxFlutterSamples = 0.0015f * static_cast<float> (sampleRate_);
    constexpr auto delayMask = delaySize - 1;
    const auto wearTarget = wear_.getTargetValue();
    const auto wearCutoffHz = 20000.0f - wearTarget * 17000.0f;
    const auto wearLpfCoeff = 1.0f - std::exp (-static_cast<float> (twoPi * static_cast<double> (wearCutoffHz) / sampleRate_));

    for (auto sample = 0; sample < numSamples; ++sample)
    {
        const auto drive = drive_.getNextValue();
        const auto instability = instability_.getNextValue();
        const auto wear = wear_.getNextValue();
        const auto noise = noise_.getNextValue();
        const auto mix = mix_.getNextValue();

        const auto inL = channelL[sample];
        const auto inR = channelCount > 1 ? channelR[sample] : inL;

        const auto previousDcInL = dcInL_;
        const auto previousDcInR = dcInR_;
        dcInL_ = inL;
        dcInR_ = inR;
        dcOutL_ = inL - previousDcInL + dcCoeff_ * dcOutL_;
        dcOutR_ = inR - previousDcInR + dcCoeff_ * dcOutR_;
        auto wetL = dcOutL_;
        auto wetR = dcOutR_;

        if (drive > 0.001f)
        {
            const auto gain = 1.0f + drive * 3.0f;
            const auto asymmetry = 0.04f * drive;
            const auto tanhAsymmetry = std::tanh (asymmetry);
            const auto normalization = 1.0f / (gain * (1.0f - tanhAsymmetry * tanhAsymmetry));
            wetL = (std::tanh (wetL * gain + asymmetry) - tanhAsymmetry) * normalization;
            wetR = (std::tanh (wetR * gain + asymmetry) - tanhAsymmetry) * normalization;
        }

        wowSHTimer_ -= 1.0;
        if (wowSHTimer_ <= 0.0)
        {
            const auto u = xorshift32 (wowFlutterRng_) * 0.5f + 0.5f;
            const auto exponent = 4.0f - instability * 3.7f;
            wowSHTarget_ = std::pow (u, exponent);
            const auto t = xorshift32 (wowFlutterRng_) * 0.5f + 0.5f;
            wowSHTimer_ = sampleRate_ * (1.5 + static_cast<double> (t) * 4.5);
        }
        wowSHSmoothed_ += wowSHCoeff_ * (wowSHTarget_ - wowSHSmoothed_);

        flutterSHTimer_ -= 1.0;
        if (flutterSHTimer_ <= 0.0)
        {
            const auto u = xorshift32 (wowFlutterRng_) * 0.5f + 0.5f;
            const auto exponent = 4.0f - instability * 3.7f;
            flutterSHTarget_ = std::pow (u, exponent);
            const auto t = xorshift32 (wowFlutterRng_) * 0.5f + 0.5f;
            flutterSHTimer_ = sampleRate_ * (0.2 + static_cast<double> (t) * 1.3);
        }
        flutterSHSmoothed_ += flutterSHCoeff_ * (flutterSHTarget_ - flutterSHSmoothed_);

        const auto wowHz = 0.33 * (0.70 + 0.60 * static_cast<double> (wowSHSmoothed_));
        const auto flutter1Hz = 6.1 * (0.75 + 0.50 * static_cast<double> (flutterSHSmoothed_));
        const auto flutter2Hz = 13.7 * (0.75 + 0.50 * static_cast<double> (flutterSHSmoothed_));

        const auto wowMod = static_cast<float> (std::sin (twoPi * wowPhase_));
        const auto flutter1Mod = static_cast<float> (std::sin (twoPi * flutterPhase1_));
        const auto flutter2Mod = static_cast<float> (std::sin (twoPi * flutterPhase2_));

        wowPhase_ += wowHz / sampleRate_;
        if (wowPhase_ >= 1.0)
            wowPhase_ -= 1.0;
        flutterPhase1_ += flutter1Hz / sampleRate_;
        if (flutterPhase1_ >= 1.0)
            flutterPhase1_ -= 1.0;
        flutterPhase2_ += flutter2Hz / sampleRate_;
        if (flutterPhase2_ >= 1.0)
            flutterPhase2_ -= 1.0;

        delayBufL_[static_cast<std::size_t> (writePos_)] = wetL;
        delayBufR_[static_cast<std::size_t> (writePos_)] = wetR;

        if (instability > 0.001f)
        {
            const auto wowDepth = maxWowSamples * wowSHSmoothed_;
            const auto flutterDepth = maxFlutterSamples * flutterSHSmoothed_;
            const auto delayL = juce::jlimit (2.0f,
                                              static_cast<float> (delaySize / 2),
                                              instability * (wowDepth * (1.0f + wowMod) * 0.5f
                                                           + flutterDepth * (flutter1Mod + flutter2Mod) * 0.5f)
                                                  + 2.0f);
            const auto delayR = juce::jlimit (2.0f,
                                              static_cast<float> (delaySize / 2),
                                              instability * (wowDepth * (1.0f + wowMod * 0.97f) * 0.5f
                                                           + flutterDepth * (flutter1Mod * 1.03f + flutter2Mod * 0.97f) * 0.5f)
                                                  + 2.0f);
            wetL = readDelay (delayBufL_, writePos_, delayL);
            wetR = readDelay (delayBufR_, writePos_, delayR);
        }
        else
        {
            wetL = delayBufL_[static_cast<std::size_t> (writePos_)];
            wetR = delayBufR_[static_cast<std::size_t> (writePos_)];
        }

        writePos_ = (writePos_ + 1) & delayMask;

        if (wear > 0.001f)
        {
            wearLpfL_ += wearLpfCoeff * (wetL - wearLpfL_);
            wearLpfR_ += wearLpfCoeff * (wetR - wearLpfR_);
            wetL = wearLpfL_;
            wetR = wearLpfR_;

            dropoutBrownL_ = 0.998f * dropoutBrownL_ + 0.002f * xorshift32 (dropoutRng_);
            dropoutBrownR_ = 0.998f * dropoutBrownR_ + 0.002f * xorshift32 (dropoutRng_);
            const auto threshold = 1.0f - wear * 0.07f;
            const auto targetGainL = std::abs (dropoutBrownL_) > threshold ? 1.0f - 0.25f * wear : 1.0f;
            const auto targetGainR = std::abs (dropoutBrownR_) > threshold ? 1.0f - 0.25f * wear : 1.0f;
            dropoutGainL_ += 0.005f * (targetGainL - dropoutGainL_);
            dropoutGainR_ += 0.005f * (targetGainR - dropoutGainR_);
            wetL *= dropoutGainL_;
            wetR *= dropoutGainR_;
        }

        if (noise > 0.001f)
        {
            const auto absL = std::abs (wetL);
            const auto absR = std::abs (wetR);
            const auto diffL = absL - envL_;
            const auto diffR = absR - envR_;
            envL_ += (diffL > 0.0f ? envAttackCoeff_ : envReleaseCoeff_) * diffL;
            envR_ += (diffR > 0.0f ? envAttackCoeff_ : envReleaseCoeff_) * diffR;

            driftPhase_ += 0.2 / sampleRate_;
            if (driftPhase_ >= 1.0)
            {
                driftPhase_ -= 1.0;
                driftTarget_ = 0.65f + 0.35f * (xorshift32 (hissRng_) * 0.5f + 0.5f);
            }
            driftValue_ += driftSmoothCoeff_ * (driftTarget_ - driftValue_);

            const auto whiteL = xorshift32 (hissRng_);
            const auto whiteR = xorshift32 (hissRng_);
            hissLpfL_ += hissLpfCoeff_ * (whiteL - hissLpfL_);
            hissLpfR_ += hissLpfCoeff_ * (whiteR - hissLpfR_);
            hissBandL_ += hissBandCoeff_ * (hissLpfL_ - hissBandL_);
            hissBandR_ += hissBandCoeff_ * (hissLpfR_ - hissBandR_);
            const auto hissL = hissLpfL_ - hissBandL_;
            const auto hissR = hissLpfR_ - hissBandR_;

            humPhase_ += 60.0 / sampleRate_;
            if (humPhase_ >= 1.0)
                humPhase_ -= 1.0;
            const auto hum = noise * 0.004f * static_cast<float> (std::sin (twoPi * humPhase_));
            const auto noiseAmount = noise * 0.025f;
            wetL += noiseAmount * driftValue_ * (hissL + hissL * envL_ * 0.5f) + hum;
            wetR += noiseAmount * driftValue_ * (hissR + hissR * envR_ * 0.5f) + hum;
        }

        const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
        const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        channelL[sample] = dryGain * inL + wetGain * wetL;
        if (channelCount > 1)
            channelR[sample] = dryGain * inR + wetGain * wetR;
    }
}
}
