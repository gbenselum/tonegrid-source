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

class BassRackLook final : public juce::LookAndFeel_V4
{
public:
    explicit BassRackLook(bool isDriveUnit = false) : isDrive(isDriveUnit)
    {
        using namespace juce;
        setColour(Slider::textBoxTextColourId, Colour(0xffdce9f2));
        setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(ComboBox::backgroundColourId, Colour(0xff0b151d));
        setColour(ComboBox::textColourId, Colour(0xffe2eef7));
        setColour(ComboBox::outlineColourId, Colour(0xff253b4a));
        setColour(PopupMenu::backgroundColourId, Colour(0xff0d1b24));
        setColour(PopupMenu::textColourId, Colour(0xffe2eef7));
        setColour(PopupMenu::highlightedBackgroundColourId, Colour(0xff1e3a4c));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return ui::font(12.5f); }
    juce::Font getPopupMenuFont() override { return ui::font(13.0f); }
    juce::Font getLabelFont(juce::Label&) override { return ui::font(11.5f, true); }

    void drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float>(0.5f, 0.5f, float(w) - 1.0f, float(h) - 1.0f);
        g.setColour(juce::Colour(box.isMouseOver() || down ? 0xff122330 : 0xff0a141c));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colour(box.isMouseOver() ? 0xff3b5c73 : 0xff233847));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);

        juce::Path arrow;
        const float cx = float(w) - 14.0f, cy = float(h) * 0.5f;
        arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
        arrow.lineTo(cx, cy + 2.5f);
        arrow.lineTo(cx + 4.0f, cy - 2.0f);
        g.setColour(juce::Colour(0xff9eb4c2));
        g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced(0.75f);
        const bool on = b.getToggleState();
        const auto txt = b.getButtonText();
        const auto primary = isDrive ? juce::Colour(0xffff8a3d) : juce::Colour(0xff29b6f6);

        juce::Colour bg(0xff10202b);
        juce::Colour border(0xff2b4354);
        if (txt == "S" && on)
        {
            bg = juce::Colour(0xff664d00);
            border = juce::Colour(0xffffca28);
        }
        else if (txt == "M" && on)
        {
            bg = juce::Colour(0xff6b1d1c);
            border = juce::Colour(0xffef5350);
        }
        else if (on)
        {
            bg = primary.withAlpha(0.26f);
            border = primary;
        }
        else if (txt == "SPECTRUM & BANDS")
        {
            bg = primary.withAlpha(hover ? 0.24f : 0.14f);
            border = primary.withAlpha(0.75f);
        }

        if (hover && !down)
            bg = bg.brighter(0.12f);
        g.setColour(bg);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(border);
        g.drawRoundedRectangle(r, 5.0f, on ? 1.3f : 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        const auto txt = b.getButtonText();
        const auto primary = isDrive ? juce::Colour(0xffff8a3d) : juce::Colour(0xff29b6f6);
        g.setFont(ui::font(11.5f, true));
        if (txt == "SPECTRUM & BANDS")
            g.setColour(primary.brighter(0.35f));
        else if (on)
            g.setColour(juce::Colours::white);
        else
            g.setColour(juce::Colour(0xffbfd0dc));
        g.drawFittedText(txt, b.getLocalBounds().reduced(5, 1), juce::Justification::centred, 1);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        using namespace juce;
        const float diameter = jmin(float(w), float(h)) - 6.0f;
        if (diameter <= 8.0f) return;
        const auto r = Rectangle<float>(float(x), float(y), float(w), float(h)).withSizeKeepingCentre(diameter, diameter);
        const float cx = r.getCentreX(), cy = r.getCentreY(), radius = diameter * 0.5f;
        const auto accent = s.isColourSpecified(Slider::rotarySliderFillColourId)
                                ? s.findColour(Slider::rotarySliderFillColourId)
                                : (isDrive ? Colour(0xffff8a3d) : Colour(0xff29b6f6));

        // Background track arc
        Path bgArc;
        bgArc.addCentredArc(cx, cy, radius - 1.5f, radius - 1.5f, 0.0f, start, end, true);
        g.setColour(Colour(0xff182834));
        g.strokePath(bgArc, PathStrokeType(2.8f, PathStrokeType::curved, PathStrokeType::rounded));

        // Active value arc
        const float angle = start + pos * (end - start);
        Path valArc;
        valArc.addCentredArc(cx, cy, radius - 1.5f, radius - 1.5f, 0.0f, start, angle, true);
        g.setColour(accent);
        g.strokePath(valArc, PathStrokeType(2.8f, PathStrokeType::curved, PathStrokeType::rounded));

        // Outer shadow & dark metallic cap
        const auto cap = r.reduced(5.5f);
        g.setColour(Colours::black.withAlpha(0.65f));
        g.fillEllipse(cap.translated(0.0f, 2.5f));
        g.setGradientFill(ColourGradient(Colour(0xff2d3d49), cap.getX(), cap.getY(),
                                         Colour(0xff0d151b), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse(cap);
        g.setColour(Colour(0xff465c6b));
        g.drawEllipse(cap, 1.0f);

        // Pointer line
        Path needle;
        const float capR = cap.getWidth() * 0.5f;
        needle.addRoundedRectangle(-1.2f, -capR + 2.5f, 2.4f, capR * 0.48f, 1.0f);
        g.setColour(Colours::white);
        g.fillPath(needle, AffineTransform::rotation(angle).translated(cx, cy));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, const juce::Slider::SliderStyle, juce::Slider&) override
    {
        using namespace juce;
        const float cx = float(x) + float(w) * 0.5f;
        g.setColour(Colour(0xff081117));
        g.fillRoundedRectangle(cx - 2.5f, float(y), 5.0f, float(h), 2.0f);
        g.setColour(Colour(0xff304959));
        g.drawHorizontalLine(y + h / 2, cx - 5.0f, cx + 5.0f);
        Rectangle<float> thumb(cx - 6.0f, pos - 3.5f, 12.0f, 7.0f);
        g.setColour( isDrive ? Colour(0xffff8a3d) : Colour(0xff29b6f6));
        g.fillRoundedRectangle(thumb, 2.0f);
    }

private:
    bool isDrive = false;
};
} // namespace detail

