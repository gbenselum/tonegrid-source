#pragma once
#include "Modules/Play/UI/PedalCard.h"
#include "Modules/Play/UI/PedalScene.h"
#include "Modules/Play/UI/RecordingControls.h"
#include "Modules/Play/UI/LoopControls.h"
#include "Modules/Play/UI/BassRackView.h"
namespace tonegrid::play {
class View final : public juce::Component {
public:
    View(recording::SessionRecorder& recorder, looping::Engine& looper)
        : recordingControls(recorder), loopControls(looper) {
        addAndMakeVisible(recordingControls);addAndMakeVisible(loopControls);
    }
    void paint(juce::Graphics& g) override { assets::paintPedalScene(g, getLocalBounds()); }
    void paintOverChildren(juce::Graphics& g) override {
        if(hintOpacity<=0)return;
        const auto actions=actionBounds();
        const std::array<const char*,4> labels{"Play","Record","Download","Looper"};
        g.setFont(ui::font(12));g.setColour(juce::Colours::white.withAlpha(hintOpacity));
        for(size_t i=0;i<actions.size();++i){const auto r=actions[i];g.drawText(labels[i],juce::Rectangle<int>(r.getRight()+8,r.getY(),juce::jmax(0,getWidth()-r.getRight()-10),r.getHeight()),juce::Justification::centredLeft);}
    }
    void setHintOpacity(float opacity){if(hintOpacity!=opacity){hintOpacity=opacity;repaint();}}
    void setInstrumentMode(InstrumentMode mode, bool playActive = true)
    {
        instrumentMode = mode;
        const bool bass = isBass(mode);
        for (int v = 0; v < board::count; ++v)
        {
            if (!cards[size_t(v)]) continue;
            const int pid = board::order[size_t(v)];
            const bool showPedal = playActive && (!bass || !isPedalRemovedInBassMode(pid));
            cards[size_t(v)]->setVisible(showPedal);
        }
        if (triCompCard) triCompCard->setVisible(playActive && bass);
        if (triDriveCard) triDriveCard->setVisible(playActive && bass);
        resized();
        repaint();
    }
    InstrumentMode getInstrumentMode() const noexcept { return instrumentMode; }
    std::array<juce::Rectangle<int>,4> actionBounds()const {
        const auto r=recordingControls.actionBounds();const auto p=recordingControls.getPosition();
        return {r[0].translated(p.x,p.y),r[1].translated(p.x,p.y),r[2].translated(p.x,p.y),loopControls.getBounds().withSizeKeepingCentre(juce::jmin(46,loopControls.getWidth()),juce::jmin(46,loopControls.getHeight()))};
    }
    void resized() override {
        const auto area = getLocalBounds();
        const auto bounds = assets::boardBounds(area);
        if (!isBass(instrumentMode))
        {
            for (int i=0;i<board::count;++i) if (cards[size_t(i)]) cards[size_t(i)]->setBounds(bounds[size_t(i)]);
        }
        else
        {
            // Bass mode layout:
            // Row 1 (upper): Wide TRI-COMP and TRI-DRIVE rack units side-by-side
            // Row 2 (lower): The 5 kept bass stompboxes (ClassicFuzz=0, AutoWah=2, VintageFuzz=10, GraphicEq=11, CleanBoost=12)
            constexpr int rowGap = 12;
            const int cellHeight = juce::jmax(1, (area.getHeight() - rowGap) / 2);
            const int rackH = juce::jmax(1, cellHeight - 4);
            const int rackW = juce::jmin((area.getWidth() - 40) / 2, int(rackH * 2.35f));
            const int totalTopW = rackW * 2 + 18;
            const int topX = area.getX() + (area.getWidth() - totalTopW) / 2;
            const int topY = area.getY() + (cellHeight - rackH) / 2;

            if (triCompCard)  triCompCard->setBounds(topX, topY, rackW, rackH);
            if (triDriveCard) triDriveCard->setBounds(topX + rackW + 18, topY, rackW, rackH);

            // Lower row: center the 5 kept stompboxes using their native aspect ratios from bounds
            const std::array<int, 5> keptVisualIndices{{ 0, 2, 10, 11, 12 }};
            constexpr int pedalGap = 18;
            int totalBotW = 0;
            for (int v : keptVisualIndices)
                totalBotW += bounds[size_t(v)].getWidth();
            totalBotW += 4 * pedalGap;
            int botX = area.getX() + (area.getWidth() - totalBotW) / 2;
            const int botY = bounds[7].getY();
            for (int v : keptVisualIndices)
            {
                const int w = bounds[size_t(v)].getWidth();
                const int h = bounds[size_t(v)].getHeight();
                if (cards[size_t(v)])
                    cards[size_t(v)]->setBounds(botX, botY, w, h);
                botX += w + pedalGap;
            }
        }
        const auto last=bounds[12];
        const int width=juce::jmax(1,juce::jmin(64,area.getRight()-last.getRight()-12));
        recordingControls.setBounds(last.getRight()+8,last.getCentreY()-118,width,184);
        loopControls.setBounds(last.getRight()+8,last.getCentreY()+74,width,46);
    }
    std::array<std::unique_ptr<PedalCard>,board::count> cards;
    std::unique_ptr<BassRackCard> triCompCard;
    std::unique_ptr<BassRackCard> triDriveCard;
    recording::Controls recordingControls;
    looping::Controls loopControls;
private:
    InstrumentMode instrumentMode = InstrumentMode::Guitar;
    float hintOpacity=0;
};
}
