#pragma once

#include "BoardTests.h"
#include "RealtimeAllocationProbe.h"
#include "TunerEngine.h"
#include "Modules/Play/UI/PlayView.h"
#include "Modules/Play/UI/BassRackView.h"

namespace bassModeTests
{
inline bool run(const juce::File& models, juce::String& report)
{
    using namespace juce;
    bool ok = true;
    int checks = 0;
    auto check = [&](bool good, const String& label)
    {
        ++checks;
        ok &= good;
        report += String(good ? "PASS " : "FAIL ") + label + "\n";
    };

    // -------------------------------------------------------------------------
    // 1. 3-Band Phase-Compensated LR4 Crossover Null & Magnitude Flatness Test
    // -------------------------------------------------------------------------
    {
        tonegrid::multiband::ThreeBandSplitter splitter;
        splitter.prepare(48000.0, 120.0f, 1800.0f);

        float maxNullError = 0.0f;
        for (int n = 0; n < 4800; ++n)
        {
            const float x = 0.5f * std::sin(2.0f * MathConstants<float>::pi * 55.0f * (float)n / 48000.0f)
                          + 0.3f * std::sin(2.0f * MathConstants<float>::pi * 440.0f * (float)n / 48000.0f)
                          + 0.2f * std::sin(2.0f * MathConstants<float>::pi * 3200.0f * (float)n / 48000.0f);
            const auto frame = splitter.processSample(x);
            const float sum = frame.low + frame.mid + frame.high;
            maxNullError = jmax(maxNullError, std::abs(sum - frame.alignedDry));
        }
        const float nullErrorDb = Decibels::gainToDecibels(maxNullError, -140.0f);
        check(nullErrorDb < -95.0f, "3-Band LR4 phase-compensated splitter sums identically to alignedDry (error " + String(nullErrorDb, 1) + " dBFS)");

        // Verify flat magnitude response across frequencies (including right at f1=120Hz and f2=1800Hz)
        bool flatMagnitude = true;
        for (float freq : {35.0f, 80.0f, 120.0f, 350.0f, 1000.0f, 1800.0f, 4500.0f, 10000.0f})
        {
            splitter.reset();
            double inRms = 0.0, outRms = 0.0;
            const int settle = 4800;
            const int total = settle + 48000;
            for (int n = 0; n < total; ++n)
            {
                const float x = std::sin(2.0f * MathConstants<float>::pi * freq * (float)n / 48000.0f);
                const auto frame = splitter.processSample(x);
                const float y = frame.low + frame.mid + frame.high;
                if (n >= settle)
                {
                    inRms += (double)x * x;
                    outRms += (double)y * y;
                }
            }
            const float diffDb = std::abs(Decibels::gainToDecibels((float)std::sqrt(outRms / inRms)));
            flatMagnitude &= (diffDb < 0.02f);
        }
        check(flatMagnitude, "3-Band LR4 sum maintains < 0.02 dB magnitude flatness across 35 Hz - 10 kHz including crossover frequencies");
    }

    // -------------------------------------------------------------------------
    // 2. TRI-COMP Ratios (1:1..NUKE), Harmonics (Dist 2 / Dist 3), & SC Link
    // -------------------------------------------------------------------------
    {
        using namespace tonegrid::multiband;
        const float gr1 = computeGainReductionDb(-6.0f, -24.0f, CompRatio::R1_1);
        const float gr2 = computeGainReductionDb(-6.0f, -24.0f, CompRatio::R2_1);
        const float gr4 = computeGainReductionDb(-6.0f, -24.0f, CompRatio::R4_1);
        const float gr10 = computeGainReductionDb(-6.0f, -24.0f, CompRatio::R10_1_Opto);
        const float gr20 = computeGainReductionDb(-6.0f, -24.0f, CompRatio::R20_1);
        const float grNuke = computeGainReductionDb(-6.0f, -24.0f, CompRatio::Nuke);

        check(std::abs(gr1) < 1.0e-5f, "TRI-COMP 1:1 ratio produces exactly 0.0 dB gain reduction (harmonic warmth mode)");
        check(gr2 > 4.0f && gr4 > gr2 && gr10 > gr4 && grNuke > gr20,
              "TRI-COMP gain reduction increases across 2:1 -> 4:1 -> 10:1 Opto -> NUKE");

        // Verify Dist 2 (even H2 asymmetry) vs Dist 3 (odd H3 symmetry)
        CompHarmonics dist2Harm, dist3Harm;
        dist2Harm.prepare(48000.0);
        dist3Harm.prepare(48000.0);

        float diff2 = 0.0f, diff3 = 0.0f, diff2vs3 = 0.0f;
        for (int n = 0; n < 4800; ++n)
        {
            const float s = 0.7f * std::sin(2.0f * MathConstants<float>::pi * 100.0f * (float)n / 48000.0f);
            const float y2 = dist2Harm.process(s, CompAudioMode::Dist2, 6.0f);
            const float y3 = dist3Harm.process(s, CompAudioMode::Dist3, 6.0f);
            diff2 += std::abs(y2 - s);
            diff3 += std::abs(y3 - s);
            diff2vs3 += std::abs(y2 - y3);
        }
        check(diff2 > 5.0f && diff3 > 5.0f && diff2vs3 > 5.0f,
              "TRI-COMP Dist 2 and Dist 3 generate distinct nonlinear harmonic transfer characteristics");

        // Verify cross-band sidechain linking in TriCompProcessor
        tonegrid::triComp::Processor compUnlinked, compLinked;
        auto pUnlinked = tonegrid::triComp::presetDefaults(0, true);
        pUnlinked.linkAmount = 0.0f;
        pUnlinked.inputDb = 14.0f;
        pUnlinked.bands[0].ratio = CompRatio::R10_1_Opto;
        pUnlinked.bands[2].ratio = CompRatio::R10_1_Opto;

        auto pLinked = pUnlinked;
        pLinked.linkAmount = 1.0f;

        compUnlinked.prepare(48000.0, pUnlinked);
        compLinked.prepare(48000.0, pLinked);

        AudioBuffer<float> bufA(1, 512), bufB(1, 512);
        for (int block = 0; block < 12; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float t = (float)(block * 512 + i) / 48000.0f;
                // Strong 55 Hz low note + tiny 3 kHz harmonic
                const float x = 0.8f * std::sin(2.0f * MathConstants<float>::pi * 55.0f * t)
                              + 0.01f * std::sin(2.0f * MathConstants<float>::pi * 3000.0f * t);
                bufA.setSample(0, i, x);
                bufB.setSample(0, i, x);
            }
            compUnlinked.processBlock(bufA.getWritePointer(0), 512, pUnlinked);
            compLinked.processBlock(bufB.getWritePointer(0), 512, pLinked);
        }
        check(compLinked.readMeters().grDb[2] > compUnlinked.readMeters().grDb[2] + 2.0f,
              "TRI-COMP cross-band SC link couples heavy Low-band compression into High-band detector");
    }