/**
 * Interactive real-time FFT Spectrum + 3-Band Crossover & Level Handle Canvas.
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
 * Full Editor Popup for TRI-COMP and TRI-DRIVE.
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
          rackLook(initialUnit == BassRackUnit::TriDrive),
          notifyHost(std::move(onStateChanged)),
          chooseBandNamCallback(std::move(onChooseBandNam)),
          chooseCabIrCallback(std::move(onChooseCabIr)),
          spectrum(proc, initialUnit)
    {
        setLookAndFeel(&rackLook);
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

        configureHeaderKnob(inputKnob, -24.0, 24.0, 0.1, " dB");
        configureHeaderKnob(outputKnob, -24.0, 24.0, 0.1, " dB");
        configureHeaderKnob(mixKnob, 0.0, 100.0, 1.0, " %");
        configureHeaderKnob(linkKnob, 0.0, 100.0, 1.0, " %");
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
                const auto bCol = detail::bandColour(static_cast<int>(b));
                for (int r = 0; r < 8; ++r)
                    col.ratioBox.addItem(triComp::ratioName(static_cast<multiband::CompRatio>(r)), r + 1);
                for (int a = 0; a < 3; ++a)
                    col.audioBox.addItem(triComp::audioModeName(static_cast<multiband::CompAudioMode>(a)), a + 1);
                for (int d = 0; d < 4; ++d)
                    col.detectorBox.addItem(triComp::detectorModeName(static_cast<multiband::DetectorMode>(d)), d + 1);

                col.ratioBox.onChange = [this] { commitUiToProcessor(); };
                col.audioBox.onChange = [this] { commitUiToProcessor(); };
                col.detectorBox.onChange = [this] { commitUiToProcessor(); };

                configureKnob(col.attackKnob, 0.1, 50.0, 0.1, " ms", bCol);
                configureKnob(col.releaseKnob, 30.0, 3000.0, 1.0, " ms", bCol);
                configureKnob(col.levelKnob, -24.0, 24.0, 0.1, " dB", bCol);

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
            configureKnob(lowCompKnob, 0.0, 10.0, 0.1, "", detail::bandColour(0));
            configureKnob(lowLpfKnob, 80.0, 500.0, 1.0, " Hz", detail::bandColour(0));
            configureKnob(lowLevelKnob, -24.0, 24.0, 0.1, " dB", detail::bandColour(0));

            // Mid band controls
            for (int c = 0; c < 5; ++c)
                midCharBox.addItem(triDrive::characterName(static_cast<multiband::DriveCharacter>(c)), c + 1);
            midCharBox.onChange = [this] { commitUiToProcessor(); };
            configureKnob(midDriveKnob, 0.0, 10.0, 0.1, "", detail::bandColour(1));
            configureKnob(midPresenceKnob, -6.0, 6.0, 0.1, " dB", detail::bandColour(1));
            configureKnob(midLevelKnob, -24.0, 24.0, 0.1, " dB", detail::bandColour(1));
            midNamBtn.setButtonText("Load Mid NAM...");
            midNamBtn.onClick = [this] { if (chooseBandNamCallback) chooseBandNamCallback(0); syncFromProcessor(); };

            // High band controls
            for (int c : { 0, 1, 3, 4 })
                highCharBox.addItem(triDrive::characterName(static_cast<multiband::DriveCharacter>(c)), c + 1);
            highCharBox.onChange = [this] { commitUiToProcessor(); };
            configureKnob(highDriveKnob, 0.0, 10.0, 0.1, "", detail::bandColour(2));
            configureKnob(highHpfKnob, 500.0, 5000.0, 10.0, " Hz", detail::bandColour(2));
            configureKnob(highLevelKnob, -24.0, 24.0, 0.1, " dB", detail::bandColour(2));
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

    ~BassRackEditorView() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

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
        auto topBar = getLocalBounds().removeFromTop(52).toFloat();
        g.setColour(juce::Colour(0xff111f28));
        g.fillRect(topBar);
        g.setColour(juce::Colour(0xff233746));
        g.drawHorizontalLine(52, 0.0f, static_cast<float>(getWidth()));

        g.setFont(ui::font(17.5f, true));
        g.setColour(unit == BassRackUnit::TriComp ? juce::Colour(0xff29b6f6) : juce::Colour(0xffff8a3d));
        g.drawText(unit == BassRackUnit::TriComp ? "TRI-COMP // 3-BAND COMPRESSOR" : "TRI-DRIVE // 3-BAND DISTORTION",
                   16, 11, 335, 30, juce::Justification::centredLeft);

        // Header trim knob labels
        g.setFont(ui::font(9.5f, true));
        g.setColour(juce::Colour(0xff9bb0bd));
        if (unit == BassRackUnit::TriComp)
            g.drawText("SC LINK", 772, 2, 52, 12, juce::Justification::centred);
        g.drawText("INPUT",  824, 2, 50, 12, juce::Justification::centred);
        g.drawText("MIX",    874, 2, 50, 12, juce::Justification::centred);
        g.drawText("OUTPUT", 924, 2, 50, 12, juce::Justification::centred);

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

            const int labelY = card.getY() + 114;
            if (unit == BassRackUnit::TriComp)
            {
                g.setFont(ui::font(10.5f, true));
                g.setColour(juce::Colour(0xff90a4ae));
                g.drawText("RATIO", card.getX() + 12, card.getY() + 36, 70, 16, juce::Justification::centredLeft);
                g.drawText("HARMONICS", card.getX() + 110, card.getY() + 36, 90, 16, juce::Justification::centredLeft);
                g.drawText("DETECTOR", card.getX() + 208, card.getY() + 36, 80, 16, juce::Justification::centredLeft);
                g.drawText("ATTACK", card.getX() + 16, labelY, 80, 15, juce::Justification::centred);
                g.drawText("RELEASE", card.getX() + 112, labelY, 80, 15, juce::Justification::centred);
                g.drawText("LEVEL", card.getX() + 208, labelY, 80, 15, juce::Justification::centred);
            }
            else
            {
                g.setFont(ui::font(10.5f, true));
                g.setColour(juce::Colour(0xff90a4ae));
                if (b == 0)
                {
                    g.drawText("BUS COMP", card.getX() + 16, labelY, 80, 15, juce::Justification::centred);
                    g.drawText("CLEAN LPF", card.getX() + 112, labelY, 80, 15, juce::Justification::centred);
                    g.drawText("SUB LEVEL", card.getX() + 208, labelY, 80, 15, juce::Justification::centred);
                }
                else if (b == 1)
                {
                    g.drawText("CHARACTER", card.getX() + 12, card.getY() + 32, 95, 16, juce::Justification::centredLeft);
                    g.drawText("NEURAL CAPTURE", card.getX() + 140, card.getY() + 32, 130, 16, juce::Justification::centredLeft);
                    g.drawText("MID DRIVE", card.getX() + 16, labelY, 80, 15, juce::Justification::centred);
                    g.drawText("PRESENCE", card.getX() + 112, labelY, 80, 15, juce::Justification::centred);
                    g.drawText("MID LEVEL", card.getX() + 208, labelY, 80, 15, juce::Justification::centred);
                }
                else
                {
                    g.drawText("CHARACTER", card.getX() + 12, card.getY() + 32, 95, 16, juce::Justification::centredLeft);
                    g.drawText("NEURAL CAPTURE", card.getX() + 140, card.getY() + 32, 130, 16, juce::Justification::centredLeft);
                    g.drawText("HIGH DRIVE", card.getX() + 16, labelY, 80, 15, juce::Justification::centred);
                    g.drawText("PRE HPF", card.getX() + 112, labelY, 80, 15, juce::Justification::centred);
                    g.drawText("HIGH LEVEL", card.getX() + 208, labelY, 80, 15, juce::Justification::centred);
                }
            }
        }
    }

    void resized() override
    {
        enableToggle.setBounds(350, 12, 86, 28);
        presetBox.setBounds(442, 12, 210, 28);
        orderButton.setBounds(658, 12, 110, 28);

        inputKnob.setBounds(825, 13, 48, 37);
        mixKnob.setBounds(875, 13, 48, 37);
        outputKnob.setBounds(925, 13, 48, 37);

        spectrum.setBounds(14, 58, getWidth() - 28, 244);

        const auto lower = getLocalBounds().withTrimmedTop(308).reduced(14, 8);
        const int colW = (lower.getWidth() - 24) / 3;

        if (unit == BassRackUnit::TriComp)
        {
            linkKnob.setBounds(775, 13, 48, 37);
            audioHpfToggle.setBounds(lower.getX() + 12, lower.getY() + 84, 92, 24);
            for (int b = 0; b < 3; ++b)
            {
                juce::Rectangle<int> card(lower.getX() + b * (colW + 12), lower.getY(), colW, lower.getHeight());
                auto& col = compColumns[static_cast<size_t>(b)];
                col.soloBtn.setBounds(card.getRight() - 62, card.getY() + 7, 24, 22);
                col.muteBtn.setBounds(card.getRight() - 34, card.getY() + 7, 24, 22);

                col.ratioBox.setBounds(card.getX() + 12, card.getY() + 54, 92, 26);
                col.audioBox.setBounds(card.getX() + 110, card.getY() + 54, 92, 26);
                col.detectorBox.setBounds(card.getX() + 208, card.getY() + 54, 92, 26);

                const int knobY = card.getY() + 132;
                const int knobH = juce::jmax(80, card.getBottom() - knobY - 10);
                col.attackKnob.setBounds(card.getX() + 16, knobY, 80, knobH);
                col.releaseKnob.setBounds(card.getX() + 112, knobY, 80, knobH);
                col.levelKnob.setBounds(card.getX() + 208, knobY, 80, knobH);
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
                const int knobY = card.getY() + 132;
                const int knobH = juce::jmax(80, card.getBottom() - knobY - 10);

                if (b == 0)
                {
                    cabSimToggle.setBounds(card.getX() + 12, card.getY() + 38, 128, 26);
                    cabIrBtn.setBounds(card.getX() + 146, card.getY() + 38, 152, 26);
                    postEqToggle.setBounds(card.getX() + 12, card.getY() + 72, 128, 24);
                    for (int eqIdx = 0; eqIdx < 7; ++eqIdx)
                        postEqSliders[static_cast<size_t>(eqIdx)].setBounds(card.getX() + 146 + eqIdx * 22, card.getY() + 68, 20, 42);

                    lowCompKnob.setBounds(card.getX() + 16, knobY, 80, knobH);
                    lowLpfKnob.setBounds(card.getX() + 112, knobY, 80, knobH);
                    lowLevelKnob.setBounds(card.getX() + 208, knobY, 80, knobH);
                }
                else if (b == 1)
                {
                    midCharBox.setBounds(card.getX() + 12, card.getY() + 50, 120, 26);
                    midNamBtn.setBounds(card.getX() + 140, card.getY() + 50, 158, 26);
                    midDriveKnob.setBounds(card.getX() + 16, knobY, 80, knobH);
                    midPresenceKnob.setBounds(card.getX() + 112, knobY, 80, knobH);
                    midLevelKnob.setBounds(card.getX() + 208, knobY, 80, knobH);
                }
                else
                {
                    highCharBox.setBounds(card.getX() + 12, card.getY() + 50, 120, 26);
                    highNamBtn.setBounds(card.getX() + 140, card.getY() + 50, 158, 26);
                    highDriveKnob.setBounds(card.getX() + 16, knobY, 80, knobH);
                    highHpfKnob.setBounds(card.getX() + 112, knobY, 80, knobH);
                    highLevelKnob.setBounds(card.getX() + 208, knobY, 80, knobH);
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

    void configureHeaderKnob(juce::Slider& s, double minV, double maxV, double step, const char* suffix)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled(true, true, this);
        s.setRange(minV, maxV, step);
        s.setTextValueSuffix(suffix);
        s.onValueChange = [this] { if (!syncing) commitUiToProcessor(); };
    }

    void configureKnob(juce::Slider& s, double minV, double maxV, double step, const char* suffix, juce::Colour accent)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
        s.setColour(juce::Slider::rotarySliderFillColourId, accent);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0b141a));
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffdbe9f4));
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
                bp.ratio = static_cast<multiband::CompRatio>(juce::jlimit(0, 7, col.ratioBox.getSelectedId() - 1));
                bp.audioMode = static_cast<multiband::CompAudioMode>(juce::jlimit(0, 2, col.audioBox.getSelectedId() - 1));
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
    detail::BassRackLook rackLook;
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
 * quick macro knobs, bypass footswitch, preset selector, and a "SPECTRUM & BANDS"
 * button that launches the full-size interactive spectrum analyzer.
 */
