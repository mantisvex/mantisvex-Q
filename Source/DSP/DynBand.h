#pragma once
#include "EQBand.h"

// Per-band dynamic EQ processor.
// Analyzes signal level in the band's frequency range via a bandpass envelope
// follower and returns a blend factor [0..1] that scales the band's effect.
// 0 = band silent (below threshold), 1 = band at full static gain.
class DynBand
{
public:
    void prepare(float centreFreq, float q, float threshDB,
                 float attackMs, float releaseMs, float ratio,
                 double sampleRate) noexcept;

    // Feed pre-EQ stereo sample, returns blend factor for this sample.
    float compute(float sideL, float sideR) noexcept;

    void reset() noexcept
    {
        envLevel = 0.f;
        analysisL.reset();
        analysisR.reset();
    }

private:
    BiquadState  analysisL, analysisR;
    BiquadCoeffs analysisCoeffs;
    float envLevel     = 0.f;
    float attackCoeff  = 0.f;
    float releaseCoeff = 0.f;
    float threshDB_    = -18.f;
    float ratio_       = 4.f;
};
