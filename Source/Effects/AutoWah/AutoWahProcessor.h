#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
namespace board {
struct WahPreset { const char* name; float sensitivity, range, decay; };
inline constexpr std::array<WahPreset,8> wahPresets{{
    {"01 Classic funk",55,75,160},{"02 Soft soul",48,55,300},
    {"03 Fast quack",65,65,70},{"04 Wide sweep",65,100,220},
    {"05 Dark vowel",60,30,280},{"06 Reverse funk",55,80,240},
    {"07 Single coils",75,70,180},{"08 Humbuckers",35,65,180}
}};
// Original ToneGrid envelope filter. A resonant TPT SVF combines lowpass body
// with a Q-normalised bandpass vocal peak. No borrowed effect implementation.
// Peak detector -> smoothed sweep -> filter -> fixed makeup -> soft ceiling.
class AutoWah
{
    struct Voice { double q, band, bottom, attackMs; bool reverse; };
    static constexpr std::array<Voice,8> voices{{
        {3.8,1.25,260,1.5,false},{2.2,.65,320,4,false},
        {4.8,1.5,350,.8,false},{3.6,1.15,180,2,false},
        {4.2,1.3,180,3,false},{3.8,1.25,260,1.5,true},
        {3.4,1.1,280,1.5,false},{3.2,1.0,240,2,false}
    }};
public:
    void prepare(double sampleRate,float sensitivity,float range,float decay,int preset=0)
    {
        sr=sampleRate; reset();
        for(auto* s:{&sens,&span,&release})s->reset(sr,.02);
        sens.setCurrentAndTargetValue(sensitivity);span.setCurrentAndTargetValue(range);release.setCurrentAndTargetValue(decay);
        for(auto* s:{&q,&band,&bottom,&direction,&attackMs})s->reset(sr,.02);
        setVoice(preset,true);
        detectorPole=std::exp(-2*juce::MathConstants<double>::pi*35/sr);
        sweepPole=std::exp(-1.0/(sr*.002));
    }
    // Bass mode moves the sweep floor down an octave so the vowel lands on
    // bass fundamentals. Guitar mode (default) uses the original voices.
    void setBassMode(bool value){if(value==bassMode)return;bassMode=value;setVoice(voice,false);}
    bool isBassMode()const{return bassMode;}
    void reset(){envelope=ic1=ic2=detectorLow=sweep=0;frequency=260;}
    void set(float sensitivity,float range,float decay,int preset=0)
    {sens.setTargetValue(sensitivity);span.setTargetValue(range);release.setTargetValue(decay);if(preset!=voice)setVoice(preset,false);}
    float process(float x)
    {
        // Reject DC/subsonic thumps in the detector, without filtering audio.
        detectorLow=detectorPole*detectorLow+(1-detectorPole)*x;
        const double detectorGain=juce::Decibels::decibelsToGain(-6.0+sens.getNextValue()*.34);
        const double decay=std::exp(-1.0/(sr*release.getNextValue()*.001));
        const double attack=std::exp(-1.0/(sr*attackMs.getNextValue()*.001));
        const double level=std::abs(x-detectorLow);const double c=level>envelope?attack:decay;
        envelope=c*envelope+(1-c)*level;
        const double amount=std::sqrt(juce::jlimit(0.0,1.0,envelope*detectorGain));
        sweep=sweepPole*sweep+(1-sweepPole)*amount;
        const double position=sweep+(1-2*sweep)*direction.getNextValue();
        const double low=bottom.getNextValue(),top=juce::jmin(sr*.35,700+span.getNextValue()*33);
        frequency=low*std::pow(top/low,position);
        const double g=std::tan(juce::MathConstants<double>::pi*frequency/sr),k=1.0/q.getNextValue();
        const double a1=1/(1+g*(g+k)),a2=g*a1,a3=g*a2;
        const double v3=x-ic2,v1=a1*ic1+a2*v3,v2=ic2+a2*ic1+a3*v3;
        ic1=2*v1-ic1;ic2=2*v2-ic2;
        // LP keeps the low-frequency body; BP emphasises the moving vowel.
        // Q-normalisation keeps the added BP energy consistent across voices.
        const double compensated=(.88*v2+band.getNextValue()*k*v1)*1.12;
        constexpr double knee=.8;
        const double magnitude=std::abs(compensated);
        return (float)(magnitude<=knee?compensated:std::copysign(knee+(1-knee)*std::tanh((magnitude-knee)/(1-knee)),compensated));
    }
    double currentFrequency() const{return frequency;}
private:
    void setVoice(int preset,bool immediate)
    {
        voice=juce::jlimit(0,7,preset);const auto& v=voices[(size_t)voice];
        auto assign=[&](auto& s,double value){if(immediate)s.setCurrentAndTargetValue(value);else s.setTargetValue(value);};
        assign(q,v.q);assign(band,v.band);assign(bottom,bassMode?v.bottom*.5:v.bottom);assign(direction,v.reverse?1.0:0.0);assign(attackMs,v.attackMs);
    }
    int voice=0;bool bassMode=false;
    double sr=48000,envelope=0,ic1=0,ic2=0,frequency=260,detectorLow=0,detectorPole=0,sweep=0,sweepPole=0;
    juce::SmoothedValue<double> sens,span,release,q,band,bottom,direction,attackMs;
};

}
