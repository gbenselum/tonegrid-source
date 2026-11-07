#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>

namespace tonegrid::multiband
{
/**
 * Lock-free audio-to-UI dual-channel (pre/post) sample ring buffer and FFT computer
 * for the interactive spectrum analyzer in TRI-COMP and TRI-DRIVE.
 *
 * Audio thread only calls push(preSample, postSample) which writes into a fixed
 * atomic ring buffer with zero locks or allocations.
 * UI thread calls computeSpectrum(preDbOut, postDbOut, sampleRate) at 30-60 Hz.
 */
class SpectrumFifo
{
public:
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize = 1 << kFftOrder; // 2048
    static constexpr int kNumBins = kFftSize / 2;   // 1024

    SpectrumFifo() noexcept
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const float phase = 2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(kFftSize - 1);
            hannWindow[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(phase));
        }
        smoothedPreDb.fill(-96.0f);
        smoothedPostDb.fill(-96.0f);
    }

    void reset() noexcept
    {
        writePos.store(0, std::memory_order_relaxed);
        preRing.fill(0.0f);
        postRing.fill(0.0f);
    }

    // Audio-thread safe: lock-free single-writer ring buffer write.
    void push(float preSample, float postSample) noexcept
    {
        const int idx = writePos.load(std::memory_order_relaxed);
        preRing[static_cast<size_t>(idx)] = std::isfinite(preSample) ? preSample : 0.0f;
        postRing[static_cast<size_t>(idx)] = std::isfinite(postSample) ? postSample : 0.0f;
        writePos.store((idx + 1) & (kFftSize - 1), std::memory_order_release);
    }

    void pushBlock(const float* preSamples, const float* postSamples, int numSamples) noexcept
    {
        if (preSamples == nullptr || postSamples == nullptr || numSamples <= 0)
            return;
        int idx = writePos.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const float pre = preSamples[i];
            const float post = postSamples[i];
            preRing[static_cast<size_t>(idx)] = std::isfinite(pre) ? pre : 0.0f;
            postRing[static_cast<size_t>(idx)] = std::isfinite(post) ? post : 0.0f;
            idx = (idx + 1) & (kFftSize - 1);
        }
        writePos.store(idx, std::memory_order_release);
    }

    // UI thread: copies the latest 2048 samples, applies Hann window + 4.5 dB/oct visual
    // tilt (so bass & treble look balanced), and updates smoothed dB bins.
    void computeSpectrum(std::array<float, kNumBins>& preDbOut,
                         std::array<float, kNumBins>& postDbOut) noexcept
    {
        const int start = writePos.load(std::memory_order_acquire);
        std::array<float, kFftSize * 2> preFftData{};
        std::array<float, kFftSize * 2> postFftData{};

        for (int i = 0; i < kFftSize; ++i)
        {
            const int ringIdx = (start + i) & (kFftSize - 1);
            const float w = hannWindow[static_cast<size_t>(i)];
            preFftData[static_cast<size_t>(i)] = preRing[static_cast<size_t>(ringIdx)] * w;
            postFftData[static_cast<size_t>(i)] = postRing[static_cast<size_t>(ringIdx)] * w;
        }

        fft.performFrequencyOnlyForwardTransform(preFftData.data());
        fft.performFrequencyOnlyForwardTransform(postFftData.data());

        constexpr float norm = 2.0f / static_cast<float>(kFftSize);
        for (size_t bin = 0; bin < static_cast<size_t>(kNumBins); ++bin)
        {
            const float preMag = preFftData[bin] * norm;
            const float postMag = postFftData[bin] * norm;
            // Gentle +3 dB/octave visual pink-noise tilt around bin 16 (~375 Hz)
            const float octavesFromRef = std::log2( static_cast<float>(juce::jmax<size_t>(1, bin)) / 16.0f );
            const float tiltDb = octavesFromRef * 3.0f;

            const float targetPreDb = juce::jlimit(-96.0f, 12.0f, juce::Decibels::gainToDecibels(preMag, -96.0f) + tiltDb);
            const float targetPostDb = juce::jlimit(-96.0f, 12.0f, juce::Decibels::gainToDecibels(postMag, -96.0f) + tiltDb);

            const float prevPre = smoothedPreDb[bin];
            const float prevPost = smoothedPostDb[bin];
            const float alphaPre = (targetPreDb > prevPre) ? 0.55f : 0.18f;
            const float alphaPost = (targetPostDb > prevPost) ? 0.55f : 0.18f;

            smoothedPreDb[bin] = prevPre + alphaPre * (targetPreDb - prevPre);
            smoothedPostDb[bin] = prevPost + alphaPost * (targetPostDb - prevPost);
            preDbOut[bin] = smoothedPreDb[bin];
            postDbOut[bin] = smoothedPostDb[bin];
        }
    }

private:
    juce::dsp::FFT fft{ kFftOrder };
    std::array<float, kFftSize> hannWindow{};
    std::array<float, kFftSize> preRing{};
    std::array<float, kFftSize> postRing{};
    std::array<float, kNumBins> smoothedPreDb{};
    std::array<float, kNumBins> smoothedPostDb{};
    std::atomic<int> writePos{ 0 };
};

} // namespace tonegrid::multiband
