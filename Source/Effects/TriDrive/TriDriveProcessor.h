#pragma once
#include "Effects/Multiband/ThreeBandSplitter.h"
#include "Effects/Multiband/DynamicsCore.h"
#include "Effects/Multiband/Saturators.h"
#include "Effects/Multiband/SpectrumFifo.h"
#include "Effects/NamCapture/CaptureModel.h"
#include "Effects/Pedalboard/ToneTilt.h"
#include "Effects/GraphicEq/GraphicEqProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <cmath>

namespace tonegrid::triDrive
{
using tonegrid::multiband::DriveCharacter;
using tonegrid::multiband::LowBandBusComp;
using tonegrid::multiband::OnePoleFilter;
using tonegrid::multiband::OversampledBandDrive;
using tonegrid::multiband::SpectrumFifo;
using tonegrid::multiband::ThreeBandSplitter;

inline constexpr std::array<float, 7> kPostEqFrequencies{{
    80.0f, 180.0f, 400.0f, 800.0f, 1600.0f, 3000.0f, 5000.0f
}};

/**
 * Trivially-copyable realtime parameters for TRI-DRIVE (safe for ParameterMailbox<Parameters>).
 * File paths/hashes for NAM captures and Cabinet IRs are stored separately on the processor
 * for UI/preset serialization because juce::String is not trivially copyable.
 */
struct Parameters
{
    bool enabled = false;
    int preset = 0;                 // 0 .. 7
    float inputDb = 0.0f;           // -24 .. +24 dB
    float outputDb = 0.0f;          // -24 .. +24 dB
    float blend = 1.0f;             // 0.0 .. 1.0 global wet blend

    float lowMidHz = 150.0f;        // 60 .. 500 Hz
    float midHighHz = 1800.0f;      // 600 .. 6000 Hz

    // Band 1: Clean Low + Bus Compressor + variable LPF
    float lowComp = 4.5f;           // 0 .. 10
    float lowLpfHz = 220.0f;        // 80 .. 500 Hz
    float lowLevelDb = 0.0f;        // -24 .. +24 dB
    bool lowSolo = false;
    bool lowMute = false;

    // Band 2: Mid Saturation (200 Hz .. 2.5 kHz growl & grind)
    DriveCharacter midCharacter = DriveCharacter::Grind;
    float midDrive = 5.5f;          // 0 .. 10
    float midPresence = 1.0f;       // -6 .. +6 dB tilt
    float midLevelDb = 0.0f;        // -24 .. +24 dB
    bool midSolo = false;
    bool midMute = false;

    // Band 3: High Saturation (1.5 kHz+ pick attack & clank)
    DriveCharacter highCharacter = DriveCharacter::Fizz;
    float highDrive = 5.0f;         // 0 .. 10
    float highHpfHz = 1600.0f;      // 500 .. 5000 Hz
    float highLevelDb = 0.0f;       // -24 .. +24 dB
    bool highSolo = false;
    bool highMute = false;

    // Mid+High Cabinet IR toggle (Low band always bypasses Cab IR to stay phase-solid!)
    bool cabSimEnabled = true;