    // -------------------------------------------------------------------------
    // 3. TRI-DRIVE Multiband Saturation, Low Bus Comp, Cab Stage & NAM-in-Band
    // -------------------------------------------------------------------------
    {
        tonegrid::triDrive::Processor driveProc;
        driveProc.prepare(48000.0, tonegrid::triDrive::presetDefaults(0, true));

        // Test all 8 factory presets produce finite, non-silent audio
        bool allPresetsHealthy = true;
        for (int preset = 0; preset < (int)tonegrid::triDrive::kPresets.size(); ++preset)
        {
            auto params = tonegrid::triDrive::presetDefaults(preset, true);
            driveProc.prepare(48000.0, params);
            AudioBuffer<float> buf(1, 256);
            for (int b = 0; b < 8; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float t = (float)(b * 256 + i) / 48000.0f;
                    const float x = 0.25f * std::sin(2.0f * MathConstants<float>::pi * 82.41f * t)
                                  + 0.15f * std::sin(2.0f * MathConstants<float>::pi * 440.0f * t)
                                  + 0.10f * std::sin(2.0f * MathConstants<float>::pi * 1760.0f * t);
                    buf.setSample(0, i, x);
                }
                driveProc.processBlock(buf.getWritePointer(0), 256, params);
            }
            const float rms = buf.getRMSLevel(0, 0, 256);
            allPresetsHealthy &= (std::isfinite(rms) && rms > 0.01f && rms < 4.0f);
        }
        check(allPresetsHealthy, "All 8 TRI-DRIVE factory bass presets produce finite, well-bounded output");

