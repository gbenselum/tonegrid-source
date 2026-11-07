#pragma once
#include "Effects/Pedalboard/PedalBoard.h"
#include "Shared/UI/UiStyle.h"
#include "Shared/UI/PopupWindow.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <functional>

namespace tonegrid::play
{
enum class BassRackUnit : int
{
    TriComp = 0,
    TriDrive = 1
};

namespace detail
{
inline constexpr float kMinFreqHz = 20.0f;
inline constexpr float kMaxFreqHz = 16000.0f;

inline float freqToNormX(float hz) noexcept
{
    const float clamped = juce::jlimit(kMinFreqHz, kMaxFreqHz, hz);
    return std::log(clamped / kMinFreqHz) / std::log(kMaxFreqHz / kMinFreqHz);
}

inline float normXToFreq(float norm) noexcept
{
    const float clamped = juce::jlimit(0.0f, 1.0f, norm);
    return kMinFreqHz * std::pow(kMaxFreqHz / kMinFreqHz, clamped);
}

inline juce::Colour bandColour(int bandIndex) noexcept
{
    switch (bandIndex)
    {
        case 0:  return juce::Colour(0xff29b6f6); // Sub / Low: Deep Cyan
        case 1:  return juce::Colour(0xffffa726); // Mid: Warm Amber
        default: return juce::Colour(0xffef5350); // High: Vibrant Coral
    }
}

inline juce::String formatHz(float hz)
{
    if (hz >= 1000.0f)
        return juce::String(hz * 0.001f, 2) + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}
} // namespace detail

/**
 * Interactive real-time FFT Spectrum + 3-Band Crossover & Level Handle Canvas
 * inspired by Neural DSP Parallax X.
 *
 * Users can:
 * - Drag the vertical Low/Mid (f1) and Mid/High (f2) crossover lines horizontally
 * - Drag the horizontal band gain handles (Low, Mid, High) vertically (-24..+24 dB)
 * - Click a band region to select/focus that band
 * - View real-time Pre (dim outline) and Post (filled gradient) FFT spectrums
 * - View per-band Gain Reduction (downward cyan/amber/coral bars) and Harmonic saturation
 */
class InteractiveSpectrumCanvas final : public juce::Component, private juce::Timer
{
public:
    InteractiveSpectrumCanvas(board::Processor& proc, BassRackUnit unitType)
        : processor(proc), unit(unitType)
    {
        setOpaque(false);
        startTimerHz(30);
    }

    ~InteractiveSpectrumCanvas() override { stopTimer(); }

    std::function<void()> onParametersChanged;
    std::function<void(int)> onBandSelected;

    int selectedBand() const noexcept { return activeBand; }
    void setSelectedBand(int b) noexcept
    {
        activeBand = juce::jlimit(0, 2, b);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff070e13));
        g.fillRoundedRectangle(bounds, 8.0f);

        const auto plot = bounds.reduced(38.0f, 22.0f);
        if (plot.getWidth() <= 10.0f || plot.getHeight() <= 10.0f)
            return;

        float f1 = 150.0f, f2 = 1600.0f;
        std::array<float, 3> bandLevelsDb{};
        std::array<bool, 3> solo{}, mute{};
        std::array<float, 3> grDb{};

        if (unit == BassRackUnit::TriComp)
        {
            const auto p = processor.getTriComp();
            const auto m = processor.triCompProcessor().readMeters();
            f1 = p.lowMidHz;
            f2 = p.midHighHz;
            for (size_t b = 0; b < 3; ++b)
            {
                bandLevelsDb[b] = p.bands[b].levelDb;
                solo[b] = p.bands[b].solo;
                mute[b] = p.bands[b].mute;
                grDb[b] = m.grDb[b];
            }
        }
        else
        {
            const auto p = processor.getTriDrive();
            const auto m = processor.triDriveProcessor().readMeters();
            f1 = p.lowMidHz;
            f2 = p.midHighHz;
            bandLevelsDb = { p.lowLevelDb, p.midLevelDb, p.highLevelDb };
            solo = { p.lowSolo, p.midSolo, p.highSolo };
            mute = { p.lowMute, p.midMute, p.highMute };
            grDb = { m.lowCompGrDb, 0.0f, 0.0f };
        }

        const float x1 = plot.getX() + detail::freqToNormX(f1) * plot.getWidth();
        const float x2 = plot.getX() + detail::freqToNormX(f2) * plot.getWidth();
        const std::array<float, 4> bandEdges{{ plot.getX(), x1, x2, plot.getRight() }};

        // 1. Shaded band regions
        const bool anySolo = solo[0] || solo[1] || solo[2];
        for (int b = 0; b < 3; ++b)
        {
            const bool active = anySolo ? solo[static_cast<size_t>(b)] : !mute[static_cast<size_t>(b)];
            const auto col = detail::bandColour(b);
            juce::Rectangle<float> bandRect(bandEdges[static_cast<size_t>(b)],
                                            plot.getY(),
                                            bandEdges[static_cast<size_t>(b + 1)] - bandEdges[static_cast<size_t>(b)],
                                            plot.getHeight());
            const float fillAlpha = active ? (b == activeBand ? 0.14f : 0.07f) : 0.02f;
            g.setColour(col.withAlpha(fillAlpha));
            g.fillRect(bandRect);

            // Downward Gain Reduction bar at the top of each band
            const float gr = juce::jlimit(0.0f, 24.0f, grDb[static_cast<size_t>(b)]);
            if (gr > 0.1f)
            {
                const float grHeight = (gr / 24.0f) * (plot.getHeight() * 0.45f);
                juce::Rectangle<float> grBar(bandRect.getRight() - 16.0f, plot.getY() + 4.0f, 10.0f, grHeight);
                g.setColour(col.withAlpha(0.80f));
                g.fillRoundedRectangle(grBar, 3.0f);
            }
        }

        // 2. Frequency & dB grid lines
        g.setFont(ui::font(10.5f));
        for (float hz : { 30.0f, 60.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f })
        {
            const float gx = plot.getX() + detail::freqToNormX(hz) * plot.getWidth();
            g.setColour(juce::Colour(0xff192833));
            g.drawVerticalLine(juce::roundToInt(gx), plot.getY(), plot.getBottom());
            g.setColour(juce::Colour(0xff6b8291));
            const juce::String lbl = hz >= 1000.0f ? juce::String(juce::roundToInt(hz / 1000.0f)) + "k" : juce::String(juce::roundToInt(hz));
            g.drawText(lbl, juce::roundToInt(gx) - 18, juce::roundToInt(plot.getBottom()) + 2, 36, 16, juce::Justification::centred);
        }

        for (float db : { 12.0f, 0.0f, -12.0f, -24.0f })
        {
            const float gy = levelDbToY(db, plot);
            g.setColour(db == 0.0f ? juce::Colour(0xff2c4251) : juce::Colour(0xff15222b));
            g.drawHorizontalLine(juce::roundToInt(gy), plot.getX(), plot.getRight());
            g.setColour(juce::Colour(0xff6b8291));
            g.drawText((db > 0.0f ? "+" : "") + juce::String(juce::roundToInt(db)),
                       2, juce::roundToInt(gy) - 7, 32, 14, juce::Justification::centredRight);
        }

