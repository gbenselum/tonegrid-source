#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace tonegrid::multiband
{
// First-order DC blocker (~8 Hz at 48 kHz) to remove DC offset from asymmetric waveshapers.
struct DcBlocker
{
    double pole = 0.99895;
    double x1 = 0.0, y1 = 0.0;

    void prepare(double sampleRate, double cutoffHz = 8.0) noexcept
    {
        pole = std::exp(-2.0 * juce::MathConstants<double>::pi * cutoffHz / (sampleRate > 0.0 ? sampleRate : 48000.0));
        reset();
    }
    void reset() noexcept { x1 = y1 = 0.0; }

    float process(float x) noexcept
    {
        const double y = static_cast<double>(x) - x1 + pole * y1;
        x1 = x;
        y1 = y;
        return static_cast<float>(y);
    }
};

// First-order one-pole lowpass/highpass utility for pre/de-emphasis inside drive stages.
struct OnePoleFilter
{
    double a = 0.1;
    double z = 0.0;

    void prepare(double sampleRate, double cutoffHz) noexcept
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double fc = juce::jlimit(10.0, sr * 0.48, cutoffHz);
        a = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * fc / sr);
        reset();
    }
    void reset() noexcept { z = 0.0; }

    float processLowpass(float x) noexcept
    {
        z += a * (static_cast<double>(x) - z);
        return static_cast<float>(z);
    }
    float processHighpass(float x) noexcept
    {
        return x - processLowpass(x);
    }
};

enum class CompAudioMode : int
{
    Clean = 0,
    Dist2 = 1,
    Dist3 = 2
};

/**
 * TRI-COMP harmonic saturation stage (Clean / Dist 2 / Dist 3).
 *
 * - Dist 2 emphasizes 2nd-order (even) tube-like harmonics, scaling with compression.
 * - Dist 3 emphasizes 3rd-order (odd) tape-like harmonics, flattening waveform peaks.
 */
class CompHarmonics
{
public:
    void prepare(double sampleRate) noexcept
    {
        dc.prepare(sampleRate, 10.0);
        audioHpf1.prepare(sampleRate, 80.0);
        audioHpf2.prepare(sampleRate, 80.0);
    }

    void reset() noexcept
    {
        dc.reset();
        audioHpf1.reset();
        audioHpf2.reset();
    }

    float process(float x, CompAudioMode mode, float currentGrDb, bool audioHighPass = false, float* harmonicMeterOut = nullptr) noexcept
    {
        if (audioHighPass)
            x = audioHpf2.processHighpass(audioHpf1.processHighpass(x));

        if (mode == CompAudioMode::Clean)
        {
            if (harmonicMeterOut) *harmonicMeterOut = 0.0f;
            return x;
        }

        // Harmonic intensity increases with gain reduction (0..18 dB) plus a base warmth floor.
        const float grFactor = juce::jlimit(0.0f, 1.5f, currentGrDb * 0.08f);
        if (mode == CompAudioMode::Dist2)
        {
            // Asymmetric transfer curve: y = x + k2 * x^2 followed by soft ceiling and DC blocker.
            // Produces dominant 2nd harmonic (even order) with minimal odd harmonics.
            const float k2 = 0.22f + 0.38f * grFactor;
            const float shaped = x + k2 * x * x;
            const float soft = std::tanh(shaped * 1.15f) / 1.15f;
            const float out = dc.process(soft);
            if (harmonicMeterOut) *harmonicMeterOut = juce::jmin(1.0f, std::abs(out - x) * 4.5f);
            return out;
        }

        // Dist 3: odd-order symmetric cubic/tanh tape-style saturation (dominant 3rd harmonic).
        const float drive = 1.35f + 1.45f * grFactor;
        const float u = x * drive;
        const float cubic = u + 0.28f * u * u * u;
        const float out = std::tanh(cubic) / std::tanh(drive);
        if (harmonicMeterOut) *harmonicMeterOut = juce::jmin(1.0f, std::abs(out - x) * 3.5f);
        return out;
    }

private:
    DcBlocker dc;
    OnePoleFilter audioHpf1, audioHpf2;
};

enum class DriveCharacter : int
{
    Tube = 0,   // Warm asymmetric triode saturation
    Grind = 1,  // Modern bass op-amp/diode grind
    Fuzz = 2,   // Thick saturated bass fuzz
    Fizz = 3,   // High-band harmonic edge & clank exciter
    Nam = 4     // External/Factory NAM capture slot
};

/**
 * 4x oversampled nonlinear drive processor for a single band (Mid or High) in TRI-DRIVE.
 * Uses JUCE's polyphase IIR half-band oversampler (zero allocation after prepare()).
 */
