#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
namespace board {
inline constexpr std::array<float,7> eqFrequencies{100,200,400,800,1600,3200,6400};
struct EqPreset { const char* name; std::array<float,7> bands; };
inline constexpr std::array<EqPreset,8> eqPresets{{
    {"01 Flat",{0,0,0,0,0,0,0}}, {"02 Mid push",{-2,-1,1,4,2,0,-1}},
    {"03 Tight rhythm",{-6,-3,0,1,2,1,-2}}, {"04 Warm lead",{0,1,2,3,1,-2,-4}},
    {"05 Scoop",{2,1,-2,-4,-2,1,2}}, {"06 Bright clean",{-2,-1,0,0,1,2,3}},
    {"07 Tame bite",{0,0,1,1,0,-3,-5}}, {"08 Hollowbody",{-5,-3,-1,1,2,1,-1}}
}};

// Seven fixed-frequency, constant-Q peaking sections. RBJ Audio EQ Cookbook
// coefficients; control ramps and updates every 16 samples, no audio allocation.
class GraphicEq
{
    struct Band
    {
        double cosine=0,alpha=0,b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0,last=999;
        juce::SmoothedValue<double> db;
        void coefficients(double value)
        {
            if(value==last)return;last=value;
            const double A=std::pow(10.0,value/40),den=1+alpha/A;
            b0=(1+alpha*A)/den;b1=-2*cosine/den;b2=(1-alpha*A)/den;
            a1=b1;a2=(1-alpha/A)/den;
        }
        float process(float input)
        {
            const double y=last==0?input:b0*input+b1*x1+b2*x2-a1*y1-a2*y2;
            x2=x1;x1=input;y2=y1;y1=y;return (float)y;
        }
    };
public:
    void prepare(double sr,const std::array<float,7>& values){prepare(sr,values,eqFrequencies);}
    // Bass mode supplies its own centre frequencies (tonegrid::bassEqFrequencies).
    void prepare(double sr,const std::array<float,7>& values,const std::array<float,7>& frequencies)
    {
        tick=0;
        for(size_t i=0;i<bands.size();++i){auto& b=bands[i];b=Band{};const double w=2*juce::MathConstants<double>::pi*juce::jlimit(10.0,sr*.45,(double)frequencies[i])/sr;b.cosine=std::cos(w);b.alpha=std::sin(w)/(2*1.41421356237);b.db.reset(sr,.02);b.db.setCurrentAndTargetValue(values[i]);b.coefficients(values[i]);}
    }
    void set(const std::array<float,7>& values){for(size_t i=0;i<bands.size();++i)bands[i].db.setTargetValue(values[i]);}
    void reset(){for(auto& b:bands)b.x1=b.x2=b.y1=b.y2=0;}
    float process(float input)
    {
        for(auto& b:bands){const double value=b.db.getNextValue();if(tick==0)b.coefficients(value);input=b.process(input);}
        tick=(tick+1)%16;return input;
    }
private:
    std::array<Band,7> bands;int tick=0;
};
}