    // Post-Sum 7-Band Bass Graphic EQ
    bool postEqEnabled = true;
    std::array<float, 7> postEqBands{{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }};
};

/**
 * Control-thread asset metadata for NAM-in-Band and M+H Cab IR serialization.
 */
struct AssetState
{
    juce::String midNamFile, midNamHash, midNamLabel;
    juce::String highNamFile, highNamHash, highNamLabel;
    juce::String cabIrFile, cabIrHash, cabIrLabel;
};

struct PresetSpec
{
    const char* name;
    Parameters params;
};

inline constexpr std::array<PresetSpec, 8> kPresets{{
    { "01 Modern Clank",
      Parameters{ false, 0,  0.0f, 0.0f, 1.0f, 150.0f, 1800.0f,
                  6.0f, 210.0f,  1.5f, false, false,
                  DriveCharacter::Grind, 6.2f,  1.5f,  0.5f, false, false,
                  DriveCharacter::Fizz,  6.5f, 1700.0f, 1.0f, false, false,
                  true, true, {{ 1.5f, 0.5f, -2.5f, -1.5f, 2.5f, 3.5f, 1.5f }} } },
    { "02 Clean Sub + Tube Growl",
      Parameters{ false, 1,  0.0f, 0.0f, 1.0f, 135.0f, 1500.0f,
                  5.0f, 190.0f,  2.0f, false, false,
                  DriveCharacter::Tube,  5.5f,  0.5f,  1.0f, false, false,
                  DriveCharacter::Tube,  3.5f, 1400.0f,-0.5f, false, false,
                  true, true, {{ 2.0f, 1.5f,  1.0f,  2.0f, 0.5f,-1.5f,-2.5f }} } },
    { "03 Fuzz On Top",
      Parameters{ false, 2,  1.0f, 0.0f, 0.90f, 160.0f, 1650.0f,
                  6.5f, 230.0f,  2.5f, false, false,
                  DriveCharacter::Fuzz,  7.5f,  0.0f,  1.5f, false, false,
                  DriveCharacter::Fizz,  5.5f, 1600.0f, 0.5f, false, false,
                  true, true, {{ 2.5f, 1.0f, -1.0f,  1.5f, 2.0f, 1.0f,-1.0f }} } },
    { "04 Lemmy Grind",
      Parameters{ false, 3,  2.0f, 0.0f, 1.0f, 180.0f, 1400.0f,
                  3.5f, 260.0f,  0.0f, false, false,
                  DriveCharacter::Grind, 7.8f,  3.0f,  3.0f, false, false,
                  DriveCharacter::Tube,  6.5f, 1300.0f, 1.5f, false, false,
                  true, true, {{ -1.0f, 2.0f,  4.0f,  3.5f, 2.0f, 0.5f,-1.5f }} } },
    { "05 Studio DI + Grit",
      Parameters{ false, 4, -1.0f, 0.0f, 0.70f, 140.0f, 1700.0f,
                  4.5f, 200.0f,  1.5f, false, false,
                  DriveCharacter::Tube,  3.8f,  1.0f,  0.0f, false, false,
                  DriveCharacter::Fizz,  3.2f, 1800.0f, 0.0f, false, false,
                  false, true, {{ 1.0f, 1.0f,  0.0f,  0.5f, 1.5f, 1.5f, 0.5f }} } },
    { "06 Sub-Safe Doom",
      Parameters{ false, 5,  3.0f, 0.0f, 1.0f, 125.0f, 1250.0f,
                  7.5f, 175.0f,  3.0f, false, false,
                  DriveCharacter::Fuzz,  8.8f, -1.5f,  2.0f, false, false,
                  DriveCharacter::Grind, 7.0f, 1200.0f, 0.5f, false, false,
                  true, true, {{ 3.5f, 2.5f,  1.0f,  0.0f, 1.5f,-1.0f,-3.0f }} } },
    { "07 Slap Sparkle",
      Parameters{ false, 6,  0.0f, 0.0f, 0.85f, 130.0f, 2100.0f,
                  6.0f, 185.0f,  2.0f, false, false,
                  DriveCharacter::Tube,  3.0f, -1.0f, -1.5f, false, false,
                  DriveCharacter::Fizz,  6.0f, 2000.0f, 2.5f, false, false,
                  true, true, {{ 3.0f, 1.5f, -3.5f, -2.5f, 1.5f, 3.5f, 3.0f }} } },
    { "08 Heavy Pick Aggression",
      Parameters{ false, 7,  2.5f, 0.0f, 1.0f, 155.0f, 1600.0f,
                  7.0f, 215.0f,  2.0f, false, false,
                  DriveCharacter::Grind, 8.2f,  2.5f,  1.5f, false, false,
                  DriveCharacter::Fizz,  7.8f, 1550.0f, 2.0f, false, false,
                  true, true, {{ 2.0f, 1.0f, -2.0f,  1.0f, 3.5f, 4.0f, 1.5f }} } }
}};

inline Parameters presetDefaults(int index, bool keepEnabled = false) noexcept
{
    const int clamped = juce::jlimit(0, static_cast<int>(kPresets.size()) - 1, index);
    Parameters p = kPresets[static_cast<size_t>(clamped)].params;
    p.preset = clamped;
    p.enabled = keepEnabled;
    return p;
}

inline const char* characterName(DriveCharacter c) noexcept
{
    switch (c)
    {
        case DriveCharacter::Tube:  return "TUBE";
        case DriveCharacter::Grind: return "GRIND";
        case DriveCharacter::Fuzz:  return "FUZZ";
        case DriveCharacter::Fizz:  return "FIZZ";
        case DriveCharacter::Nam:
        default:                    return "NAM";
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
    if (!std::isfinite(p.blend) || p.blend < 0.0f || p.blend > 1.0f)
        return false;
    if (!std::isfinite(p.lowMidHz) || p.lowMidHz < 40.0f || p.lowMidHz > 600.0f)
        return false;
    if (!std::isfinite(p.midHighHz) || p.midHighHz < 400.0f || p.midHighHz > 7000.0f || p.midHighHz <= p.lowMidHz)
        return false;
    if (!std::isfinite(p.lowComp) || p.lowComp < 0.0f || p.lowComp > 10.0f)
        return false;
    if (!std::isfinite(p.lowLpfHz) || p.lowLpfHz < 60.0f || p.lowLpfHz > 600.0f)
        return false;
    if (!std::isfinite(p.lowLevelDb) || p.lowLevelDb < -24.0f || p.lowLevelDb > 24.0f)
        return false;

    const int mc = static_cast<int>(p.midCharacter);
    const int hc = static_cast<int>(p.highCharacter);
    if (mc < 0 || mc > 4 || hc < 0 || hc > 4)
        return false;
    if (!std::isfinite(p.midDrive) || p.midDrive < 0.0f || p.midDrive > 10.0f)
        return false;
    if (!std::isfinite(p.midPresence) || p.midPresence < -6.0f || p.midPresence > 6.0f)
        return false;
    if (!std::isfinite(p.midLevelDb) || p.midLevelDb < -24.0f || p.midLevelDb > 24.0f)
        return false;
    if (!std::isfinite(p.highDrive) || p.highDrive < 0.0f || p.highDrive > 10.0f)
        return false;
    if (!std::isfinite(p.highHpfHz) || p.highHpfHz < 400.0f || p.highHpfHz > 6000.0f)
        return false;
    if (!std::isfinite(p.highLevelDb) || p.highLevelDb < -24.0f || p.highLevelDb > 24.0f)
        return false;

    for (float db : p.postEqBands)
        if (!std::isfinite(db) || db < -12.0f || db > 12.0f)
            return false;

    return true;
}

struct Meters
{
    float lowCompGrDb = 0.0f;
    std::array<float, 3> bandPeakDb{{ -96.0f, -96.0f, -96.0f }};
    float inputPeakDb = -96.0f;
    float outputPeakDb = -96.0f;
};

/**
 * Built-in zero-allocation Mid+High Cabinet speaker simulator + custom IR FIR stage.
 * Models a classic 8x10 sealed bass cabinet resonance (bump at 110 Hz, scoop at 380 Hz,
 * presence peak at 1.9 kHz, and steep 4th-order speaker roll-off above 4.8 kHz),
 * OR convolves with a user-loaded cabinet IR (up to 512 taps, realtime safe).
 */
class MidHighCabinetStage
{
public:
    static constexpr size_t kMaxIrTaps = 512;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        hpf1.prepare(sampleRate, 95.0);
        hpf2.prepare(sampleRate, 95.0);
        lpf1.prepare(sampleRate, 4600.0);
        lpf2.prepare(sampleRate, 4600.0);
        lpf3.prepare(sampleRate, 5200.0);
        coneDipFilter.prepare(sampleRate, 1800.0);
        reset();
    }

    void reset() noexcept
    {
        hpf1.reset();
        hpf2.reset();
        lpf1.reset();
        lpf2.reset();
        lpf3.reset();
        coneDipFilter.reset();
        firHistory.fill(0.0f);
        firWritePos = 0;
    }

    // Control-thread only (called with player detached when user loads a custom IR WAV)
    void installCustomIr(const float* taps, int numTaps) noexcept
    {
        customIrActive.store(false, std::memory_order_release);
        customIrTaps.fill(0.0f);
        const int count = juce::jlimit(0, static_cast<int>(kMaxIrTaps), numTaps);
        if (taps != nullptr && count > 0)
        {
            double energy = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const float v = std::isfinite(taps[i]) ? taps[i] : 0.0f;
                customIrTaps[static_cast<size_t>(i)] = v;
                energy += static_cast<double>(v) * v;
            }
            const float norm = energy > 1e-9 ? static_cast<float>(1.0 / std::sqrt(energy)) : 1.0f;
            for (int i = 0; i < count; ++i)
                customIrTaps[static_cast<size_t>(i)] *= norm;
            customIrLength = count;
            customIrActive.store(true, std::memory_order_release);
        }
        else
        {
            customIrLength = 0;
        }
    }