        // 3. Pre and Post FFT Spectrum curves
        drawSpectrumPath(g, plot, preDb, juce::Colour(0xff607d8b).withAlpha(0.45f), false);
        drawSpectrumPath(g, plot, postDb, unit == BassRackUnit::TriComp ? juce::Colour(0xff29b6f6) : juce::Colour(0xffff8a3d), true);

        // 4. Band Level horizontal handles (-24..+24 dB)
        for (int b = 0; b < 3; ++b)
        {
            const float bx0 = bandEdges[static_cast<size_t>(b)];
            const float bx1 = bandEdges[static_cast<size_t>(b + 1)];
            const float by = levelDbToY(bandLevelsDb[static_cast<size_t>(b)], plot);
            const auto col = detail::bandColour(b);

            g.setColour(col.withAlpha(0.85f));
            g.drawLine(bx0 + 4.0f, by, bx1 - 4.0f, by, 2.2f);

            const float cx = 0.5f * (bx0 + bx1);
            juce::Rectangle<float> node(cx - 32.0f, by - 10.0f, 64.0f, 20.0f);
            g.setColour(juce::Colour(0xff0b161e).withAlpha(0.92f));
            g.fillRoundedRectangle(node, 10.0f);
            g.setColour(col);
            g.drawRoundedRectangle(node, 10.0f, 1.5f);
            g.setFont(ui::font(11.0f, true));
            const char* bName = (b == 0 ? "LOW " : (b == 1 ? "MID " : "HIGH "));
            const float val = bandLevelsDb[static_cast<size_t>(b)];
            g.drawText(juce::String(bName) + (val >= 0.0f ? "+" : "") + juce::String(val, 1),
                       node.toNearestInt(), juce::Justification::centred);
        }

        // 5. Draggable Crossover vertical lines (f1 & f2)
        for (int c = 0; c < 2; ++c)
        {
            const float cx = (c == 0) ? x1 : x2;
            const float hz = (c == 0) ? f1 : f2;
            const bool hoveredOrDragged = (dragTarget == (c == 0 ? DragTarget::Crossover1 : DragTarget::Crossover2));
            g.setColour(hoveredOrDragged ? juce::Colours::white : juce::Colour(0xffd0e2ee));
            g.drawLine(cx, plot.getY(), cx, plot.getBottom(), hoveredOrDragged ? 2.6f : 1.8f);

            juce::Rectangle<float> pill(cx - 34.0f, plot.getY() + 6.0f, 68.0f, 19.0f);
            g.setColour(juce::Colour(0xff10202b).withAlpha(0.95f));
            g.fillRoundedRectangle(pill, 9.0f);
            g.setColour(hoveredOrDragged ? juce::Colours::white : juce::Colour(0xff64b5f6));
            g.drawRoundedRectangle(pill, 9.0f, 1.3f);
            g.setFont(ui::font(11.0f, true));
            g.setColour(juce::Colours::white);
            g.drawText(detail::formatHz(hz), pill.toNearestInt(), juce::Justification::centred);
        }

        g.setColour(juce::Colour(0xff253946));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto plot = getLocalBounds().toFloat().reduced(38.0f, 22.0f);
        if (plot.getWidth() <= 10.0f)
            return;

        float f1 = 150.0f, f2 = 1600.0f;
        std::array<float, 3> levels{};
        if (unit == BassRackUnit::TriComp)
        {
            const auto p = processor.getTriComp();
            f1 = p.lowMidHz; f2 = p.midHighHz;
            levels = { p.bands[0].levelDb, p.bands[1].levelDb, p.bands[2].levelDb };
        }
        else
        {
            const auto p = processor.getTriDrive();
            f1 = p.lowMidHz; f2 = p.midHighHz;
            levels = { p.lowLevelDb, p.midLevelDb, p.highLevelDb };
        }

        const float x1 = plot.getX() + detail::freqToNormX(f1) * plot.getWidth();
        const float x2 = plot.getX() + detail::freqToNormX(f2) * plot.getWidth();
        const float mx = static_cast<float>(e.x);
        const float my = static_cast<float>(e.y);

        if (std::abs(mx - x1) < 12.0f)
        {
            dragTarget = DragTarget::Crossover1;
            return;
        }
        if (std::abs(mx - x2) < 12.0f)
        {
            dragTarget = DragTarget::Crossover2;
            return;
        }

        const int clickedBand = (mx < x1) ? 0 : (mx < x2 ? 1 : 2);
        activeBand = clickedBand;
        if (onBandSelected)
            onBandSelected(activeBand);

        const float by = levelDbToY(levels[static_cast<size_t>(clickedBand)], plot);
        if (std::abs(my - by) < 16.0f)
            dragTarget = (clickedBand == 0) ? DragTarget::Level0 : (clickedBand == 1 ? DragTarget::Level1 : DragTarget::Level2);
        else
            dragTarget = DragTarget::None;

        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragTarget == DragTarget::None)
            return;
        const auto plot = getLocalBounds().toFloat().reduced(38.0f, 22.0f);
        if (plot.getWidth() <= 10.0f || plot.getHeight() <= 10.0f)
            return;

        const float normX = (static_cast<float>(e.x) - plot.getX()) / plot.getWidth();
        const float hz = detail::normXToFreq(normX);
        const float db = yToLevelDb(static_cast<float>(e.y), plot);

        if (unit == BassRackUnit::TriComp)
        {
            auto p = processor.getTriComp();
            if (dragTarget == DragTarget::Crossover1)
                p.lowMidHz = juce::jlimit(40.0f, juce::jmin(500.0f, p.midHighHz * 0.78f), hz);
            else if (dragTarget == DragTarget::Crossover2)
                p.midHighHz = juce::jlimit(juce::jmax(400.0f, p.lowMidHz * 1.28f), 6000.0f, hz);
            else if (dragTarget == DragTarget::Level0)
                p.bands[0].levelDb = db;
            else if (dragTarget == DragTarget::Level1)
                p.bands[1].levelDb = db;
            else if (dragTarget == DragTarget::Level2)
                p.bands[2].levelDb = db;
            processor.setTriComp(p);
        }
        else
        {
            auto p = processor.getTriDrive();
            if (dragTarget == DragTarget::Crossover1)
                p.lowMidHz = juce::jlimit(60.0f, juce::jmin(500.0f, p.midHighHz * 0.78f), hz);
            else if (dragTarget == DragTarget::Crossover2)
                p.midHighHz = juce::jlimit(juce::jmax(600.0f, p.lowMidHz * 1.28f), 6000.0f, hz);
            else if (dragTarget == DragTarget::Level0)
                p.lowLevelDb = db;
            else if (dragTarget == DragTarget::Level1)
                p.midLevelDb = db;
            else if (dragTarget == DragTarget::Level2)
                p.highLevelDb = db;
            processor.setTriDrive(p);
        }

        if (onParametersChanged)
            onParametersChanged();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragTarget = DragTarget::None;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto plot = getLocalBounds().toFloat().reduced(38.0f, 22.0f);
        float f1 = unit == BassRackUnit::TriComp ? processor.getTriComp().lowMidHz : processor.getTriDrive().lowMidHz;
        float f2 = unit == BassRackUnit::TriComp ? processor.getTriComp().midHighHz : processor.getTriDrive().midHighHz;
        const float x1 = plot.getX() + detail::freqToNormX(f1) * plot.getWidth();
        const float x2 = plot.getX() + detail::freqToNormX(f2) * plot.getWidth();
        const int b = (e.x < x1) ? 0 : (e.x < x2 ? 1 : 2);
        if (unit == BassRackUnit::TriComp)
        {
            auto p = processor.getTriComp();
            p.bands[static_cast<size_t>(b)].levelDb = 0.0f;
            processor.setTriComp(p);
        }
        else
        {
            auto p = processor.getTriDrive();
            if (b == 0) p.lowLevelDb = 0.0f;
            else if (b == 1) p.midLevelDb = 0.0f;
            else p.highLevelDb = 0.0f;
            processor.setTriDrive(p);
        }
        if (onParametersChanged)
            onParametersChanged();
        repaint();
    }

