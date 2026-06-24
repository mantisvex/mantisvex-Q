#pragma once

#include <JuceHeader.h>
#include <array>
#include <complex>

enum class FilterType
{
    Bell      = 0,
    LowShelf  = 1,
    HighShelf = 2,
    LowCut    = 3,
    HighCut   = 4,
    Notch     = 5,
    BandPass  = 6,
    AllPass   = 7,
    TiltShelf = 8,  // 2-biquad cascade: HighShelf(+g/2) + LowShelf(-g/2)
    Count     = 9
};

enum class ChannelMode
{
    Stereo = 0,
    Left   = 1,
    Right  = 2,
    Mid    = 3,
    Side   = 4,
    Count  = 5
};

static constexpr int kMaxCascade = 4;

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

struct BiquadState
{
    double s1 = 0.0, s2 = 0.0;

    inline double process(double x, const BiquadCoeffs& c) noexcept
    {
        double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void reset() noexcept { s1 = s2 = 0.0; }
};

struct EQBandParams
{
    float      freq     = 1000.0f;
    float      gainDB   = 0.0f;
    float      q        = 0.707f;
    FilterType type     = FilterType::Bell;
    int        order    = 1;     // 1=12dB, 2=24dB, 3=36dB, 4=48dB/oct
    bool       enabled  = true;
    bool       bypassed = false;
};

// Returns true for filter types where gainDB has no effect
inline bool filterTypeIgnoresGain(FilterType t)
{
    return t == FilterType::LowCut  || t == FilterType::HighCut  ||
           t == FilterType::Notch   || t == FilterType::BandPass ||
           t == FilterType::AllPass;
}

class EQBand
{
public:
    void setParams(const EQBandParams& p, double sampleRate);
    void reset() noexcept;

    inline float processSampleL(float x) noexcept
    {
        double y = static_cast<double>(x);
        for (int i = 0; i < numBiquads; ++i)
            y = stateL[i].process(y, coeffs[i]);
        return static_cast<float>(y);
    }

    inline float processSampleR(float x) noexcept
    {
        double y = static_cast<double>(x);
        for (int i = 0; i < numBiquads; ++i)
            y = stateR[i].process(y, coeffs[i]);
        return static_cast<float>(y);
    }

    std::complex<double> getFrequencyResponse(double freq, double sampleRate) const noexcept;
    const EQBandParams& getParams() const noexcept { return params; }

private:
    void computeCoefficients(double sampleRate);

    static BiquadCoeffs makeBell      (double f0, double dBgain, double Q, double fs);
    static BiquadCoeffs makeLowShelf  (double f0, double dBgain, double Q, double fs);
    static BiquadCoeffs makeHighShelf (double f0, double dBgain, double Q, double fs);
    static BiquadCoeffs makeLowPass   (double f0, double Q, double fs);
    static BiquadCoeffs makeHighPass  (double f0, double Q, double fs);
    static BiquadCoeffs makeNotch     (double f0, double Q, double fs);
    static BiquadCoeffs makeBandPass  (double f0, double Q, double fs);
    static BiquadCoeffs makeAllPass   (double f0, double Q, double fs);

    EQBandParams params;
    int numBiquads = 1;
    std::array<BiquadCoeffs, kMaxCascade> coeffs;
    std::array<BiquadState,  kMaxCascade> stateL, stateR;
};