    void clearCustomIr() noexcept
    {
        customIrActive.store(false, std::memory_order_release);
        customIrLength = 0;
    }

    bool hasCustomIr() const noexcept { return customIrActive.load(std::memory_order_acquire); }

    float processSample(float x) noexcept
    {
        if (customIrActive.load(std::memory_order_acquire) && customIrLength > 0)
        {
            firHistory[static_cast<size_t>(firWritePos)] = x;
            float acc = 0.0f;
            int idx = firWritePos;
            const int len = customIrLength;
            for (int k = 0; k < len; ++k)
            {
                acc += customIrTaps[static_cast<size_t>(k)] * firHistory[static_cast<size_t>(idx)];
                idx = (idx - 1) & (static_cast<int>(kMaxIrTaps) - 1);
            }
            firWritePos = (firWritePos + 1) & (static_cast<int>(kMaxIrTaps) - 1);
            return acc;
        }

        // Built-in 8x10 bass cabinet contour for Mid + High distortion bands:
        // trims sub-95 Hz mud, shapes 1.8 kHz speaker cone vocal peak, and rolls off >4.6 kHz fizz.
        const float tight = hpf2.processHighpass(hpf1.processHighpass(x));
        const float cone = coneDipFilter.processLowpass(tight);
        const float voiced = tight * 0.75f + (tight - cone) * 0.55f + cone * 0.35f;
        return lpf3.processLowpass(lpf2.processLowpass(lpf1.processLowpass(voiced))) * 1.15f;
    }

private:
    OnePoleFilter hpf1, hpf2, lpf1, lpf2, lpf3, coneDipFilter;
    std::array<float, kMaxIrTaps> customIrTaps{};
    std::array<float, kMaxIrTaps> firHistory{};
    int customIrLength = 0;
    int firWritePos = 0;
    std::atomic<bool> customIrActive{ false };
    double sampleRate = 48000.0;
};

