#pragma once
#include "Effects/Multiband/Saturators.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace tonegrid::multiband
{
enum class CompRatio : int
{
    R1_1 = 0,
    R2_1 = 1,
    R3_1 = 2,
    R4_1 = 3,
    R6_1 = 4,
    R10_1_Opto = 5,
    R20_1 = 6,
    Nuke = 7
};

enum class DetectorMode : int
{
    Off = 0,
    HighPass = 1,
    BandEmphasis = 2,
    HpAndEmphasis = 3
};

struct RatioCurveSpec
{
    float ratio = 1.0f;
    float kneeWidthDb = 6.0f;
    float thresholdOffsetDb = 0.0f;
};

constexpr RatioCurveSpec ratioSpec(CompRatio mode) noexcept
{
    switch (mode)
    {
        case CompRatio::R1_1:       return {  1.0f,  0.0f,  0.0f };
        case CompRatio::R2_1:       return {  2.0f, 12.0f, -2.0f };
        case CompRatio::R3_1:       return {  3.0f,  9.0f, -1.5f };
        case CompRatio::R4_1:       return {  4.0f,  7.0f, -1.0f };
        case CompRatio::R6_1:       return {  6.0f,  5.0f,  0.0f };
        case CompRatio::R10_1_Opto: return { 10.0f,  8.0f, -2.5f };
        case CompRatio::R20_1:      return { 20.0f,  2.5f,  1.0f };
        case CompRatio::Nuke:
        default:                    return { 60.0f,  1.5f, -4.0f };
    }
}

/**
 * Pure static gain reduction computer (in positive dB of attenuation) for a given
 * input level in dBFS, threshold in dBFS, and CompRatio curve.
 * Exposed as a static method for deterministic curve unit tests.
 */
inline float computeGainReductionDb(float levelDb, float baseThresholdDb, CompRatio mode) noexcept
{
    if (mode == CompRatio::R1_1)
        return 0.0f;

    const auto spec = ratioSpec(mode);
    const float thr = baseThresholdDb + spec.thresholdOffsetDb;
    const float slope = 1.0f - 1.0f / spec.ratio;
    const float over = levelDb - thr;
    const float halfKnee = spec.kneeWidthDb * 0.5f;

    if (halfKnee <= 0.001f)
        return over > 0.0f ? over * slope : 0.0f;

    if (over <= -halfKnee)
        return 0.0f;
    if (over >= halfKnee)
        return over * slope;

    const float x = over + halfKnee;
    return slope * (x * x) / (2.0f * spec.kneeWidthDb);
}

/**
 * Single-band TRI-COMP compressor cell with:
 * - Sidechain HPF (90 Hz) and Band Emphasis (mid/high peak detector tilt)
 * - 8 ratio modes (1:1 through NUKE, including 10:1 Opto)
 * - Program-dependent dual-stage release ballistics
 * - Integrated Dist 2 / Dist 3 harmonic generation stage
 */
class CompBandCell
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        scHpf1.prepare(sampleRate, 90.0);
        scHpf2.prepare(sampleRate, 90.0);
        scEmphasisHpf.prepare(sampleRate, 1800.0);
        scEmphasisLpf.prepare(sampleRate, 6500.0);
        harmonics.prepare(sampleRate);
        reset();
    }

    void reset() noexcept
    {
        scHpf1.reset();
        scHpf2.reset();
        scEmphasisHpf.reset();
        scEmphasisLpf.reset();
        harmonics.reset();
        fastGrDb = 0.0f;
        slowGrDb = 0.0f;
        optoMemory = 0.0f;
        lastGrDb = 0.0f;
        lastHarmonic = 0.0f;
    }

    // Step 1 of 2: detect target & smoothed gain reduction for this sample.
    // Separating detection from application allows cross-band linking in TRI-COMP.
    float detectGainReductionDb(float x,
                                float baseThresholdDb,
                                CompRatio ratio,
                                float attackMs,
                                float releaseMs,
                                DetectorMode detector) noexcept
    {
        float sc = x;
        if (detector == DetectorMode::HighPass || detector == DetectorMode::HpAndEmphasis)
            sc = scHpf2.processHighpass(scHpf1.processHighpass(sc));
        if (detector == DetectorMode::BandEmphasis || detector == DetectorMode::HpAndEmphasis)
        {
            const float midHigh = scEmphasisLpf.processLowpass(scEmphasisHpf.processHighpass(sc));
            sc = sc + 2.2f * midHigh; // +7 dB emphasis in the clank/presence region
        }

        const float absSc = std::abs(sc);
        const float levelDb = juce::Decibels::gainToDecibels(absSc, -100.0f);
        const float targetGrDb = computeGainReductionDb(levelDb, baseThresholdDb, ratio);

        const float clampedAttackMs = juce::jlimit(0.05f, 50.0f, attackMs);
        const float clampedReleaseMs = juce::jlimit(30.0f, 3500.0f, releaseMs);

        // Opto mode (10:1) uses slower optical cell attack and program-dependent memory release.
        const float effectiveAttackMs = (ratio == CompRatio::R10_1_Opto)
                                            ? juce::jmax(2.5f, clampedAttackMs * 1.6f)
                                            : (ratio == CompRatio::Nuke ? juce::jmin(clampedAttackMs, 1.5f) : clampedAttackMs);

        const float attackCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * effectiveAttackMs * 0.001));
        const float fastRelCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * clampedReleaseMs * 0.001));
        const float slowRelSeconds = (ratio == CompRatio::R10_1_Opto)
                                         ? juce::jmin(8.0f, clampedReleaseMs * 0.0045f * (1.0f + optoMemory * 2.5f))
                                         : clampedReleaseMs * 0.0025f;
        const float slowRelCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * juce::jmax(0.08f, slowRelSeconds)));

        if (targetGrDb > fastGrDb)
        {
            fastGrDb = attackCoeff * fastGrDb + (1.0f - attackCoeff) * targetGrDb;
            slowGrDb = std::sqrt(attackCoeff) * slowGrDb + (1.0f - std::sqrt(attackCoeff)) * targetGrDb;
            optoMemory = juce::jmin(1.0f, optoMemory + static_cast<float>(1.0 / (sampleRate * 0.25)));
        }
        else
        {
            fastGrDb = fastRelCoeff * fastGrDb + (1.0f - fastRelCoeff) * targetGrDb;
            slowGrDb = slowRelCoeff * slowGrDb + (1.0f - slowRelCoeff) * targetGrDb;
            optoMemory *= slowRelCoeff;
        }

        // Dual-release blend: sustained compression smoothly leans on the slower time constant.
        const float pdWeight = (ratio == CompRatio::R10_1_Opto) ? 0.55f : 0.25f * optoMemory;
        lastGrDb = (1.0f - pdWeight) * fastGrDb + pdWeight * slowGrDb;
        return lastGrDb;
    }

    // Step 2 of 2: apply gain reduction (possibly linked across bands) + Dist 2/3 harmonics + band makeup.
    float applyGainAndHarmonics(float x,
                                float effectiveGrDb,
                                CompAudioMode audioMode,
                                bool audioHighPass,
                                float bandGainLinear) noexcept
    {
        lastGrDb = effectiveGrDb;
        const float grLinear = juce::Decibels::decibelsToGain(-effectiveGrDb);
        const float compressed = x * grLinear;
        const float colored = harmonics.process(compressed, audioMode, effectiveGrDb, audioHighPass, &lastHarmonic);
        return colored * bandGainLinear;
    }

    float currentGrDb() const noexcept { return lastGrDb; }
    float currentHarmonicLevel() const noexcept { return lastHarmonic; }

