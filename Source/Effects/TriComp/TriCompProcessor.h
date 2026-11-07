#pragma once
#include "Effects/Multiband/ThreeBandSplitter.h"
#include "Effects/Multiband/DynamicsCore.h"
#include "Effects/Multiband/SpectrumFifo.h"
#include <array>
#include <atomic>
#include <cmath>

namespace tonegrid::triComp
{
using tonegrid::multiband::CompAudioMode;
using tonegrid::multiband::CompBandCell;
using tonegrid::multiband::CompRatio;
using tonegrid::multiband::DetectorMode;
using tonegrid::multiband::SpectrumFifo;
using tonegrid::multiband::ThreeBandSplitter;

struct BandParameters
{
    CompRatio ratio = CompRatio::R4_1;
    float attackMs = 5.0f;      // 0.1 .. 50.0 ms
    float releaseMs = 150.0f;   // 30.0 .. 3000.0 ms
    CompAudioMode audioMode = CompAudioMode::Clean;
    DetectorMode detectorMode = DetectorMode::Off;
    float levelDb = 0.0f;       // -24 .. +24 dB
    bool solo = false;
    bool mute = false;
};

struct Parameters
{
    bool enabled = false;
    int preset = 0;             // 0 .. 7
    float inputDb = 0.0f;       // -24 .. +24 dB (drives into fixed band thresholds)
    float outputDb = 0.0f;      // -24 .. +24 dB
    float mix = 1.0f;           // 0.0 .. 1.0
    float lowMidHz = 140.0f;    // 40 .. 500 Hz
    float midHighHz = 1400.0f;  // 400 .. 6000 Hz
    float linkAmount = 0.0f;    // 0.0 .. 1.0 cross-band sidechain link
    bool audioHighPass = false; // 80 Hz 2nd-order Butterworth HPF on audio path
    std::array<BandParameters, 3> bands{};
};

struct PresetSpec
{
    const char* name;
    Parameters params;
};

inline constexpr std::array<PresetSpec, 8> kPresets{{
    { "01 Fingerstyle Even",
      Parameters{ false, 0,  4.0f, 0.0f, 1.0f, 130.0f, 1400.0f, 0.25f, false, {{
          { CompRatio::R6_1,       8.0f,  180.0f, CompAudioMode::Clean, DetectorMode::HighPass,     1.5f, false, false },
          { CompRatio::R4_1,       4.5f,  130.0f, CompAudioMode::Dist2, DetectorMode::Off,          1.0f, false, false },
          { CompRatio::R3_1,       3.0f,  110.0f, CompAudioMode::Clean, DetectorMode::BandEmphasis, 0.5f, false, false }
      }} } },
    { "02 Pick Punch",
      Parameters{ false, 1,  5.5f, 0.0f, 1.0f, 150.0f, 1600.0f, 0.15f, false, {{
          { CompRatio::R6_1,      12.0f,  140.0f, CompAudioMode::Clean, DetectorMode::HighPass,     2.0f, false, false },
          { CompRatio::R6_1,       9.0f,  110.0f, CompAudioMode::Dist3, DetectorMode::BandEmphasis, 1.5f, false, false },
          { CompRatio::R4_1,       2.0f,   95.0f, CompAudioMode::Dist2, DetectorMode::BandEmphasis, 0.0f, false, false }
      }} } },
    { "03 Slap Tamer",
      Parameters{ false, 2,  6.5f, 0.0f, 1.0f, 120.0f, 1800.0f, 0.10f, true, {{
          { CompRatio::R10_1_Opto, 6.0f,  160.0f, CompAudioMode::Clean, DetectorMode::HighPass,     2.0f, false, false },
          { CompRatio::R4_1,       5.0f,  130.0f, CompAudioMode::Clean, DetectorMode::Off,         -1.0f, false, false },
          { CompRatio::R20_1,      0.4f,   75.0f, CompAudioMode::Dist2, DetectorMode::BandEmphasis, 1.0f, false, false }
      }} } },
    { "04 Motown Opto",
      Parameters{ false, 3,  5.0f, 1.0f, 1.0f, 160.0f, 1200.0f, 0.65f, false, {{
          { CompRatio::R10_1_Opto,18.0f,  420.0f, CompAudioMode::Dist2, DetectorMode::HighPass,     2.5f, false, false },
          { CompRatio::R10_1_Opto,15.0f,  380.0f, CompAudioMode::Dist2, DetectorMode::Off,          1.5f, false, false },
          { CompRatio::R6_1,      10.0f,  300.0f, CompAudioMode::Dist2, DetectorMode::Off,         -1.5f, false, false }
      }} } },
    { "05 Modern Metal Glue",
      Parameters{ false, 4,  7.0f, 0.0f, 1.0f, 140.0f, 1500.0f, 0.20f, true, {{
          { CompRatio::R20_1,      5.0f,  120.0f, CompAudioMode::Clean, DetectorMode::HighPass,     3.0f, false, false },
          { CompRatio::R6_1,       4.0f,  110.0f, CompAudioMode::Dist3, DetectorMode::BandEmphasis, 1.5f, false, false },
          { CompRatio::R6_1,       1.5f,   90.0f, CompAudioMode::Dist3, DetectorMode::BandEmphasis, 2.0f, false, false }
      }} } },
    { "06 Tape Warmth 1:1",
      Parameters{ false, 5,  3.0f, 0.0f, 1.0f, 150.0f, 1500.0f, 0.50f, false, {{
          { CompRatio::R1_1,      10.0f,  200.0f, CompAudioMode::Dist2, DetectorMode::Off,          1.0f, false, false },
          { CompRatio::R1_1,      10.0f,  200.0f, CompAudioMode::Dist2, DetectorMode::Off,          1.5f, false, false },
          { CompRatio::R1_1,      10.0f,  200.0f, CompAudioMode::Dist3, DetectorMode::Off,          0.5f, false, false }
      }} } },
    { "07 Sub Lock Nuke",
      Parameters{ false, 6,  8.0f, 0.0f, 0.85f, 110.0f, 1300.0f, 0.0f, true, {{
          { CompRatio::Nuke,       1.2f,  110.0f, CompAudioMode::Clean, DetectorMode::HighPass,     3.5f, false, false },
          { CompRatio::R4_1,       6.0f,  140.0f, CompAudioMode::Dist2, DetectorMode::Off,          1.0f, false, false },
          { CompRatio::R3_1,       4.0f,  120.0f, CompAudioMode::Clean, DetectorMode::Off,          0.5f, false, false }
      }} } },
    { "08 Parallel Smash",
      Parameters{ false, 7, 10.0f, 0.0f, 0.55f, 145.0f, 1550.0f, 0.40f, false, {{
          { CompRatio::Nuke,       0.8f,  130.0f, CompAudioMode::Dist2, DetectorMode::HighPass,     3.0f, false, false },
          { CompRatio::Nuke,       0.8f,  120.0f, CompAudioMode::Dist3, DetectorMode::BandEmphasis, 2.5f, false, false },
          { CompRatio::R20_1,      0.5f,   95.0f, CompAudioMode::Dist2, DetectorMode::BandEmphasis, 2.0f, false, false }
      }} } }
}};

inline Parameters presetDefaults(int index, bool keepEnabled = false) noexcept
{
    const int clamped = juce::jlimit(0, static_cast<int>(kPresets.size()) - 1, index);
    Parameters p = kPresets[static_cast<size_t>(clamped)].params;
    p.preset = clamped;
    p.enabled = keepEnabled;
    return p;
}

inline const char* ratioName(CompRatio r) noexcept
{
    switch (r)
    {
        case CompRatio::R1_1:       return "1:1";
        case CompRatio::R2_1:       return "2:1";
        case CompRatio::R3_1:       return "3:1";
        case CompRatio::R4_1:       return "4:1";
        case CompRatio::R6_1:       return "6:1";
        case CompRatio::R10_1_Opto: return "10:1 OPTO";
        case CompRatio::R20_1:      return "20:1";
        case CompRatio::Nuke:
        default:                    return "NUKE";
    }
}

inline const char* audioModeName(CompAudioMode m) noexcept
{
    switch (m)
    {
        case CompAudioMode::Clean: return "CLEAN";
        case CompAudioMode::Dist2: return "DIST 2";
        case CompAudioMode::Dist3:
        default:                   return "DIST 3";
    }
}

inline const char* detectorModeName(DetectorMode d) noexcept
{
    switch (d)
    {
        case DetectorMode::Off:           return "FLAT";
        case DetectorMode::HighPass:      return "HPF";
        case DetectorMode::BandEmphasis:  return "EMPH";
        case DetectorMode::HpAndEmphasis:
        default:                          return "HP+EMPH";
    }
}

inline bool valid(const Parameters& p) noexcept
{
    if (p.preset < 0 || p.preset >= static_cast<int>(kPresets.size()))
        return false;
    if (!std::isfinite(p.inputDb) || p.inputDb < -24.0f || p.inputDb > 24.0f)
        return false;
    if (!std::isfinite(p.outputDb) || p.outputDb < -24.0f || p.outputDb > 24.0f)
        return false;
    if (!std::isfinite(p.mix) || p.mix < 0.0f || p.mix > 1.0f)
        return false;
    if (!std::isfinite(p.lowMidHz) || p.lowMidHz < 30.0f || p.lowMidHz > 800.0f)
        return false;
    if (!std::isfinite(p.midHighHz) || p.midHighHz < 250.0f || p.midHighHz > 8000.0f || p.midHighHz <= p.lowMidHz)
        return false;
    if (!std::isfinite(p.linkAmount) || p.linkAmount < 0.0f || p.linkAmount > 1.0f)
        return false;

    for (const auto& b : p.bands)
    {
        const int r = static_cast<int>(b.ratio);
        const int a = static_cast<int>(b.audioMode);
        const int d = static_cast<int>(b.detectorMode);
        if (r < 0 || r > 7 || a < 0 || a > 2 || d < 0 || d > 3)
            return false;
        if (!std::isfinite(b.attackMs) || b.attackMs < 0.05f || b.attackMs > 50.0f)
            return false;
        if (!std::isfinite(b.releaseMs) || b.releaseMs < 30.0f || b.releaseMs > 3500.0f)
            return false;
        if (!std::isfinite(b.levelDb) || b.levelDb < -24.0f || b.levelDb > 24.0f)
            return false;
    }
    return true;
}

struct Meters
{
    std::array<float, 3> grDb{};
    std::array<float, 3> harmonic{};
    float inputPeakDb = -96.0f;
    float outputPeakDb = -96.0f;
};

/**
 * TRI-COMP: 3-Band Tone-Sculpting Bass Compressor.
 * Zero-allocation realtime processor with LR4 phase-compensated 3-band crossover,
 * 3 independent CompBandCell dynamics + harmonic processors, cross-band
 * sidechain linking, and lock-free pre/post SpectrumFifo.
 */
class Processor
{
public:
    void prepare(double newSampleRate, const Parameters& initial = presetDefaults(0)) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        splitter.prepare(sampleRate, initial.lowMidHz, initial.midHighHz);
        for (auto& cell : cells)
            cell.prepare(sampleRate);

