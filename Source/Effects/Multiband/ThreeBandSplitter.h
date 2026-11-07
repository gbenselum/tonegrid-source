#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace tonegrid::multiband
{
struct BandFrame
{
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    // Phase-aligned dry sum (low + mid + high). Magnitude-flat (allpass)
    // and phase-coherent with the three bands for parallel dry/wet mixing.
    float alignedDry = 0.0f;
};

/**
 * Phase-compensated 4th-order Linkwitz-Riley (24 dB/oct) 3-band splitter.
 *
 * Stage 1 splits x at f1 into (lowRaw, midHighRaw).
 * Stage 2 splits midHighRaw at f2 into (mid, high).
 * Stage 2-comp passes lowRaw through an identical LR4 stage at f2 and sums
 * its low + high outputs, imparting the exact same allpass phase response
 * AP(f2) that (mid + high) receives.
 *
 * Consequently:
 *   low + mid + high == AP(f2) * AP(f1) * x
 * which is magnitude-flat (0.000 dB ripple) across 0..Nyquist for all f1, f2.
 */
class ThreeBandSplitter
{
public:
    void prepare(double newSampleRate, float initialLowMidHz = 150.0f, float initialMidHighHz = 1600.0f) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        juce::dsp::ProcessSpec spec{ sampleRate, 256, 1 };
        lowSplit.prepare(spec);
        highSplit.prepare(spec);
        lowPhaseComp.prepare(spec);
        lowSplit.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
        highSplit.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
        lowPhaseComp.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);

        lowMidSmooth.reset(sampleRate, 0.015);
        midHighSmooth.reset(sampleRate, 0.015);
        const auto clamped = clampCrossovers(initialLowMidHz, initialMidHighHz);
        lowMidSmooth.setCurrentAndTargetValue(clamped.first);
        midHighSmooth.setCurrentAndTargetValue(clamped.second);
        applyCoefficients(clamped.first, clamped.second);
        reset();
    }

    void reset() noexcept
    {
        lowSplit.reset();
        highSplit.reset();
        lowPhaseComp.reset();
        subTick = 0;
    }

    void setCrossovers(float lowMidHz, float midHighHz) noexcept
    {
        const auto clamped = clampCrossovers(lowMidHz, midHighHz);
        lowMidSmooth.setTargetValue(clamped.first);
        midHighSmooth.setTargetValue(clamped.second);
    }

    BandFrame processSample(float x) noexcept
    {
        if (!std::isfinite(x))
            x = 0.0f;

        const float f1 = lowMidSmooth.getNextValue();
        const float f2 = midHighSmooth.getNextValue();
        if (subTick == 0 && (std::abs(f1 - appliedLowMidHz) > 0.05f || std::abs(f2 - appliedMidHighHz) > 0.1f))
            applyCoefficients(f1, f2);
        subTick = (subTick + 1) & 15;

        float lowRaw = 0.0f, midHighRaw = 0.0f;
        lowSplit.processSample(0, x, lowRaw, midHighRaw);

        float mid = 0.0f, high = 0.0f;
        highSplit.processSample(0, midHighRaw, mid, high);

        float compLow = 0.0f, compHigh = 0.0f;
        lowPhaseComp.processSample(0, lowRaw, compLow, compHigh);
        const float low = compLow + compHigh;

        return { low, mid, high, low + mid + high };
    }

    static float combineBands(float low, float mid, float high,
                              const std::array<bool, 3>& solo,
                              const std::array<bool, 3>& mute) noexcept
    {
        const bool anySolo = solo[0] || solo[1] || solo[2];
        const float g0 = (anySolo ? solo[0] : !mute[0]) ? 1.0f : 0.0f;
        const float g1 = (anySolo ? solo[1] : !mute[1]) ? 1.0f : 0.0f;
        const float g2 = (anySolo ? solo[2] : !mute[2]) ? 1.0f : 0.0f;
        return low * g0 + mid * g1 + high * g2;
    }

    float currentLowMidHz() const noexcept { return appliedLowMidHz; }
    float currentMidHighHz() const noexcept { return appliedMidHighHz; }

private:
    std::pair<float, float> clampCrossovers(float f1, float f2) const noexcept
    {
        const float nyquistGuard = static_cast<float>(sampleRate * 0.45);
        const float c1 = std::isfinite(f1) ? juce::jlimit(30.0f, juce::jmin(800.0f, nyquistGuard), f1) : 150.0f;
        const float minF2 = juce::jmax(c1 * 1.25f, 300.0f);
        const float c2 = std::isfinite(f2) ? juce::jlimit(minF2, juce::jmin(8000.0f, nyquistGuard), f2) : 1600.0f;
        return { c1, c2 };
    }

    void applyCoefficients(float f1, float f2) noexcept
    {
        appliedLowMidHz = f1;
        appliedMidHighHz = f2;
        lowSplit.setCutoffFrequency(f1);
        highSplit.setCutoffFrequency(f2);
        lowPhaseComp.setCutoffFrequency(f2);
    }

    juce::dsp::LinkwitzRileyFilter<float> lowSplit;
    juce::dsp::LinkwitzRileyFilter<float> highSplit;
    juce::dsp::LinkwitzRileyFilter<float> lowPhaseComp;

    juce::SmoothedValue<float> lowMidSmooth{ 150.0f };
    juce::SmoothedValue<float> midHighSmooth{ 1600.0f };
    float appliedLowMidHz = 150.0f;
    float appliedMidHighHz = 1600.0f;
    double sampleRate = 48000.0;
    int subTick = 0;
};

} // namespace tonegrid::multiband