private:
    enum class DragTarget
    {
        None,
        Crossover1,
        Crossover2,
        Level0,
        Level1,
        Level2
    };

    static float levelDbToY(float db, juce::Rectangle<float> plot) noexcept
    {
        const float clamped = juce::jlimit(-24.0f, 24.0f, db);
        const float norm = (24.0f - clamped) / 48.0f;
        return plot.getY() + norm * plot.getHeight();
    }

    static float yToLevelDb(float y, juce::Rectangle<float> plot) noexcept
    {
        const float norm = juce::jlimit(0.0f, 1.0f, (y - plot.getY()) / plot.getHeight());
        return 24.0f - norm * 48.0f;
    }

    void drawSpectrumPath(juce::Graphics& g,
                          juce::Rectangle<float> plot,
                          const std::array<float, multiband::SpectrumFifo::kNumBins>& binsDb,
                          juce::Colour colour,
                          bool fillUnder) const
    {
        juce::Path path;
        constexpr double sr = 48000.0;
        constexpr int fftSize = multiband::SpectrumFifo::kFftSize;
        bool started = false;

        for (size_t bin = 1; bin < binsDb.size(); ++bin)
        {
            const float hz = static_cast<float>(bin * sr / fftSize);
            if (hz < detail::kMinFreqHz || hz > detail::kMaxFreqHz)
                continue;
            const float x = plot.getX() + detail::freqToNormX(hz) * plot.getWidth();
            const float db = juce::jlimit(-84.0f, 6.0f, binsDb[bin]);
            const float y = plot.getBottom() - ((db + 84.0f) / 90.0f) * plot.getHeight();
            if (!started)
            {
                path.startNewSubPath(x, y);
                started = true;
            }
            else
            {
                path.lineTo(x, y);
            }
        }
        if (!started)
            return;

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(fillUnder ? 1.8f : 1.1f));

        if (fillUnder)
        {
            juce::Path closed(path);
            closed.lineTo(plot.getRight(), plot.getBottom());
            closed.lineTo(plot.getX(), plot.getBottom());
            closed.closeSubPath();
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.28f), 0.0f, plot.getY(),
                                                   colour.withAlpha(0.02f), 0.0f, plot.getBottom(), false));
            g.fillPath(closed);
        }
    }

    void timerCallback() override
    {
        if (!isShowing())
            return;
        if (unit == BassRackUnit::TriComp)
            processor.triCompProcessor().spectrumFifo().computeSpectrum(preDb, postDb);
        else
            processor.triDriveProcessor().spectrumFifo().computeSpectrum(preDb, postDb);
        repaint();
    }

    board::Processor& processor;
    BassRackUnit unit;
    std::array<float, multiband::SpectrumFifo::kNumBins> preDb{};
    std::array<float, multiband::SpectrumFifo::kNumBins> postDb{};
    DragTarget dragTarget = DragTarget::None;
    int activeBand = 1;
};

/**
 * Full Parallax X / Distressor-inspired Editor Popup for TRI-COMP and TRI-DRIVE.
 * Features the large InteractiveSpectrumCanvas on the upper half and the 3-band
 * parameter strips + global controls on the lower half.
 */