private:
    OnePoleFilter scHpf1, scHpf2, scEmphasisHpf, scEmphasisLpf;
    CompHarmonics harmonics;
    double sampleRate = 48000.0;
    float fastGrDb = 0.0f;
    float slowGrDb = 0.0f;
    float optoMemory = 0.0f;
    float lastGrDb = 0.0f;
    float lastHarmonic = 0.0f;
};

/**
 * Single-knob Low-Band Bus Compressor for TRI-DRIVE.
 * Keeps the bass fundamental tight, punchy, and consistent while Mid and High
 * bands carry the saturation.
 */
class LowBandBusComp
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        attackCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * 0.012));  // 12 ms punchy attack
        releaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * 0.110)); // 110 ms tight release
        lpf1.prepare(sampleRate, 250.0);
        lpf2.prepare(sampleRate, 250.0);
        reset();
    }

    void reset() noexcept
    {
        envDb = 0.0f;
        lpf1.reset();
        lpf2.reset();
    }

    void setLowPassHz(float hz) noexcept
    {
        if (std::abs(hz - appliedLpfHz) > 0.5f)
        {
            appliedLpfHz = hz;
            lpf1.prepare(sampleRate, hz);
            lpf2.prepare(sampleRate, hz);
        }
    }

    float process(float x, float amount0to10) noexcept
    {
        const float filtered = lpf2.processLowpass(lpf1.processLowpass(x));
        const float norm = juce::jlimit(0.0f, 1.0f, amount0to10 * 0.1f);
        if (norm <= 0.001f)
        {
            envDb = 0.0f;
            return filtered;
        }

        // Threshold sweeps from -8 dBFS down to -36 dBFS at 4:1 ratio with auto-makeup.
        const float thresholdDb = -8.0f - norm * 28.0f;
        const float levelDb = juce::Decibels::gainToDecibels(std::abs(filtered), -100.0f);
        const float targetGrDb = computeGainReductionDb(levelDb, thresholdDb, CompRatio::R4_1);

        const float c = (targetGrDb > envDb) ? attackCoeff : releaseCoeff;
        envDb = c * envDb + (1.0f - c) * targetGrDb;

        const float autoMakeupDb = norm * 9.5f;
        return filtered * juce::Decibels::decibelsToGain(autoMakeupDb - envDb);
    }

    float currentGrDb() const noexcept { return envDb; }

private:
    OnePoleFilter lpf1, lpf2;
    double sampleRate = 48000.0;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float envDb = 0.0f;
    float appliedLpfHz = 250.0f;
};

} // namespace tonegrid::multiband
