#include "DynBand.h"

static BiquadCoeffs makeBandpassAnalysis(double f0, double Q, double fs)
{
    double w0    = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    double cw    = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * Q);
    double a0    = 1.0 + alpha;
    return { sw * 0.5 / a0, 0.0, -sw * 0.5 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
}

void DynBand::prepare(float centreFreq, float q, float threshDB,
                      float attackMs, float releaseMs, float ratio,
                      double sampleRate) noexcept
{
    float f0 = juce::jlimit(20.f, (float)(sampleRate * 0.499), centreFreq);
    // Use wider Q for analysis so we catch energy around the band
    double bpQ = juce::jmax(0.3, (double)q * 0.7);
    analysisCoeffs = makeBandpassAnalysis((double)f0, bpQ, sampleRate);

    auto makeCoeff = [&](float ms) -> float {
        return ms > 0.f ? std::exp(-1.f / (ms * 0.001f * (float)sampleRate)) : 0.f;
    };

    attackCoeff  = makeCoeff(juce::jmax(0.1f, attackMs));
    releaseCoeff = makeCoeff(juce::jmax(1.f,  releaseMs));
    threshDB_    = threshDB;
    ratio_       = juce::jmax(1.001f, ratio);

    reset();
}

float DynBand::compute(float sideL, float sideR) noexcept
{
    float anaL = std::abs((float)analysisL.process((double)sideL, analysisCoeffs));
    float anaR = std::abs((float)analysisR.process((double)sideR, analysisCoeffs));
    float peak = std::max(anaL, anaR);

    if (peak > envLevel)
        envLevel = attackCoeff  * envLevel + (1.f - attackCoeff)  * peak;
    else
        envLevel = releaseCoeff * envLevel + (1.f - releaseCoeff) * peak;

    float levelDB  = 20.f * std::log10(std::max(envLevel, 1e-10f));
    float excessDB = levelDB - threshDB_;
    if (excessDB <= 0.f) return 0.f;

    // Smooth engagement over a 12 dB knee, scaled by ratio
    float blend = excessDB * ratio_ / 12.f;
    return juce::jlimit(0.f, 1.f, blend);
}