class BassRackEditorView final : public juce::Component, private juce::Timer
{
public:
    BassRackEditorView(board::Processor& proc,
                       BassRackUnit initialUnit,
                       std::function<void()> onStateChanged = {},
                       std::function<void(int)> onChooseBandNam = {},
                       std::function<void()> onChooseCabIr = {})
        : processor(proc),
          unit(initialUnit),
          notifyHost(std::move(onStateChanged)),
          chooseBandNamCallback(std::move(onChooseBandNam)),
          chooseCabIrCallback(std::move(onChooseCabIr)),
          spectrum(proc, initialUnit)
    {
        setSize(980, 620);

        addAndMakeVisible(spectrum);
        spectrum.onParametersChanged = [this] { syncFromProcessor(); if (notifyHost) notifyHost(); };

        // Header controls
        enableToggle.setClickingTogglesState(true);
        enableToggle.onClick = [this] { commitUiToProcessor(); };
        addAndMakeVisible(enableToggle);

        presetBox.onChange = [this]
        {
            const int idx = presetBox.getSelectedId() - 1;
            if (idx < 0) return;
            if (unit == BassRackUnit::TriComp)
                processor.setTriComp(triComp::presetDefaults(idx, processor.getTriComp().enabled));
            else
                processor.setTriDrive(triDrive::presetDefaults(idx, processor.getTriDrive().enabled));
            syncFromProcessor();
            if (notifyHost) notifyHost();
        };
        addAndMakeVisible(presetBox);

        orderButton.onClick = [this]
        {
            const auto next = processor.rackOrder() == BassRackOrder::CompThenDrive
                                  ? BassRackOrder::DriveThenComp
                                  : BassRackOrder::CompThenDrive;
            processor.setRackOrder(next);
            syncFromProcessor();
            if (notifyHost) notifyHost();
        };
        addAndMakeVisible(orderButton);

        configureKnob(inputKnob, -24.0, 24.0, 0.1, " dB");
        configureKnob(outputKnob, -24.0, 24.0, 0.1, " dB");
        configureKnob(mixKnob, 0.0, 100.0, 1.0, " %");
        configureKnob(linkKnob, 0.0, 100.0, 1.0, " %");
        addAndMakeVisible(inputKnob);
        addAndMakeVisible(outputKnob);
        addAndMakeVisible(mixKnob);
        addAndMakeVisible(linkKnob);

        if (unit == BassRackUnit::TriComp)
        {
            for (int i = 0; i < static_cast<int>(triComp::kPresets.size()); ++i)
                presetBox.addItem(triComp::kPresets[static_cast<size_t>(i)].name, i + 1);

            audioHpfToggle.setButtonText("80Hz HPF");
            audioHpfToggle.setClickingTogglesState(true);
            audioHpfToggle.onClick = [this] { commitUiToProcessor(); };
            addAndMakeVisible(audioHpfToggle);

            for (size_t b = 0; b < 3; ++b)
            {
                auto& col = compColumns[b];
                for (int r = 0; r < 8; ++r)
                    col.ratioBox.addItem(triComp::ratioName(static_cast<multiband::DistressorRatio>(r)), r + 1);
                for (int a = 0; a < 3; ++a)
                    col.audioBox.addItem(triComp::audioModeName(static_cast<multiband::DistressorAudioMode>(a)), a + 1);
                for (int d = 0; d < 4; ++d)
                    col.detectorBox.addItem(triComp::detectorModeName(static_cast<multiband::DetectorMode>(d)), d + 1);

                col.ratioBox.onChange = [this] { commitUiToProcessor(); };
                col.audioBox.onChange = [this] { commitUiToProcessor(); };
                col.detectorBox.onChange = [this] { commitUiToProcessor(); };

                configureKnob(col.attackKnob, 0.1, 50.0, 0.1, " ms");
                configureKnob(col.releaseKnob, 30.0, 3000.0, 1.0, " ms");
                configureKnob(col.levelKnob, -24.0, 24.0, 0.1, " dB");

                col.soloBtn.setButtonText("S");
                col.muteBtn.setButtonText("M");
                col.soloBtn.setClickingTogglesState(true);
                col.muteBtn.setClickingTogglesState(true);
                col.soloBtn.onClick = [this] { commitUiToProcessor(); };
                col.muteBtn.onClick = [this] { commitUiToProcessor(); };

                for (auto* c : std::initializer_list<juce::Component*>{
                         &col.ratioBox, &col.audioBox, &col.detectorBox,
                         &col.attackKnob, &col.releaseKnob, &col.levelKnob,
                         &col.soloBtn, &col.muteBtn })
                    addAndMakeVisible(c);
            }
        }
        else
        {
            for (int i = 0; i < static_cast<int>(triDrive::kPresets.size()); ++i)
                presetBox.addItem(triDrive::kPresets[static_cast<size_t>(i)].name, i + 1);

            // Low band controls
            configureKnob(lowCompKnob, 0.0, 10.0, 0.1, "");
            configureKnob(lowLpfKnob, 80.0, 500.0, 1.0, " Hz");
            configureKnob(lowLevelKnob, -24.0, 24.0, 0.1, " dB");

            // Mid band controls
            for (int c = 0; c < 5; ++c)
                midCharBox.addItem(triDrive::characterName(static_cast<multiband::DriveCharacter>(c)), c + 1);
            midCharBox.onChange = [this] { commitUiToProcessor(); };
            configureKnob(midDriveKnob, 0.0, 10.0, 0.1, "");
            configureKnob(midPresenceKnob, -6.0, 6.0, 0.1, " dB");
            configureKnob(midLevelKnob, -24.0, 24.0, 0.1, " dB");
            midNamBtn.setButtonText("Load Mid NAM...");
            midNamBtn.onClick = [this] { if (chooseBandNamCallback) chooseBandNamCallback(0); syncFromProcessor(); };

            // High band controls
            for (int c : { 0, 1, 3, 4 })
                highCharBox.addItem(triDrive::characterName(static_cast<multiband::DriveCharacter>(c)), c + 1);
            highCharBox.onChange = [this] { commitUiToProcessor(); };
            configureKnob(highDriveKnob, 0.0, 10.0, 0.1, "");
            configureKnob(highHpfKnob, 500.0, 5000.0, 10.0, " Hz");
            configureKnob(highLevelKnob, -24.0, 24.0, 0.1, " dB");
            highNamBtn.setButtonText("Load High NAM...");
            highNamBtn.onClick = [this] { if (chooseBandNamCallback) chooseBandNamCallback(1); syncFromProcessor(); };

            for (size_t b = 0; b < 3; ++b)
            {
                driveSolo[b].setButtonText("S");
                driveMute[b].setButtonText("M");
                driveSolo[b].setClickingTogglesState(true);
                driveMute[b].setClickingTogglesState(true);
                driveSolo[b].onClick = [this] { commitUiToProcessor(); };
                driveMute[b].onClick = [this] { commitUiToProcessor(); };
                addAndMakeVisible(driveSolo[b]);
                addAndMakeVisible(driveMute[b]);
            }

            cabSimToggle.setButtonText("M+H Cab Sim");
            cabSimToggle.setClickingTogglesState(true);
            cabSimToggle.onClick = [this] { commitUiToProcessor(); };
            cabIrBtn.setButtonText("Load Cab IR...");
            cabIrBtn.onClick = [this] { if (chooseCabIrCallback) chooseCabIrCallback(); syncFromProcessor(); };
            postEqToggle.setButtonText("Post 7-Band EQ");
            postEqToggle.setClickingTogglesState(true);
            postEqToggle.onClick = [this] { commitUiToProcessor(); };

            for (auto* c : std::initializer_list<juce::Component*>{
                     &lowCompKnob, &lowLpfKnob, &lowLevelKnob,
                     &midCharBox, &midDriveKnob, &midPresenceKnob, &midLevelKnob, &midNamBtn,
                     &highCharBox, &highDriveKnob, &highHpfKnob, &highLevelKnob, &highNamBtn,
                     &cabSimToggle, &cabIrBtn, &postEqToggle })
                addAndMakeVisible(c);

            for (size_t b = 0; b < 7; ++b)
            {
                auto& s = postEqSliders[b];
                s.setSliderStyle(juce::Slider::LinearVertical);
                s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                s.setRange(-12.0, 12.0, 0.25);
                s.setDoubleClickReturnValue(true, 0.0);
                s.onValueChange = [this] { if (!syncing) commitUiToProcessor(); };
                addAndMakeVisible(s);
            }
        }

        syncFromProcessor();
        startTimerHz(20);
    }

    ~BassRackEditorView() override { stopTimer(); }