/**
 * TRI-DRIVE: 3-Band Bass Distortion & NAM-in-Band Processor.
 *
 * Signal flow:
 * 1. Input gain -> Phase-compensated LR4 3-band splitter (Low, Mid, High)
 * 2. Band 1 (Low): LowBandBusComp + variable LPF + delay compensation matching 4x OS latency
 * 3. Band 2 (Mid): 4x OversampledBandDrive (Tube/Grind/Fuzz) OR NAM capture slot + Presence tilt
 * 4. Band 3 (High): Variable pre-HPF + 4x OversampledBandDrive (Tube/Fizz) OR NAM capture slot
 * 5. Mid + High sum -> Optional 8x10 / Custom IR Cabinet stage (Low band bypasses cab IR!)
 * 6. Band level & Solo/Mute sum -> Optional 7-Band Post Bass Graphic EQ -> Global Blend -> Output
 */
class Processor
{
public:
    static constexpr size_t kChunkSize = 128;
    static constexpr size_t kDelayMask = 63;

    void prepare(double newSampleRate, const Parameters& initial = presetDefaults(0)) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        splitter.prepare(sampleRate, initial.lowMidHz, initial.midHighHz);
        lowBusComp.prepare(sampleRate);
        midDriveStage.prepare(sampleRate, kChunkSize);
        highDriveStage.prepare(sampleRate, kChunkSize);
        midPresenceTilt.prepare(sampleRate, initial.midPresence);
        highPreHpf1.prepare(sampleRate, initial.highHpfHz);
        highPreHpf2.prepare(sampleRate, initial.highHpfHz);
        appliedHighHpfHz = initial.highHpfHz;
        cabStage.prepare(sampleRate);
        postEq.prepare(sampleRate, initial.postEqBands, kPostEqFrequencies);