class BassRackCard final : public juce::Component
{
public:
    BassRackCard(board::Processor& proc, BassRackUnit unitType)
        : processor(proc), unit(unitType), rackLook(unitType == BassRackUnit::TriDrive)
    {
        setLookAndFeel(&rackLook);
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
            const auto cyan = juce::Colour(0xff29b6f6);
            for (int i = 0; i < static_cast<int>(triComp::kPresets.size()); ++i)
                presetBox.addItem(triComp::kPresets[static_cast<size_t>(i)].name, i + 1);
            setupMacroKnob(macroKnobs[0], -24.0, 24.0, " dB", cyan);
            setupMacroKnob(macroKnobs[1], 0.0, 100.0, " %", cyan);
            setupMacroKnob(macroKnobs[2], -24.0, 24.0, " dB", cyan);
        }
        else
        {
            for (int i = 0; i < static_cast<int>(triDrive::kPresets.size()); ++i)
                presetBox.addItem(triDrive::kPresets[static_cast<size_t>(i)].name, i + 1);
            setupMacroKnob(macroKnobs[0], 0.0, 10.0, "", detail::bandColour(0));
            setupMacroKnob(macroKnobs[1], 0.0, 10.0, "", detail::bandColour(1));
            setupMacroKnob(macroKnobs[2], 0.0, 10.0, "", detail::bandColour(2));
        }