    void syncFromProcessor()
    {
        juce::ScopedValueSetter<bool> guard(syncing, true);
        orderButton.setButtonText(processor.rackOrder() == BassRackOrder::CompThenDrive
                                      ? "Chain: COMP -> DRIVE"
                                      : "Chain: DRIVE -> COMP");

        if (unit == BassRackUnit::TriComp)
        {
            const auto p = processor.getTriComp();
            enableToggle.setToggleState(p.enabled, juce::dontSendNotification);
            enableToggle.setButtonText(p.enabled ? "ACTIVE" : "BYPASSED");
            presetBox.setSelectedId(p.preset + 1, juce::dontSendNotification);
            inputKnob.setValue(p.inputDb, juce::dontSendNotification);
            outputKnob.setValue(p.outputDb, juce::dontSendNotification);
            mixKnob.setValue(p.mix * 100.0f, juce::dontSendNotification);
            linkKnob.setValue(p.linkAmount * 100.0f, juce::dontSendNotification);
            audioHpfToggle.setToggleState(p.audioHighPass, juce::dontSendNotification);

            for (size_t b = 0; b < 3; ++b)
            {
                auto& col = compColumns[b];
                const auto& bp = p.bands[b];
                col.ratioBox.setSelectedId(static_cast<int>(bp.ratio) + 1, juce::dontSendNotification);
                col.audioBox.setSelectedId(static_cast<int>(bp.audioMode) + 1, juce::dontSendNotification);
                col.detectorBox.setSelectedId(static_cast<int>(bp.detectorMode) + 1, juce::dontSendNotification);
                col.attackKnob.setValue(bp.attackMs, juce::dontSendNotification);
                col.releaseKnob.setValue(bp.releaseMs, juce::dontSendNotification);
                col.levelKnob.setValue(bp.levelDb, juce::dontSendNotification);
                col.soloBtn.setToggleState(bp.solo, juce::dontSendNotification);
                col.muteBtn.setToggleState(bp.mute, juce::dontSendNotification);
            }
        }
        else
        {
            const auto p = processor.getTriDrive();
            enableToggle.setToggleState(p.enabled, juce::dontSendNotification);
            enableToggle.setButtonText(p.enabled ? "ACTIVE" : "BYPASSED");
            presetBox.setSelectedId(p.preset + 1, juce::dontSendNotification);
            inputKnob.setValue(p.inputDb, juce::dontSendNotification);
            outputKnob.setValue(p.outputDb, juce::dontSendNotification);
            mixKnob.setValue(p.blend * 100.0f, juce::dontSendNotification);

            lowCompKnob.setValue(p.lowComp, juce::dontSendNotification);
            lowLpfKnob.setValue(p.lowLpfHz, juce::dontSendNotification);
            lowLevelKnob.setValue(p.lowLevelDb, juce::dontSendNotification);
            driveSolo[0].setToggleState(p.lowSolo, juce::dontSendNotification);
            driveMute[0].setToggleState(p.lowMute, juce::dontSendNotification);

            midCharBox.setSelectedId(static_cast<int>(p.midCharacter) + 1, juce::dontSendNotification);
            midDriveKnob.setValue(p.midDrive, juce::dontSendNotification);
            midPresenceKnob.setValue(p.midPresence, juce::dontSendNotification);
            midLevelKnob.setValue(p.midLevelDb, juce::dontSendNotification);
            driveSolo[1].setToggleState(p.midSolo, juce::dontSendNotification);
            driveMute[1].setToggleState(p.midMute, juce::dontSendNotification);

            highCharBox.setSelectedId(static_cast<int>(p.highCharacter) + 1, juce::dontSendNotification);
            highDriveKnob.setValue(p.highDrive, juce::dontSendNotification);
            highHpfKnob.setValue(p.highHpfHz, juce::dontSendNotification);
            highLevelKnob.setValue(p.highLevelDb, juce::dontSendNotification);
            driveSolo[2].setToggleState(p.highSolo, juce::dontSendNotification);
            driveMute[2].setToggleState(p.highMute, juce::dontSendNotification);

            cabSimToggle.setToggleState(p.cabSimEnabled, juce::dontSendNotification);
            postEqToggle.setToggleState(p.postEqEnabled, juce::dontSendNotification);

            const auto& assets = processor.triDriveProcessor().assetState();
            midNamBtn.setButtonText(assets.midNamLabel.isNotEmpty() ? "NAM: " + assets.midNamLabel : "Load Mid NAM...");
            highNamBtn.setButtonText(assets.highNamLabel.isNotEmpty() ? "NAM: " + assets.highNamLabel : "Load High NAM...");
            cabIrBtn.setButtonText(assets.cabIrLabel.isNotEmpty() ? "IR: " + assets.cabIrLabel : "8x10 Cab / Load IR...");

            for (size_t b = 0; b < 7; ++b)
                postEqSliders[b].setValue(p.postEqBands[b], juce::dontSendNotification);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b141a));

        // Top bar background
        auto topBar = getLocalBounds().removeFromTop(50).toFloat();
        g.setColour(juce::Colour(0xff111f28));
        g.fillRect(topBar);
        g.setColour(juce::Colour(0xff233746));
        g.drawHorizontalLine(50, 0.0f, static_cast<float>(getWidth()));

        g.setFont(ui::font(18.0f, true));
        g.setColour(unit == BassRackUnit::TriComp ? juce::Colour(0xff29b6f6) : juce::Colour(0xffff8a3d));
        g.drawText(unit == BassRackUnit::TriComp ? "TRI-COMP // 3-BAND DISTRESSOR" : "TRI-DRIVE // 3-BAND PARALLAX ENGINE",
                   16, 10, 340, 30, juce::Justification::centredLeft);

        // Band column cards
        const auto lower = getLocalBounds().withTrimmedTop(308).reduced(14, 8);
        const int colW = (lower.getWidth() - 24) / 3;
        const char* titles[3] = {
            "BAND 1 // SUB & LOW FUNDAMENTAL",
            "BAND 2 // MID PUNCH & GROWL",
            "BAND 3 // HIGH ATTACK & CLANK"
        };

        for (int b = 0; b < 3; ++b)
        {
            juce::Rectangle<int> card(lower.getX() + b * (colW + 12), lower.getY(), colW, lower.getHeight());
            g.setColour(juce::Colour(0xff101c24));
            g.fillRoundedRectangle(card.toFloat(), 8.0f);
            g.setColour(detail::bandColour(b).withAlpha(0.55f));
            g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 8.0f, 1.3f);

            g.setFont(ui::font(12.0f, true));
            g.setColour(detail::bandColour(b));
            g.drawText(titles[b], card.getX() + 12, card.getY() + 8, card.getWidth() - 80, 20, juce::Justification::centredLeft);