        inputGainSmooth.reset(sampleRate, 0.012);
        outputGainSmooth.reset(sampleRate, 0.012);
        blendSmooth.reset(sampleRate, 0.012);
        bypassSmooth.reset(sampleRate, 0.008);
        lowLevelSmooth.reset(sampleRate, 0.012);
        midLevelSmooth.reset(sampleRate, 0.012);
        highLevelSmooth.reset(sampleRate, 0.012);

        inputGainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.inputDb));
        outputGainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.outputDb));
        blendSmooth.setCurrentAndTargetValue(initial.blend);
        bypassSmooth.setCurrentAndTargetValue(initial.enabled ? 1.0f : 0.0f);
        lowLevelSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.lowLevelDb));
        midLevelSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.midLevelDb));
        highLevelSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initial.highLevelDb));

        for (auto& m : bandNamModels)
            if (m.dsp)
            {
                m.dsp->SetPrewarmOnReset(false);
                m.dsp->Reset(sampleRate, static_cast<int>(kChunkSize));
                m.dsp->prewarm();
            }

        reset();
    }

    void reset() noexcept
    {
        splitter.reset();
        lowBusComp.reset();
        midDriveStage.reset();
        highDriveStage.reset();
        midPresenceTilt.reset();
        highPreHpf1.reset();
        highPreHpf2.reset();
        cabStage.reset();
        postEq.reset();
        lowDelayLine.fill(0.0f);
        dryDelayLine.fill(0.0f);
        delayWriteIdx = 0;
        lowGrMeter.store(0.0f, std::memory_order_relaxed);
    }

    // Control-thread only (called with audio player detached, matching board::Processor::installCustom)
    // bandIndex: 0 = Mid band NAM slot, 1 = High band NAM slot
    void installBandNam(int bandIndex, board::Model model) noexcept
    {
        if (bandIndex < 0 || bandIndex > 1)
            return;
        if (model.dsp)
        {
            model.dsp->SetPrewarmOnReset(false);
            model.dsp->Reset(sampleRate, static_cast<int>(kChunkSize));
            model.dsp->prewarm();
        }
        auto& slot = bandNamModels[static_cast<size_t>(bandIndex)];
        if (bandIndex == 0)
        {
            assets.midNamFile = model.file;
            assets.midNamHash = model.hash;
            assets.midNamLabel = model.label;
        }
        else
        {
            assets.highNamFile = model.file;
            assets.highNamHash = model.hash;
            assets.highNamLabel = model.label;
        }
        slot = std::move(model);
    }

    void clearBandNam(int bandIndex) noexcept
    {
        if (bandIndex < 0 || bandIndex > 1)
            return;
        bandNamModels[static_cast<size_t>(bandIndex)] = {};
        if (bandIndex == 0)
            assets.midNamFile = assets.midNamHash = assets.midNamLabel = {};
        else
            assets.highNamFile = assets.highNamHash = assets.highNamLabel = {};
    }

    bool hasBandNam(int bandIndex) const noexcept
    {
        return bandIndex >= 0 && bandIndex <= 1 && bandNamModels[static_cast<size_t>(bandIndex)].dsp != nullptr;
    }

    const board::Model& bandNam(int bandIndex) const noexcept
    {
        return bandNamModels[static_cast<size_t>(juce::jlimit(0, 1, bandIndex))];
    }

    juce::Result loadCabIrFromFile(const juce::File& file)
    {
        if (!file.existsAsFile())
        {
            cabStage.clearCustomIr();
            assets.cabIrFile = assets.cabIrHash = assets.cabIrLabel = {};
            return juce::Result::fail("Cabinet IR file does not exist.");
        }
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0)
            return juce::Result::fail("Unsupported Cabinet IR audio file.");

        const int numSamples = static_cast<int>(std::min<juce::int64>(MidHighCabinetStage::kMaxIrTaps, reader->lengthInSamples));
        juce::AudioBuffer<float> temp(1, numSamples);
        reader->read(&temp, 0, numSamples, 0, true, false);
        cabStage.installCustomIr(temp.getReadPointer(0), numSamples);
        assets.cabIrFile = file.getFullPathName();
        assets.cabIrLabel = file.getFileNameWithoutExtension();
        assets.cabIrHash = juce::SHA256(file).toHexString();
        return juce::Result::ok();
    }

    void clearCabIr() noexcept
    {
        cabStage.clearCustomIr();
        assets.cabIrFile = assets.cabIrHash = assets.cabIrLabel = {};
    }

    AssetState& assetState() noexcept { return assets; }
    const AssetState& assetState() const noexcept { return assets; }

    void processBlock(float* samples, int numSamples, const Parameters& p) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return;

        bypassSmooth.setTargetValue(p.enabled ? 1.0f : 0.0f);
        if (!p.enabled && !bypassSmooth.isSmoothing() && bypassSmooth.getCurrentValue() <= 0.0001f)
        {
            lowGrMeter.store(0.0f, std::memory_order_relaxed);
            return;
        }

        splitter.setCrossovers(p.lowMidHz, p.midHighHz);
        lowBusComp.setLowPassHz(p.lowLpfHz);
        midPresenceTilt.setTone(p.midPresence);
        if (std::abs(p.highHpfHz - appliedHighHpfHz) > 0.5f)
        {
            appliedHighHpfHz = p.highHpfHz;
            highPreHpf1.prepare(sampleRate, appliedHighHpfHz);
            highPreHpf2.prepare(sampleRate, appliedHighHpfHz);
        }
        postEq.set(p.postEqBands);

        inputGainSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.inputDb));
        outputGainSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.outputDb));
        blendSmooth.setTargetValue(juce::jlimit(0.0f, 1.0f, p.blend));
        lowLevelSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.lowLevelDb));
        midLevelSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.midLevelDb));
        highLevelSmooth.setTargetValue(juce::Decibels::decibelsToGain(p.highLevelDb));

        const std::array<bool, 3> solo{{ p.lowSolo, p.midSolo, p.highSolo }};
        const std::array<bool, 3> mute{{ p.lowMute, p.midMute, p.highMute }};
        const int osLatency = juce::jlimit(0, static_cast<int>(kDelayMask) - 1, midDriveStage.latencySamples());

        float peakIn = 0.0f;
        float peakOut = 0.0f;
        std::array<float, 3> peakBands{};
        float peakLowGr = 0.0f;

        for (int pos = 0; pos < numSamples; pos += static_cast<int>(kChunkSize))
        {
            const int length = juce::jmin(static_cast<int>(kChunkSize), numSamples - pos);

            // Step 1: Split chunk into Low, Mid, High and pre-filter High band
            for (int i = 0; i < length; ++i)
            {
                const float rawIn = std::isfinite(samples[pos + i]) ? samples[pos + i] : 0.0f;
                rawChunk[static_cast<size_t>(i)] = rawIn;
                peakIn = juce::jmax(peakIn, std::abs(rawIn));

                const float driven = rawIn * inputGainSmooth.getNextValue();
                const auto frame = splitter.processSample(driven);

                // Delay low band & aligned dry to match 4x oversampler group delay on Mid/High
                lowDelayLine[static_cast<size_t>(delayWriteIdx)] = frame.low;
                dryDelayLine[static_cast<size_t>(delayWriteIdx)] = frame.alignedDry;
                const int readIdx = (delayWriteIdx - osLatency) & static_cast<int>(kDelayMask);
                delayWriteIdx = (delayWriteIdx + 1) & static_cast<int>(kDelayMask);

                const float alignedLow = lowDelayLine[static_cast<size_t>(readIdx)];
                alignedDryChunk[static_cast<size_t>(i)] = dryDelayLine[static_cast<size_t>(readIdx)];

                lowBuf[static_cast<size_t>(i)] = lowBusComp.process(alignedLow, p.lowComp);
                peakLowGr = juce::jmax(peakLowGr, lowBusComp.currentGrDb());

                midBuf[static_cast<size_t>(i)] = frame.mid;
                highBuf[static_cast<size_t>(i)] = highPreHpf2.processHighpass(highPreHpf1.processHighpass(frame.high));
            }

            // Step 2: Process Mid Band (4x OS Drive OR NAM capture slot)
            const float midDriveNorm = juce::jlimit(0.0f, 1.0f, p.midDrive * 0.1f);
            if (p.midCharacter == DriveCharacter::Nam && bandNamModels[0].dsp != nullptr)
            {
                const float preGain = juce::Decibels::decibelsToGain(-6.0f + midDriveNorm * 18.0f) * bandNamModels[0].inputGain;
                for (int i = 0; i < length; ++i)
                    midBuf[static_cast<size_t>(i)] *= preGain;
                float* inPtr = midBuf.data();
                float* outPtr = midOutBuf.data();
                try { bandNamModels[0].dsp->process(&inPtr, &outPtr, length); }
                catch (...) { std::fill_n(midOutBuf.begin(), length, 0.0f); }
                const float postGain = bandNamModels[0].outputGain;
                for (int i = 0; i < length; ++i)
                    midOutBuf[static_cast<size_t>(i)] *= postGain;
            }
            else
            {
                const auto algoChar = (p.midCharacter == DriveCharacter::Nam) ? DriveCharacter::Grind : p.midCharacter;
                midDriveStage.processBlock(midBuf.data(), midOutBuf.data(), length, midDriveNorm, algoChar);
            }

            // Step 3: Process High Band (4x OS Drive OR NAM capture slot)
            const float highDriveNorm = juce::jlimit(0.0f, 1.0f, p.highDrive * 0.1f);
            if (p.highCharacter == DriveCharacter::Nam && bandNamModels[1].dsp != nullptr)
            {
                const float preGain = juce::Decibels::decibelsToGain(-6.0f + highDriveNorm * 18.0f) * bandNamModels[1].inputGain;
                for (int i = 0; i < length; ++i)
                    highBuf[static_cast<size_t>(i)] *= preGain;
                float* inPtr = highBuf.data();
                float* outPtr = highOutBuf.data();
                try { bandNamModels[1].dsp->process(&inPtr, &outPtr, length); }
                catch (...) { std::fill_n(highOutBuf.begin(), length, 0.0f); }
                const float postGain = bandNamModels[1].outputGain;
                for (int i = 0; i < length; ++i)
                    highOutBuf[static_cast<size_t>(i)] *= postGain;
            }
            else
            {
                const auto algoChar = (p.highCharacter == DriveCharacter::Nam) ? DriveCharacter::Fizz : p.highCharacter;
                highDriveStage.processBlock(highBuf.data(), highOutBuf.data(), length, highDriveNorm, algoChar);
            }

            // Step 4: Per-band level, Mid presence tilt, M+H Cab IR, Band sum, Post-EQ, and Global Blend
            for (int i = 0; i < length; ++i)
            {
                const size_t k = static_cast<size_t>(i);
                const float bLow = lowBuf[k] * lowLevelSmooth.getNextValue();
                const float bMid = midPresenceTilt.process(midOutBuf[k]) * midLevelSmooth.getNextValue();
                const float bHigh = highOutBuf[k] * highLevelSmooth.getNextValue();

                peakBands[0] = juce::jmax(peakBands[0], std::abs(bLow));
                peakBands[1] = juce::jmax(peakBands[1], std::abs(bMid));
                peakBands[2] = juce::jmax(peakBands[2], std::abs(bHigh));

                // Route Mid & High through Cab Sim if enabled; Low band ALWAYS stays direct & tight!
                const bool anySolo = solo[0] || solo[1] || solo[2];
                const float gLow  = (anySolo ? solo[0] : !mute[0]) ? 1.0f : 0.0f;
                const float gMid  = (anySolo ? solo[1] : !mute[1]) ? 1.0f : 0.0f;
                const float gHigh = (anySolo ? solo[2] : !mute[2]) ? 1.0f : 0.0f;

                const float midHighSum = bMid * gMid + bHigh * gHigh;
                const float cabOut = p.cabSimEnabled ? cabStage.processSample(midHighSum) : midHighSum;
                float wet = bLow * gLow + cabOut;

                if (p.postEqEnabled)
                    wet = postEq.process(wet);

                const float blendVal = blendSmooth.getNextValue();
                const float outGain = outputGainSmooth.getNextValue();
                const float dryAligned = alignedDryChunk[k];
                const float blended = ((1.0f - blendVal) * dryAligned + blendVal * wet) * outGain;

                const float rawIn = rawChunk[k];
                const float activeFade = bypassSmooth.getNextValue();
                const float finalSample = rawIn + activeFade * (blended - rawIn);

                samples[pos + i] = std::isfinite(finalSample) ? finalSample : 0.0f;
                peakOut = juce::jmax(peakOut, std::abs(samples[pos + i]));
                spectrum.push(rawIn, samples[pos + i]);
            }
        }

        lowGrMeter.store(peakLowGr, std::memory_order_relaxed);
        for (size_t b = 0; b < 3; ++b)
            bandPeakMeters[b].store(juce::Decibels::gainToDecibels(peakBands[b], -96.0f), std::memory_order_relaxed);
        inputPeakDb.store(juce::Decibels::gainToDecibels(peakIn, -96.0f), std::memory_order_relaxed);
        outputPeakDb.store(juce::Decibels::gainToDecibels(peakOut, -96.0f), std::memory_order_relaxed);
    }

    Meters readMeters() const noexcept
    {
        Meters m;
        m.lowCompGrDb = lowGrMeter.load(std::memory_order_relaxed);
        for (size_t b = 0; b < 3; ++b)
            m.bandPeakDb[b] = bandPeakMeters[b].load(std::memory_order_relaxed);
        m.inputPeakDb = inputPeakDb.load(std::memory_order_relaxed);
        m.outputPeakDb = outputPeakDb.load(std::memory_order_relaxed);
        return m;
    }

    SpectrumFifo& spectrumFifo() noexcept { return spectrum; }
    const SpectrumFifo& spectrumFifo() const noexcept { return spectrum; }