        inputGainSmooth.reset(sampleRate, 0.012);
        outputGainSmooth.reset(sampleRate, 0.012);
        mixSmooth.reset(sampleRate, 0.012);
        bypassSmooth.reset(sampleRate, 0.008);
        for (size_t b = 0; b < 3; ++b)
        {
            bandGainSmooth[b].reset(sampleRate, 0.012);
            bandGainSmooth[b].setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.bands[b].levelDb));
        }

        inputGainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.inputDb));
        outputGainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.outputDb));
        mixSmooth.setCurrentAndTargetValue(initial.mix);
        bypassSmooth.setCurrentAndTargetValue(initial.enabled ? 1.0f : 0.0f);
        spectrum.reset();
    }

    void reset() noexcept
    {
        splitter.reset();
        for (auto& cell : cells)
            cell.reset();
        for (size_t b = 0; b < 3; ++b)
        {
            grMeters[b].store(0.0f, std::memory_order_relaxed);
            harmMeters[b].store(0.0f, std::memory_order_relaxed);
        }
    }

    void processBlock(float* samples, int numSamples, const Parameters& p) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return;

        bypassSmooth.setTargetValue(p.enabled ? 1.0f : 0.0f);
        if (!p.enabled && !bypassSmooth.isSmoothing() && bypassSmooth.getCurrentValue() <= 0.0001f)
        {
            for (size_t b = 0; b < 3; ++b)
            {
                grMeters[b].store(0.0f, std::memory_order_relaxed);
                harmMeters[b].store(0.0f, std::memory_order_relaxed);
            }
            return;
        }

        splitter.setCrossovers(p.lowMidHz, p.midHighHz);
        inputGainSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.inputDb));
        outputGainSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.outputDb));
        mixSmooth.setTargetValue(juce::jlimit(0.0f, 1.0f, p.mix));

        std::array<bool, 3> solo{};
        std::array<bool, 3> mute{};
        for (size_t b = 0; b < 3; ++b)
        {
            bandGainSmooth[b].setTargetValue(juce::Decibels::decibelsToGain(p.bands[b].levelDb));
            solo[b] = p.bands[b].solo;
            mute[b] = p.bands[b].mute;
        }

        const float link = juce::jlimit(0.0f, 1.0f, p.linkAmount);
        std::array<float, 3> peakGr{};
        std::array<float, 3> peakHarm{};
        float peakIn = 0.0f;
        float peakOut = 0.0f;

        // Per-band base thresholds tuned for bass energy distribution (Low carries more energy than High)
        static constexpr std::array<float, 3> kBandThresholdsDb{{ -18.0f, -21.0f, -24.0f }};

        for (int i = 0; i < numSamples; ++i)
        {
            const float rawIn = std::isfinite(samples[i]) ? samples[i] : 0.0f;
            peakIn = juce::jmax(peakIn, std::abs(rawIn));

            const float driven = rawIn * inputGainSmooth.getNextValue();
            const auto frame = splitter.processSample(driven);
            const std::array<float, 3> bandIn{{ frame.low, frame.mid, frame.high }};

            std::array<float, 3> rawGr{};
            float maxBandGr = 0.0f;
            for (size_t b = 0; b < 3; ++b)
            {
                const auto& bp = p.bands[b];
                rawGr[b] = cells[b].detectGainReductionDb(bandIn[b],
                                                          kBandThresholdsDb[b],
                                                          bp.ratio,
                                                          bp.attackMs,
                                                          bp.releaseMs,
                                                          bp.detectorMode);
                maxBandGr = juce::jmax(maxBandGr, rawGr[b]);
            }

            std::array<float, 3> bandOut{};
            for (size_t b = 0; b < 3; ++b)
            {
                const auto& bp = p.bands[b];
                const float linkedGr = (1.0f - link) * rawGr[b] + link * maxBandGr;
                bandOut[b] = cells[b].applyGainAndHarmonics(bandIn[b],
                                                            linkedGr,
                                                            bp.audioMode,
                                                            p.audioHighPass && (b == 0),
                                                            bandGainSmooth[b].getNextValue());
                peakGr[b] = juce::jmax(peakGr[b], linkedGr);
                peakHarm[b] = juce::jmax(peakHarm[b], cells[b].currentHarmonicLevel());
            }

            const float wetSum = ThreeBandSplitter::combineBands(bandOut[0], bandOut[1], bandOut[2], solo, mute);
            const float mixVal = mixSmooth.getNextValue();
            const float outGain = outputGainSmooth.getNextValue();
            // Mix against frame.alignedDry so parallel blend is 100% phase-coherent with the LR4 crossover!
            const float dryAligned = frame.alignedDry;
            const float processed = ((1.0f - mixVal) * dryAligned + mixVal * wetSum) * outGain;
            const float activeFade = bypassSmooth.getNextValue();
            const float finalSample = rawIn + activeFade * (processed - rawIn);

            samples[i] = std::isfinite(finalSample) ? finalSample : 0.0f;
            peakOut = juce::jmax(peakOut, std::abs(samples[i]));
            spectrum.push(rawIn, samples[i]);
        }

        for (size_t b = 0; b < 3; ++b)
        {
            grMeters[b].store(peakGr[b], std::memory_order_relaxed);
            harmMeters[b].store(peakHarm[b], std::memory_order_relaxed);
        }
        inputPeakDb.store(juce::Decibels::gainToDecibels(peakIn, -96.0f), std::memory_order_relaxed);
        outputPeakDb.store(juce::Decibels::gainToDecibels(peakOut, -96.0f), std::memory_order_relaxed);
    }

    Meters readMeters() const noexcept
    {
        Meters m;
        for (size_t b = 0; b < 3; ++b)
        {
            m.grDb[b] = grMeters[b].load(std::memory_order_relaxed);
            m.harmonic[b] = harmMeters[b].load(std::memory_order_relaxed);
        }
        m.inputPeakDb = inputPeakDb.load(std::memory_order_relaxed);
        m.outputPeakDb = outputPeakDb.load(std::memory_order_relaxed);
        return m;
    }

    SpectrumFifo& spectrumFifo() noexcept { return spectrum; }
    const SpectrumFifo& spectrumFifo() const noexcept { return spectrum; }

private:
    ThreeBandSplitter splitter;
    std::array<CompBandCell, 3> cells;
    juce::SmoothedValue<float> inputGainSmooth{ 1.0f };
    juce::SmoothedValue<float> outputGainSmooth{ 1.0f };
    juce::SmoothedValue<float> mixSmooth{ 1.0f };
    juce::SmoothedValue<float> bypassSmooth{ 0.0f };
    std::array<juce::SmoothedValue<float>, 3> bandGainSmooth{};

    std::array<std::atomic<float>, 3> grMeters{};
    std::array<std::atomic<float>, 3> harmMeters{};
    std::atomic<float> inputPeakDb{ -96.0f };
    std::atomic<float> outputPeakDb{ -96.0f };
    SpectrumFifo spectrum;
    double sampleRate = 48000.0;
};

} // namespace tonegrid::triComp