            if (unit == BassRackUnit::TriComp)
            {
                g.setFont(ui::font(11.0f));
                g.setColour(juce::Colour(0xff90a4ae));
                g.drawText("RATIO", card.getX() + 12, card.getY() + 36, 70, 18, juce::Justification::centredLeft);
                g.drawText("HARMONICS", card.getX() + 112, card.getY() + 36, 90, 18, juce::Justification::centredLeft);
                g.drawText("DETECTOR", card.getX() + 212, card.getY() + 36, 80, 18, juce::Justification::centredLeft);
                g.drawText("ATTACK", card.getX() + 16, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                g.drawText("RELEASE", card.getX() + 112, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                g.drawText("LEVEL", card.getX() + 208, card.getBottom() - 32, 80, 16, juce::Justification::centred);
            }
            else
            {
                g.setFont(ui::font(11.0f));
                g.setColour(juce::Colour(0xff90a4ae));
                if (b == 0)
                {
                    g.drawText("BUS COMP", card.getX() + 16, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                    g.drawText("CLEAN LPF", card.getX() + 112, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                    g.drawText("SUB LEVEL", card.getX() + 208, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                }
                else if (b == 1)
                {
                    g.drawText("MID DRIVE", card.getX() + 16, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                    g.drawText("PRESENCE", card.getX() + 112, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                    g.drawText("MID LEVEL", card.getX() + 208, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                }
                else
                {
                    g.drawText("HIGH DRIVE", card.getX() + 16, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                    g.drawText("PRE HPF", card.getX() + 112, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                    g.drawText("HIGH LEVEL", card.getX() + 208, card.getBottom() - 32, 80, 16, juce::Justification::centred);
                }
            }
        }
    }

    void resized() override
    {
        enableToggle.setBounds(355, 11, 86, 28);
        presetBox.setBounds(448, 11, 210, 28);
        orderButton.setBounds(666, 11, 152, 28);

        inputKnob.setBounds(825, 4, 48, 44);
        mixKnob.setBounds(875, 4, 48, 44);
        outputKnob.setBounds(925, 4, 48, 44);

        spectrum.setBounds(14, 58, getWidth() - 28, 244);

        const auto lower = getLocalBounds().withTrimmedTop(308).reduced(14, 8);
        const int colW = (lower.getWidth() - 24) / 3;

        if (unit == BassRackUnit::TriComp)
        {
            linkKnob.setBounds(775, 4, 48, 44);
            audioHpfToggle.setBounds(lower.getX() + 12, lower.getY() + 82, 90, 24);
            for (int b = 0; b < 3; ++b)
            {
                juce::Rectangle<int> card(lower.getX() + b * (colW + 12), lower.getY(), colW, lower.getHeight());
                auto& col = compColumns[static_cast<size_t>(b)];
                col.soloBtn.setBounds(card.getRight() - 62, card.getY() + 7, 24, 22);
                col.muteBtn.setBounds(card.getRight() - 34, card.getY() + 7, 24, 22);

                col.ratioBox.setBounds(card.getX() + 12, card.getY() + 54, 92, 26);
                col.audioBox.setBounds(card.getX() + 110, card.getY() + 54, 92, 26);
                col.detectorBox.setBounds(card.getX() + 208, card.getY() + 54, 92, 26);

                const int knobY = card.getY() + 116;
                col.attackKnob.setBounds(card.getX() + 16, knobY, 80, 120);
                col.releaseKnob.setBounds(card.getX() + 112, knobY, 80, 120);
                col.levelKnob.setBounds(card.getX() + 208, knobY, 80, 120);
            }
        }
        else
        {
            linkKnob.setVisible(false);
            for (int b = 0; b < 3; ++b)
            {
                juce::Rectangle<int> card(lower.getX() + b * (colW + 12), lower.getY(), colW, lower.getHeight());
                driveSolo[static_cast<size_t>(b)].setBounds(card.getRight() - 62, card.getY() + 7, 24, 22);
                driveMute[static_cast<size_t>(b)].setBounds(card.getRight() - 34, card.getY() + 7, 24, 22);
                const int knobY = card.getY() + 116;

                if (b == 0)
                {
                    cabSimToggle.setBounds(card.getX() + 12, card.getY() + 42, 128, 26);
                    cabIrBtn.setBounds(card.getX() + 146, card.getY() + 42, 152, 26);
                    postEqToggle.setBounds(card.getX() + 12, card.getY() + 74, 128, 24);
                    for (int eqIdx = 0; eqIdx < 7; ++eqIdx)
                        postEqSliders[static_cast<size_t>(eqIdx)].setBounds(card.getX() + 146 + eqIdx * 22, card.getY() + 72, 20, 42);

                    lowCompKnob.setBounds(card.getX() + 16, knobY, 80, 120);
                    lowLpfKnob.setBounds(card.getX() + 112, knobY, 80, 120);
                    lowLevelKnob.setBounds(card.getX() + 208, knobY, 80, 120);
                }
                else if (b == 1)
                {
                    midCharBox.setBounds(card.getX() + 12, card.getY() + 48, 120, 26);
                    midNamBtn.setBounds(card.getX() + 140, card.getY() + 48, 158, 26);
                    midDriveKnob.setBounds(card.getX() + 16, knobY, 80, 120);
                    midPresenceKnob.setBounds(card.getX() + 112, knobY, 80, 120);
                    midLevelKnob.setBounds(card.getX() + 208, knobY, 80, 120);
                }
                else
                {
                    highCharBox.setBounds(card.getX() + 12, card.getY() + 48, 120, 26);
                    highNamBtn.setBounds(card.getX() + 140, card.getY() + 48, 158, 26);
                    highDriveKnob.setBounds(card.getX() + 16, knobY, 80, 120);
                    highHpfKnob.setBounds(card.getX() + 112, knobY, 80, 120);
                    highLevelKnob.setBounds(card.getX() + 208, knobY, 80, 120);
                }
            }
        }
    }

private:
    struct CompBandColumn
    {
        juce::ComboBox ratioBox, audioBox, detectorBox;
        juce::Slider attackKnob, releaseKnob, levelKnob;
        juce::TextButton soloBtn, muteBtn;
    };

    void configureKnob(juce::Slider& s, double minV, double maxV, double step, const char* suffix)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 18);
        s.setRange(minV, maxV, step);
        s.setTextValueSuffix(suffix);
        s.onValueChange = [this] { if (!syncing) commitUiToProcessor(); };
    }

    void commitUiToProcessor()
    {
        if (syncing)
            return;

        if (unit == BassRackUnit::TriComp)
        {
            auto p = processor.getTriComp();
            p.enabled = enableToggle.getToggleState();
            p.inputDb = static_cast<float>(inputKnob.getValue());
            p.outputDb = static_cast<float>(outputKnob.getValue());
            p.mix = static_cast<float>(mixKnob.getValue() * 0.01);
            p.linkAmount = static_cast<float>(linkKnob.getValue() * 0.01);
            p.audioHighPass = audioHpfToggle.getToggleState();

            for (size_t b = 0; b < 3; ++b)
            {
                const auto& col = compColumns[b];
                auto& bp = p.bands[b];
                bp.ratio = static_cast<multiband::DistressorRatio>(juce::jlimit(0, 7, col.ratioBox.getSelectedId() - 1));
                bp.audioMode = static_cast<multiband::DistressorAudioMode>(juce::jlimit(0, 2, col.audioBox.getSelectedId() - 1));
                bp.detectorMode = static_cast<multiband::DetectorMode>(juce::jlimit(0, 3, col.detectorBox.getSelectedId() - 1));
                bp.attackMs = static_cast<float>(col.attackKnob.getValue());
                bp.releaseMs = static_cast<float>(col.releaseKnob.getValue());
                bp.levelDb = static_cast<float>(col.levelKnob.getValue());
                bp.solo = col.soloBtn.getToggleState();
                bp.mute = col.muteBtn.getToggleState();
            }
            processor.setTriComp(p);
        }
        else
        {
            auto p = processor.getTriDrive();
            p.enabled = enableToggle.getToggleState();
            p.inputDb = static_cast<float>(inputKnob.getValue());
            p.outputDb = static_cast<float>(outputKnob.getValue());
            p.blend = static_cast<float>(mixKnob.getValue() * 0.01);

            p.lowComp = static_cast<float>(lowCompKnob.getValue());
            p.lowLpfHz = static_cast<float>(lowLpfKnob.getValue());
            p.lowLevelDb = static_cast<float>(lowLevelKnob.getValue());
            p.lowSolo = driveSolo[0].getToggleState();
            p.lowMute = driveMute[0].getToggleState();

            p.midCharacter = static_cast<multiband::DriveCharacter>(juce::jlimit(0, 4, midCharBox.getSelectedId() - 1));
            p.midDrive = static_cast<float>(midDriveKnob.getValue());
            p.midPresence = static_cast<float>(midPresenceKnob.getValue());
            p.midLevelDb = static_cast<float>(midLevelKnob.getValue());
            p.midSolo = driveSolo[1].getToggleState();
            p.midMute = driveMute[1].getToggleState();

            p.highCharacter = static_cast<multiband::DriveCharacter>(juce::jlimit(0, 4, highCharBox.getSelectedId() - 1));
            p.highDrive = static_cast<float>(highDriveKnob.getValue());
            p.highHpfHz = static_cast<float>(highHpfKnob.getValue());
            p.highLevelDb = static_cast<float>(highLevelKnob.getValue());
            p.highSolo = driveSolo[2].getToggleState();
            p.highMute = driveMute[2].getToggleState();

            p.cabSimEnabled = cabSimToggle.getToggleState();
            p.postEqEnabled = postEqToggle.getToggleState();
            for (size_t b = 0; b < 7; ++b)
                p.postEqBands[b] = static_cast<float>(postEqSliders[b].getValue());

            processor.setTriDrive(p);
        }

        enableToggle.setButtonText(enableToggle.getToggleState() ? "ACTIVE" : "BYPASSED");
        if (notifyHost)
            notifyHost();
        repaint();
    }

    void timerCallback() override
    {
        if (isShowing())
            repaint();
    }

    board::Processor& processor;
    BassRackUnit unit;
    std::function<void()> notifyHost;
    std::function<void(int)> chooseBandNamCallback;
    std::function<void()> chooseCabIrCallback;
    bool syncing = false;

    InteractiveSpectrumCanvas spectrum;
    juce::TextButton enableToggle{"ACTIVE"}, orderButton{"Chain: COMP -> DRIVE"}, audioHpfToggle{"80Hz HPF"};
    juce::ComboBox presetBox;
    juce::Slider inputKnob, outputKnob, mixKnob, linkKnob;

    std::array<CompBandColumn, 3> compColumns;

    juce::Slider lowCompKnob, lowLpfKnob, lowLevelKnob;
    juce::ComboBox midCharBox, highCharBox;
    juce::Slider midDriveKnob, midPresenceKnob, midLevelKnob;
    juce::Slider highDriveKnob, highHpfKnob, highLevelKnob;
    juce::TextButton midNamBtn, highNamBtn, cabSimToggle, cabIrBtn, postEqToggle;
    std::array<juce::TextButton, 3> driveSolo, driveMute;
    std::array<juce::Slider, 7> postEqSliders;
};

/**
 * Wide Pedalboard Summary Card for TRI-COMP or TRI-DRIVE on the Bass Pedalboard.
 * Shows live mini spectrum + 3-band crossover preview, per-band GR/drive meters,
 * quick macro knobs, bypass footswitch, preset selector, and an "OPEN RACK EDITOR"
 * button that launches the full-size Parallax X-style interactive spectrum analyzer.
 */
class BassRackCard final : public juce::Component
{
public:
    BassRackCard(board::Processor& proc, BassRackUnit unitType)
        : processor(proc), unit(unitType)
    {
        setComponentID(unit == BassRackUnit::TriComp ? "bass-card-tricomp" : "bass-card-tridrive");

        bypassBtn.setClickingTogglesState(true);
        bypassBtn.onClick = [this]
        {
            if (unit == BassRackUnit::TriComp)
            {
                auto p = processor.getTriComp();
                p.enabled = bypassBtn.getToggleState();
                processor.setTriComp(p);
            }
            else
            {
                auto p = processor.getTriDrive();
                p.enabled = bypassBtn.getToggleState();
                processor.setTriDrive(p);
            }
            sync();
        };
        addAndMakeVisible(bypassBtn);

        presetBox.onChange = [this]
        {
            if (syncing) return;
            const int idx = presetBox.getSelectedId() - 1;
            if (idx < 0) return;
            if (unit == BassRackUnit::TriComp)
                processor.setTriComp(triComp::presetDefaults(idx, processor.getTriComp().enabled));
            else
                processor.setTriDrive(triDrive::presetDefaults(idx, processor.getTriDrive().enabled));
            sync();
        };
        addAndMakeVisible(presetBox);

        if (unit == BassRackUnit::TriComp)
        {
            for (int i = 0; i < static_cast<int>(triComp::kPresets.size()); ++i)
                presetBox.addItem(triComp::kPresets[static_cast<size_t>(i)].name, i + 1);
            setupMacroKnob(macroKnobs[0], -24.0, 24.0, " dB");
            setupMacroKnob(macroKnobs[1], 0.0, 100.0, " %");
            setupMacroKnob(macroKnobs[2], -24.0, 24.0, " dB");
        }
        else
        {
            for (int i = 0; i < static_cast<int>(triDrive::kPresets.size()); ++i)
                presetBox.addItem(triDrive::kPresets[static_cast<size_t>(i)].name, i + 1);
            setupMacroKnob(macroKnobs[0], 0.0, 10.0, "");
            setupMacroKnob(macroKnobs[1], 0.0, 10.0, "");
            setupMacroKnob(macroKnobs[2], 0.0, 10.0, "");
        }

        openEditorBtn.setButtonText("SPECTRUM & BANDS");
        openEditorBtn.onClick = [this] { if (onOpenEditor) onOpenEditor(unit); };
        addAndMakeVisible(openEditorBtn);

        sync();
    }

    std::function<void(BassRackUnit)> onOpenEditor;

    void sync()
    {
        juce::ScopedValueSetter<bool> guard(syncing, true);
        if (unit == BassRackUnit::TriComp)
        {
            const auto p = processor.getTriComp();
            bypassBtn.setToggleState(p.enabled, juce::dontSendNotification);
            bypassBtn.setButtonText(p.enabled ? "ON" : "BYPASS");
            presetBox.setSelectedId(p.preset + 1, juce::dontSendNotification);
            macroKnobs[0].setValue(p.inputDb, juce::dontSendNotification);
            macroKnobs[1].setValue(p.mix * 100.0f, juce::dontSendNotification);
            macroKnobs[2].setValue(p.outputDb, juce::dontSendNotification);
        }
        else
        {
            const auto p = processor.getTriDrive();
            bypassBtn.setToggleState(p.enabled, juce::dontSendNotification);
            bypassBtn.setButtonText(p.enabled ? "ON" : "BYPASS");
            presetBox.setSelectedId(p.preset + 1, juce::dontSendNotification);
            macroKnobs[0].setValue(p.lowComp, juce::dontSendNotification);
            macroKnobs[1].setValue(p.midDrive, juce::dontSendNotification);
            macroKnobs[2].setValue(p.highDrive, juce::dontSendNotification);
        }
        repaint();
    }

    void animate()
    {
        if (isVisible())
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(2.0f);
        const bool isComp = (unit == BassRackUnit::TriComp);
        const bool active = isComp ? processor.getTriComp().enabled : processor.getTriDrive().enabled;
        const auto accent = isComp ? juce::Colour(0xff29b6f6) : juce::Colour(0xffff8a3d);

        // Card chassis
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff16242d), 0.0f, area.getY(),
                                               juce::Colour(0xff0b1318), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle(area, 10.0f);
        g.setColour(active ? accent.withAlpha(0.85f) : juce::Colour(0xff2b3e4b));
        g.drawRoundedRectangle(area, 10.0f, active ? 2.0f : 1.2f);

        // Title & Subtitle
        g.setFont(ui::font(15.0f, true));
        g.setColour(juce::Colours::white);
        g.drawText(isComp ? "TRI-COMP" : "TRI-DRIVE", 14, 10, 140, 20, juce::Justification::centredLeft);

        g.setFont(ui::font(10.5f));
        g.setColour(accent);
        g.drawText(isComp ? "3-BAND DISTRESSOR" : "3-BAND PARALLAX DRIVE", 14, 27, 175, 16, juce::Justification::centredLeft);

        // Mini Crossover & Band GR/Drive Display
        juce::Rectangle<float> display(14.0f, 48.0f, area.getWidth() - 28.0f, juce::jmax(38.0f, area.getHeight() * 0.28f));
        g.setColour(juce::Colour(0xff070e13));
        g.fillRoundedRectangle(display, 6.0f);

        const float f1 = isComp ? processor.getTriComp().lowMidHz : processor.getTriDrive().lowMidHz;
        const float f2 = isComp ? processor.getTriComp().midHighHz : processor.getTriDrive().midHighHz;
        const float x1 = display.getX() + detail::freqToNormX(f1) * display.getWidth();
        const float x2 = display.getX() + detail::freqToNormX(f2) * display.getWidth();
        const std::array<float, 4> edges{{ display.getX(), x1, x2, display.getRight() }};

        for (int b = 0; b < 3; ++b)
        {
            juce::Rectangle<float> seg(edges[static_cast<size_t>(b)], display.getY(),
                                       edges[static_cast<size_t>(b + 1)] - edges[static_cast<size_t>(b)], display.getHeight());
            g.setColour(detail::bandColour(b).withAlpha(active ? 0.18f : 0.06f));
            g.fillRect(seg);

            g.setFont(ui::font(10.0f, true));
            g.setColour(detail::bandColour(b));
            if (isComp)
            {
                const auto bp = processor.getTriComp().bands[static_cast<size_t>(b)];
                const float gr = processor.triCompProcessor().readMeters().grDb[static_cast<size_t>(b)];
                g.drawText(juce::String(triComp::ratioName(bp.ratio)) + "  -" + juce::String(gr, 1) + "dB",
                           seg.toNearestInt().reduced(4, 2), juce::Justification::centred);
            }
            else
            {
                const auto p = processor.getTriDrive();
                juce::String summary = (b == 0)
                                           ? ("COMP " + juce::String(p.lowComp, 1))
                                           : (b == 1 ? juce::String(triDrive::characterName(p.midCharacter)) + " " + juce::String(p.midDrive, 1)
                                                     : juce::String(triDrive::characterName(p.highCharacter)) + " " + juce::String(p.highDrive, 1));
                g.drawText(summary, seg.toNearestInt().reduced(4, 2), juce::Justification::centred);
            }
        }
        g.setColour(juce::Colours::white.withAlpha(0.65f));
        g.drawVerticalLine(juce::roundToInt(x1), display.getY(), display.getBottom());
        g.drawVerticalLine(juce::roundToInt(x2), display.getY(), display.getBottom());
        g.setColour(juce::Colour(0xff223644));
        g.drawRoundedRectangle(display, 6.0f, 1.0f);

        // Knob labels
        const char* labelsComp[3] = { "INPUT", "MIX", "OUTPUT" };
        const char* labelsDrive[3] = { "LOW COMP", "MID DRIVE", "HIGH DRIVE" };
        g.setFont(ui::font(10.5f, true));
        g.setColour(juce::Colour(0xff9bb0bd));
        for (size_t i = 0; i < 3; ++i)
        {
            const auto kb = macroKnobs[i].getBounds();
            g.drawText(isComp ? labelsComp[i] : labelsDrive[i],
                       kb.getX() - 6, kb.getBottom() - 2, kb.getWidth() + 12, 16, juce::Justification::centred);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const auto display = juce::Rectangle<int>(14, 48, getWidth() - 28, juce::jmax(38, int(getHeight() * 0.28f)));
        if (display.contains(e.getPosition()) && onOpenEditor)
            onOpenEditor(unit);
    }

    void resized() override
    {
        const int w = getWidth();
        const int h = getHeight();

        bypassBtn.setBounds(w - 76, 10, 62, 26);
        openEditorBtn.setBounds(w - 220, 10, 138, 26);

        const int displayBottom = 48 + juce::jmax(38, int(h * 0.28f));
        presetBox.setBounds(14, displayBottom + 6, w - 28, 24);

        const int knobTop = presetBox.getBottom() + 4;
        const int knobH = juce::jmax(48, h - knobTop - 20);
        const int colW = (w - 28) / 3;
        for (int i = 0; i < 3; ++i)
            macroKnobs[static_cast<size_t>(i)].setBounds(14 + i * colW + (colW - 72) / 2, knobTop, 72, knobH);
    }

private:
    void setupMacroKnob(juce::Slider& s, double minV, double maxV, const char* suffix)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 15);
        s.setRange(minV, maxV, 0.1);
        s.setTextValueSuffix(suffix);
        s.onValueChange = [this] { if (!syncing) commitMacros(); };
        addAndMakeVisible(s);
    }

    void commitMacros()
    {
        if (unit == BassRackUnit::TriComp)
        {
            auto p = processor.getTriComp();
            p.inputDb = static_cast<float>(macroKnobs[0].getValue());
            p.mix = static_cast<float>(macroKnobs[1].getValue() * 0.01);
            p.outputDb = static_cast<float>(macroKnobs[2].getValue());
            processor.setTriComp(p);
        }
        else
        {
            auto p = processor.getTriDrive();
            p.lowComp = static_cast<float>(macroKnobs[0].getValue());
            p.midDrive = static_cast<float>(macroKnobs[1].getValue());
            p.highDrive = static_cast<float>(macroKnobs[2].getValue());
            processor.setTriDrive(p);
        }
        repaint();
    }

    board::Processor& processor;
    BassRackUnit unit;
    bool syncing = false;
    juce::TextButton bypassBtn, openEditorBtn;
    juce::ComboBox presetBox;
    std::array<juce::Slider, 3> macroKnobs;
};

} // namespace tonegrid::play