private:
    ThreeBandSplitter splitter;
    LowBandBusComp lowBusComp;
    OversampledBandDrive midDriveStage;
    OversampledBandDrive highDriveStage;
    board::ToneTilt midPresenceTilt;
    OnePoleFilter highPreHpf1, highPreHpf2;
    MidHighCabinetStage cabStage;
    board::GraphicEq postEq;

    std::array<board::Model, 2> bandNamModels;
    AssetState assets;

    std::array<float, kDelayMask + 1> lowDelayLine{};
    std::array<float, kDelayMask + 1> dryDelayLine{};
    int delayWriteIdx = 0;

    std::array<float, kChunkSize> rawChunk{};
    std::array<float, kChunkSize> alignedDryChunk{};
    std::array<float, kChunkSize> lowBuf{};
    std::array<float, kChunkSize> midBuf{};
    std::array<float, kChunkSize> highBuf{};
    std::array<float, kChunkSize> midOutBuf{};
    std::array<float, kChunkSize> highOutBuf{};

    juce::SmoothedValue<float> inputGainSmooth{ 1.0f };
    juce::SmoothedValue<float> outputGainSmooth{ 1.0f };
    juce::SmoothedValue<float> blendSmooth{ 1.0f };
    juce::SmoothedValue<float> bypassSmooth{ 0.0f };
    juce::SmoothedValue<float> lowLevelSmooth{ 1.0f };
    juce::SmoothedValue<float> midLevelSmooth{ 1.0f };
    juce::SmoothedValue<float> highLevelSmooth{ 1.0f };

    std::atomic<float> lowGrMeter{ 0.0f };
    std::array<std::atomic<float>, 3> bandPeakMeters{};
    std::atomic<float> inputPeakDb{ -96.0f };
    std::atomic<float> outputPeakDb{ -96.0f };
    SpectrumFifo spectrum;
    float appliedHighHpfHz = 1600.0f;
    double sampleRate = 48000.0;
};

} // namespace tonegrid::triDrive