class OversampledBandDrive
{
public:
    static constexpr size_t kMaxBlockSize = 256;

    void prepare(double baseSampleRate, size_t maxBlock = kMaxBlockSize) noexcept
    {
        sampleRate = baseSampleRate > 0.0 ? baseSampleRate : 48000.0;
        oversampler.reset();
        oversampler.initProcessing(juce::jmax(size_t(128), maxBlock));
        const double osRate = sampleRate * 4.0;
        dc.prepare(osRate, 12.0);
        preHpf.prepare(osRate, 140.0);
        postLpf.prepare(osRate, 6800.0);
        biasFollower.prepare(osRate, 18.0);
        driveSmooth.reset(osRate, 0.008);
        driveSmooth.setCurrentAndTargetValue(1.0f);
    }

    void reset() noexcept
    {
        oversampler.reset();
        dc.reset();
        preHpf.reset();
        postLpf.reset();
        biasFollower.reset();
    }

    int latencySamples() const noexcept
    {
        return static_cast<int>(std::llround(oversampler.getLatencyInSamples()));
    }

    void processBlock(const float* input, float* output, int numSamples, float driveNorm, DriveCharacter character) noexcept
    {
        jassert(numSamples <= static_cast<int>(kMaxBlockSize));
        const float clampedDrive = juce::jlimit(0.0f, 1.0f, driveNorm);
        // Even at minimum drive (0.0), the drive stage retains a subtle trace of console/tube grit.
        const float targetGain = juce::Decibels::decibelsToGain(-2.0f + clampedDrive * 34.0f);
        driveSmooth.setTargetValue(targetGain);

        std::copy_n(input, numSamples, scratch.begin());
        float* channelPtr = scratch.data();
        juce::dsp::AudioBlock<float> baseBlock(&channelPtr, 1, static_cast<size_t>(numSamples));
        auto osBlock = oversampler.processSamplesUp(baseBlock);
        auto* osSamples = osBlock.getChannelPointer(0);
        const size_t osLength = osBlock.getNumSamples();

        for (size_t i = 0; i < osLength; ++i)
        {
            const float g = driveSmooth.getNextValue();
            osSamples[i] = shapeSample(osSamples[i], g, character);
        }

        oversampler.processSamplesDown(baseBlock);
        std::copy_n(scratch.begin(), numSamples, output);
    }

private:
    float shapeSample(float x, float gain, DriveCharacter character) noexcept
    {
        switch (character)
        {
            case DriveCharacter::Tube:
            {
                // Asymmetric triode with dynamic grid-bias shift + DC block + smooth HF rolloff.
                const float env = biasFollower.processLowpass(std::abs(x));
                const float u = x * gain + 0.14f * (1.0f + env);
                const float y = (u >= 0.0f) ? std::tanh(u) : (std::tanh(u * 0.82f) / 0.82f);
                return postLpf.processLowpass(dc.process(y * 0.72f));
            }
            case DriveCharacter::Grind:
            {
                // Pre-emphasis tightens low-mids before hard-knee op-amp/CMOS clipping.
                const float tight = 0.35f * x + 0.65f * preHpf.processHighpass(x);
                const float u = tight * gain * 1.35f;
                const float hard = u / std::sqrt(1.0f + u * u);
                const float folded = std::tanh(hard * 1.4f);
                return postLpf.processLowpass(dc.process(folded * 0.68f));
            }
            case DriveCharacter::Fuzz:
            {
                // Cascaded high-gain bipolar fuzz with subtle bias gate.
                const float g1 = juce::jmax(1.5f, gain * 1.8f);
                const float stage1 = std::tanh(x * g1 + 0.18f);
                const float stage2 = std::tanh(dc.process(stage1) * (1.2f + g1 * 0.35f));
                return postLpf.processLowpass(stage2 * 0.60f);
            }
            case DriveCharacter::Fizz:
            default:
            {
                // Crisp odd+even harmonic exciter for the high band (clank & pick attack).
                const float bright = preHpf.processHighpass(x) * gain * 1.2f;
                const float y = std::tanh(bright + 0.12f * bright * bright);
                return dc.process(y * 0.68f);
            }
        }
    }

    juce::dsp::Oversampling<float> oversampler{ 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false };
    std::array<float, kMaxBlockSize> scratch{};
    DcBlocker dc;
    OnePoleFilter preHpf, postLpf, biasFollower;
    juce::SmoothedValue<float> driveSmooth{ 1.0f };
    double sampleRate = 48000.0;
};

} // namespace tonegrid::multiband