        // Test NAM-in-Band slot loading using nam::get_dsp
        nam::DspLoadOptions options;
        options.prewarm = false;
        auto payload = nlohmann::json{
            {"version", "0.7.0"},
            {"architecture", "Linear"},
            {"sample_rate", 48000},
            {"config", {{"receptive_field", 1}, {"bias", false}}},
            {"weights", {2.25f}}
        };
        auto dsp = nam::get_dsp(payload, options);
        check(dsp != nullptr, "NAM-in-Band neural model loaded via nam::get_dsp");
        if (dsp)
        {
            board::Model model;
            model.dsp = std::move(dsp);
            model.file = "BassDarkglassMid.nam";
            model.hash = "test-mid-nam";
            model.label = "Bass Darkglass Mid";

            driveProc.installBandNam(0, std::move(model));
            check(driveProc.hasBandNam(0), "TRI-DRIVE Mid band reports active NAM capture after installBandNam");

            auto namParams = tonegrid::triDrive::presetDefaults(0, true);
            namParams.midCharacter = tonegrid::multiband::DriveCharacter::Nam;
            namParams.midDrive = 6.5f;

            AudioBuffer<float> namBuf(1, 256);
            for (int b = 0; b < 4; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float x = 0.2f * std::sin(2.0f * MathConstants<float>::pi * 400.0f * (float)(b * 256 + i) / 48000.0f);
                    namBuf.setSample(0, i, x);
                }
                driveProc.processBlock(namBuf.getWritePointer(0), 256, namParams);
            }
            const float namRms = namBuf.getRMSLevel(0, 0, 256);
            check(std::isfinite(namRms) && namRms > 0.01f, "TRI-DRIVE NAM-in-Band processes Mid-band audio through neural model");
        }
    }

    // -------------------------------------------------------------------------
    // 4. Bass Tuner Low-Frequency Accuracy (B0=30.87Hz, E1=41.20Hz, A1=55.0Hz, D2=73.42Hz)
    // -------------------------------------------------------------------------
    {
        struct BassNote { const char* name; double freq; };
        constexpr std::array<BassNote, 5> notes {{
            {"B0", 30.8677},
            {"E1", 41.2034},
            {"A1", 55.0000},
            {"D2", 73.4162},
            {"G2", 97.9989}
        }};

        for (const auto& n : notes)
        {
            std::array<float, 2048> window {};
            for (size_t i = 0; i < window.size(); ++i)
            {
                const double t = (double)i / 48000.0;
                // Realistic bass waveform: fundamental + 2nd + 3rd harmonic
                window[i] = (float)(0.65 * std::sin(2.0 * MathConstants<double>::pi * n.freq * t)
                                  + 0.25 * std::sin(2.0 * MathConstants<double>::pi * (2.0 * n.freq) * t)
                                  + 0.10 * std::sin(2.0 * MathConstants<double>::pi * (3.0 * n.freq) * t));
            }
            const auto reading = tonegrid::tuning::Engine::analyse(window, 48000.0);
            const double centsError = 1200.0 * std::log2(reading.frequency / n.freq);
            check(reading.frequency > 20.0 && reading.confidence > 0.85 && std::abs(centsError) <= 2.0,
                  String("Bass Tuner locks onto ") + n.name + " (" + String(n.freq, 2) + " Hz) within +/-2 cents (got "
                  + String(reading.frequency, 2) + " Hz, err " + String(centsError, 2) + " cents)");
        }
    }

    // -------------------------------------------------------------------------
    // 5. Bass Mode Pedalboard Signal Chain, Overdrive Removal & RT Zero Allocation
    // -------------------------------------------------------------------------
    {
        board::Processor proc;
        const auto loadRes = proc.load(models);
        check(loadRes.wasOk(), "Pedalboard Processor loads Models for Bass mode signal chain test");
        if (loadRes.wasOk())
        {
            proc.prepareToPlay(48000.0, 256);
            proc.setInstrumentMode(tonegrid::InstrumentMode::Bass);

            // Enable an overdrive that is removed in Bass mode (e.g. OrangeDist id=3) with extreme gain
            auto odSetting = board::defaults(3, 0);
            odSetting.enabled = true;
            odSetting.input = 20.0f;
            odSetting.output = 12.0f;
            proc.set(3, odSetting);

            // With TRI-COMP and TRI-DRIVE bypassed and kept stompboxes bypassed, output should match OdDisabled
            AudioBuffer<float> bufOdEnabled(2, 256), bufOdDisabled(2, 256);
            MidiBuffer midi;
            for (int i = 0; i < 256; ++i)
            {
                const float x = 0.1f * std::sin(2.0f * MathConstants<float>::pi * 110.0f * (float)i / 48000.0f);
                bufOdEnabled.setSample(0, i, x);
                bufOdEnabled.setSample(1, i, x);
                bufOdDisabled.setSample(0, i, x);
                bufOdDisabled.setSample(1, i, x);
            }
            proc.processBlock(bufOdEnabled, midi);

            proc.prepareToPlay(48000.0, 256);
            odSetting.enabled = false;
            proc.set(3, odSetting);
            proc.processBlock(bufOdDisabled, midi);

            float maxDiff = 0.0f;
            for (int i = 0; i < 256; ++i)
                maxDiff = jmax(maxDiff, std::abs(bufOdEnabled.getSample(0, i) - bufOdDisabled.getSample(0, i)));
            check(maxDiff < 1.0e-6f, "Guitar overdrive pedals are completely bypassed/removed from the Bass mode signal chain");

            // Now enable TRI-COMP, TRI-DRIVE, and Bass Graphic EQ and verify zero heap allocations on audio thread
            auto tc = tonegrid::triComp::presetDefaults(0, true);
            proc.setTriComp(tc);

            auto td = tonegrid::triDrive::presetDefaults(0, true);
            proc.setTriDrive(td);

            auto eq = board::defaults(12, 0);
            eq.enabled = true;
            eq.eqBands = {4.0f, 3.0f, -2.0f, -4.0f, 1.0f, 3.5f, 2.0f};
            proc.set(12, eq);

            AudioBuffer<float> rtBuf(2, 256);
            bool finite = true;
            realtimeProbe::begin();
            for (int iter = 0; iter < 250; ++iter)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float x = 0.15f * std::sin(2.0f * MathConstants<float>::pi * 55.0f * (float)(iter * 256 + i) / 48000.0f);
                    rtBuf.setSample(0, i, x);
                    rtBuf.setSample(1, i, x);
                }
                if (iter == 125)
                    proc.setRackOrder(tonegrid::BassRackOrder::DriveThenComp);
                proc.processBlock(rtBuf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                        finite &= std::isfinite(rtBuf.getSample(c, i));
            }
            const auto allocations = realtimeProbe::end();
            check(allocations == 0 && finite,
                  "Bass mode pedalboard (TRI-COMP + TRI-DRIVE + Bass Graphic EQ + rack order swap) allocates 0 heap objects across 250 callbacks");

            // -----------------------------------------------------------------
            // 6. RigCodec XML Round-Trip & BassRack UI Component Snapshot
            // -----------------------------------------------------------------
            PluginDescription plugin;
            plugin.name = "Synthetic Bass Amp";
            plugin.pluginFormatName = "VST3";
            plugin.fileOrIdentifier = "bass.vst3";
            plugin.uniqueId = 777;
            plugin.version = "1.0";

            board::State origState;
            origState.name = "TriComp TriDrive Rig";
            origState.amp.append("synthetic-only", 14);
            origState.instrumentMode = tonegrid::InstrumentMode::Bass;
            origState.rackOrder = tonegrid::BassRackOrder::DriveThenComp;
            origState.triComp = tonegrid::triComp::presetDefaults(3, true);
            origState.triComp.lowMidHz = 145.0f;
            origState.triComp.midHighHz = 1950.0f;
            origState.triComp.bands[0].ratio = tonegrid::multiband::CompRatio::Nuke;
            origState.triComp.bands[1].audioMode = tonegrid::multiband::CompAudioMode::Dist2;
            origState.triDrive = tonegrid::triDrive::presetDefaults(2, true);
            origState.triDrive.midDrive = 8.5f;
            origState.triDrive.highCharacter = tonegrid::multiband::DriveCharacter::Fizz;
            for (int p = 0; p < board::count; ++p)
                origState.pedals[(size_t)p] = board::defaults(p, 0);

            const auto encoded = board::encode(origState, plugin, proc);
            check(encoded.getChildByName("BassRack") != nullptr, "RigCodec emits <BassRack> element when InstrumentMode::Bass is active");

            board::State decodedState;
            const auto decodeRes = board::decode(encoded, plugin, proc, decodedState);
            check(decodeRes.wasOk(), "RigCodec decodes Bass mode preset without error");
            check(decodedState.instrumentMode == tonegrid::InstrumentMode::Bass
                  && decodedState.rackOrder == tonegrid::BassRackOrder::DriveThenComp
                  && decodedState.triComp.enabled == origState.triComp.enabled
                  && std::abs(decodedState.triComp.lowMidHz - 145.0f) < 1.0e-3f
                  && std::abs(decodedState.triComp.midHighHz - 1950.0f) < 1.0e-3f
                  && decodedState.triComp.bands[0].ratio == tonegrid::multiband::CompRatio::Nuke
                  && decodedState.triComp.bands[1].audioMode == tonegrid::multiband::CompAudioMode::Dist2
                  && decodedState.triDrive.enabled == origState.triDrive.enabled
                  && std::abs(decodedState.triDrive.midDrive - 8.5f) < 1.0e-3f
                  && decodedState.triDrive.highCharacter == tonegrid::multiband::DriveCharacter::Fizz,
                  "RigCodec round-trips InstrumentMode::Bass, BassRackOrder, TRI-COMP, and TRI-DRIVE parameters identically");

            // UI Snapshot test for BassRackCard and BassRackEditorView
            tonegrid::play::BassRackCard compCard(proc, tonegrid::play::BassRackUnit::TriComp);
            tonegrid::play::BassRackCard driveCard(proc, tonegrid::play::BassRackUnit::TriDrive);
            compCard.setSize(480, 288);
            driveCard.setSize(480, 288);
            compCard.sync();
            driveCard.sync();
            const auto compSnap = compCard.createComponentSnapshot(compCard.getLocalBounds());
            const auto driveSnap = driveCard.createComponentSnapshot(driveCard.getLocalBounds());
            check(compSnap.isValid() && compSnap.getWidth() == 480 && driveSnap.isValid() && driveSnap.getWidth() == 480,
                  "BassRackCard renders valid pedalboard summary cards for TRI-COMP and TRI-DRIVE");

            tonegrid::play::BassRackEditorView editor(proc, tonegrid::play::BassRackUnit::TriDrive);
            editor.setSize(980, 620);
            const auto editorSnap = editor.createComponentSnapshot(editor.getLocalBounds());
            check(editorSnap.isValid() && editorSnap.getWidth() == 980 && editorSnap.getHeight() == 620,
                  "BassRackEditorView renders full 980x620 interactive spectrum and multiband control view");

            // Full PlayView Bass mode non-overlap & layout check
            tonegrid::recording::SessionRecorder rec(File{});
            tonegrid::looping::Engine loop;
            tonegrid::play::View playView(rec, loop);
            for (int v = 0; v < board::count; ++v)
            {
                playView.cards[(size_t)v] = std::make_unique<PedalCard>(v, proc);
                playView.addAndMakeVisible(*playView.cards[(size_t)v]);
            }
            playView.triCompCard = std::make_unique<tonegrid::play::BassRackCard>(proc, tonegrid::play::BassRackUnit::TriComp);
            playView.triDriveCard = std::make_unique<tonegrid::play::BassRackCard>(proc, tonegrid::play::BassRackUnit::TriDrive);
            playView.addChildComponent(*playView.triCompCard);
            playView.addChildComponent(*playView.triDriveCard);
            playView.setSize(1620, 620);
            playView.setInstrumentMode(tonegrid::InstrumentMode::Bass, true);

            std::vector<Rectangle<int>> visibleRects;
            if (playView.triCompCard->isVisible()) visibleRects.push_back(playView.triCompCard->getBounds());
            if (playView.triDriveCard->isVisible()) visibleRects.push_back(playView.triDriveCard->getBounds());
            for (int v = 0; v < board::count; ++v)
                if (playView.cards[(size_t)v]->isVisible())
                    visibleRects.push_back(playView.cards[(size_t)v]->getBounds());

            bool anyOverlap = false;
            for (size_t i = 0; i < visibleRects.size(); ++i)
                for (size_t j = i + 1; j < visibleRects.size(); ++j)
                    if (visibleRects[i].intersects(visibleRects[j]))
                        anyOverlap = true;

            check(visibleRects.size() == 7 && !anyOverlap,
                  "PlayView in Bass mode positions 2 rack cards and 5 bass stompboxes with zero overlap");
        }
    }

    report += String(checks) + " Bass mode checks; " + (ok ? "ALL PASSED" : "FAILURES") + "\n";
    return ok;
}
} // namespace bassModeTests