        openEditorBtn.setButtonText("SPECTRUM & BANDS");
        openEditorBtn.onClick = [this] { if (onOpenEditor) onOpenEditor(unit); };
        addAndMakeVisible(openEditorBtn);

        sync();
    }

    ~BassRackCard() override
    {
        setLookAndFeel(nullptr);
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

    juce::Rectangle<int> displayBounds() const noexcept
    {
        const int w = getWidth();
        const int h = getHeight();
        const int dispH = juce::jlimit(58, 92, int(h * 0.30f));
        return { 20, 44, juce::jmax(40, w - 40), dispH };
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(2.0f);
        const bool isComp = (unit == BassRackUnit::TriComp);
        const bool active = isComp ? processor.getTriComp().enabled : processor.getTriDrive().enabled;
        const auto accent = isComp ? juce::Colour(0xff29b6f6) : juce::Colour(0xffff8a3d);

        // Card chassis with drop shadow and brushed rack faceplate gradient
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff172630), 0.0f, area.getY(),
                                               juce::Colour(0xff0b1318), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle(area, 9.0f);

        // Hardware rack ears on left/right rails
        for (float railX : { area.getX() + 3.0f, area.getRight() - 12.0f })
        {
            juce::Rectangle<float> ear(railX, area.getY() + 6.0f, 9.0f, area.getHeight() - 12.0f);
            g.setColour(juce::Colour(0xff0d171e));
            g.fillRoundedRectangle(ear, 3.0f);
            for (float sy : { ear.getY() + 14.0f, ear.getBottom() - 14.0f })
            {
                g.setColour(juce::Colour(0xff2d404d));
                g.fillEllipse(ear.getCentreX() - 3.0f, sy - 3.0f, 6.0f, 6.0f);
                g.setColour(juce::Colour(0xff080f14));
                g.drawLine(ear.getCentreX() - 2.0f, sy, ear.getCentreX() + 2.0f, sy, 1.0f);
            }
        }

        g.setColour(active ? accent.withAlpha(0.85f) : juce::Colour(0xff293d4b));
        g.drawRoundedRectangle(area, 9.0f, active ? 1.8f : 1.1f);

        // Status LED + Title & Subtitle
        const juce::Rectangle<float> led(22.0f, 15.0f, 8.0f, 8.0f);
        if (active)
        {
            g.setColour(accent.withAlpha(0.30f));
            g.fillEllipse(led.expanded(3.0f));
        }
        g.setColour(active ? accent : juce::Colour(0xff344652));
        g.fillEllipse(led);

        g.setFont(ui::font(15.5f, true));
        g.setColour(juce::Colours::white);
        g.drawText(isComp ? "TRI-COMP" : "TRI-DRIVE", 36, 9, 140, 20, juce::Justification::centredLeft);

        g.setFont(ui::font(10.0f, true));
        g.setColour(accent);
        g.drawText(isComp ? "3-BAND COMPRESSOR" : "3-BAND DISTORTION", 36, 26, 195, 14, juce::Justification::centredLeft);

        // Mini Crossover & Band GR/Drive Display
        const auto display = displayBounds().toFloat();
        g.setColour(juce::Colour(0xff060c11));
        g.fillRoundedRectangle(display, 6.0f);

        // Subtle grid lines inside display
        g.setColour(juce::Colour(0xff13222d));
        for (float hz : { 100.0f, 1000.0f, 5000.0f })
        {
            const float gx = display.getX() + detail::freqToNormX(hz) * display.getWidth();
            g.drawVerticalLine(juce::roundToInt(gx), display.getY() + 1.0f, display.getBottom() - 1.0f);
        }
        g.drawHorizontalLine(juce::roundToInt(display.getCentreY()), display.getX() + 1.0f, display.getRight() - 1.0f);

        const float f1 = isComp ? processor.getTriComp().lowMidHz : processor.getTriDrive().lowMidHz;
        const float f2 = isComp ? processor.getTriComp().midHighHz : processor.getTriDrive().midHighHz;
        const float x1 = display.getX() + detail::freqToNormX(f1) * display.getWidth();
        const float x2 = display.getX() + detail::freqToNormX(f2) * display.getWidth();
        const std::array<float, 4> edges{{ display.getX(), x1, x2, display.getRight() }};
        const char* bandNames[3] = { "LOW", "MID", "HIGH" };

        for (int b = 0; b < 3; ++b)
        {
            juce::Rectangle<float> seg(edges[static_cast<size_t>(b)], display.getY(),
                                       edges[static_cast<size_t>(b + 1)] - edges[static_cast<size_t>(b)], display.getHeight());
            const auto bCol = detail::bandColour(b);
            g.setColour(bCol.withAlpha(active ? 0.16f : 0.05f));
            g.fillRect(seg);

            // Band name badge in top-left of segment
            g.setFont(ui::font(9.5f, true));
            g.setColour(bCol.withAlpha(0.85f));
            g.drawText(bandNames[b], seg.toNearestInt().reduced(6, 4).removeFromTop(13), juce::Justification::centredLeft);

            // Primary band summary in center
            g.setFont(ui::font(11.5f, true));
            g.setColour(juce::Colours::white.withAlpha(active ? 0.95f : 0.55f));
            float barNorm = 0.0f;
            if (isComp)
            {
                const auto bp = processor.getTriComp().bands[static_cast<size_t>(b)];
                const float gr = processor.triCompProcessor().readMeters().grDb[static_cast<size_t>(b)];
                const juce::String modeStr = triComp::audioModeName(bp.audioMode);
                g.drawText(juce::String(triComp::ratioName(bp.ratio)) + "  " + modeStr,
                           seg.toNearestInt().reduced(4, 2), juce::Justification::centred);
                barNorm = juce::jlimit(0.0f, 1.0f, gr / 18.0f);
                g.setFont(ui::font(9.5f));
                g.setColour(bCol);
                g.drawText("-" + juce::String(gr, 1) + " dB",
                           seg.toNearestInt().reduced(6, 4).removeFromTop(13), juce::Justification::centredRight);
            }
            else
            {
                const auto p = processor.getTriDrive();
                juce::String summary;
                if (b == 0)
                {
                    summary = "COMP " + juce::String(p.lowComp, 1);
                    barNorm = juce::jlimit(0.0f, 1.0f, p.lowComp * 0.1f);
                }
                else if (b == 1)
                {
                    summary = juce::String(triDrive::characterName(p.midCharacter)) + "  " + juce::String(p.midDrive, 1);
                    barNorm = juce::jlimit(0.0f, 1.0f, p.midDrive * 0.1f);
                }
                else
                {
                    summary = juce::String(triDrive::characterName(p.highCharacter)) + "  " + juce::String(p.highDrive, 1);
                    barNorm = juce::jlimit(0.0f, 1.0f, p.highDrive * 0.1f);
                }
                g.drawText(summary, seg.toNearestInt().reduced(4, 2), juce::Justification::centred);
            }

            // Bottom activity/GR meter bar inside segment
            auto meterTrack = seg.reduced(6.0f, 0.0f).removeFromBottom(7.0f).withTrimmedBottom(3.0f);
            g.setColour(juce::Colour(0xff101d26));
            g.fillRoundedRectangle(meterTrack, 2.0f);
            if (barNorm > 0.01f)
            {
                g.setColour(bCol);
                g.fillRoundedRectangle(meterTrack.withWidth(meterTrack.getWidth() * barNorm), 2.0f);
            }
        }

        // Crossover vertical lines & frequency pills
        for (int c = 0; c < 2; ++c)
        {
            const float cx = (c == 0) ? x1 : x2;
            const float hz = (c == 0) ? f1 : f2;
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.drawVerticalLine(juce::roundToInt(cx), display.getY(), display.getBottom());

            juce::Rectangle<float> pill(cx - 26.0f, display.getBottom() - 18.0f, 52.0f, 14.0f);
            g.setColour(juce::Colour(0xe00b161e));
            g.fillRoundedRectangle(pill, 3.5f);
            g.setColour(juce::Colour(0xff385468));
            g.drawRoundedRectangle(pill, 3.5f, 0.8f);
            g.setFont(ui::font(9.0f, true));
            g.setColour(juce::Colour(0xffd0e2ee));
            g.drawText(detail::formatHz(hz), pill.toNearestInt(), juce::Justification::centred);
        }

        g.setColour(juce::Colour(0xff233847));
        g.drawRoundedRectangle(display, 6.0f, 1.0f);

        // Knob labels cleanly ABOVE each rotary knob so they never collide with the value readout below
        const char* labelsComp[3] = { "INPUT", "MIX", "OUTPUT" };
        const char* labelsDrive[3] = { "LOW COMP", "MID DRIVE", "HIGH DRIVE" };
        g.setFont(ui::font(10.5f, true));
        for (size_t i = 0; i < 3; ++i)
        {
            const auto kb = macroKnobs[i].getBounds();
            g.setColour(isComp ? juce::Colour(0xffa7bcc9) : detail::bandColour(static_cast<int>(i)));
            g.drawText(isComp ? labelsComp[i] : labelsDrive[i],
                       kb.getX() - 12, kb.getY() - 15, kb.getWidth() + 24, 15, juce::Justification::centred);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (displayBounds().contains(e.getPosition()) && onOpenEditor)
            onOpenEditor(unit);
    }

    void resized() override
    {
        const int w = getWidth();
        const int h = getHeight();

        bypassBtn.setBounds(w - 82, 10, 62, 26);
        openEditorBtn.setBounds(w - 226, 10, 138, 26);

        const auto disp = displayBounds();
        presetBox.setBounds(20, disp.getBottom() + 6, w - 40, 24);

        const int knobTop = presetBox.getBottom() + 19;
        const int knobH = juce::jmax(44, h - knobTop - 6);
        const int knobW = juce::jmin(84, knobH + 8);
        const int colW = (w - 40) / 3;
        for (int i = 0; i < 3; ++i)
            macroKnobs[static_cast<size_t>(i)].setBounds(20 + i * colW + (colW - knobW) / 2, knobTop, knobW, knobH);
    }

private:
    void setupMacroKnob(juce::Slider& s, double minV, double maxV, const char* suffix, juce::Colour accent)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 15);
        s.setColour(juce::Slider::rotarySliderFillColourId, accent);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff081016));
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffdbe9f4));
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
    detail::BassRackLook rackLook;
    bool syncing = false;
    juce::TextButton bypassBtn, openEditorBtn;
    juce::ComboBox presetBox;
    std::array<juce::Slider, 3> macroKnobs;
};

} // namespace tonegrid::play
