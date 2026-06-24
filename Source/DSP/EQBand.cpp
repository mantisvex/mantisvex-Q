#include "EQBand.h"

static constexpr double kPi = juce::MathConstants<double>::pi;
// Cascaded biquad Q values for maximally-flat (Butterworth) response.
// Row i → (i+2) biquads: row 0=4th order (24dB), row 1=6th (36dB), row 2=8th (48dB).
static constexpr double kButterworthQ[3][4] = {
    { 0.5412, 1.3066, 0.0,    0.0    },
    { 0.5176, 0.7071, 1.9319, 0.0    },
    { 0.5098, 0.6013, 0.8999, 2.5629 },
};

void EQBand::setParams(const EQBandParams& p, double sampleRate)
{
    params = p;
    computeCoefficients(sampleRate);
}

void EQBand::reset() noexcept
{
    for (int i = 0; i < kMaxCascade; ++i) { stateL[i].reset(); stateR[i].reset(); }
}

void EQBand::computeCoefficients(double sampleRate)
{
    const double freq = juce::jlimit(10.0, sampleRate * 0.499, static_cast<double>(params.freq));
    const double gain = static_cast<double>(params.gainDB);
    const double q    = juce::jlimit(0.01, 40.0, static_cast<double>(params.q));
    numBiquads = 1;

    switch (params.type)
    {
        case FilterType::Bell:
            coeffs[0] = makeBell(freq, gain, q, sampleRate);
            break;

        case FilterType::LowShelf:
            coeffs[0] = makeLowShelf(freq, gain, q, sampleRate);
            break;

        case FilterType::HighShelf:
            coeffs[0] = makeHighShelf(freq, gain, q, sampleRate);
            break;

        case FilterType::LowCut:
        {
            numBiquads = juce::jlimit(1, kMaxCascade, params.order);
            for (int i = 0; i < numBiquads; ++i)
            {
                double qi = (numBiquads == 1) ? q : kButterworthQ[numBiquads - 2][i];
                coeffs[i] = makeHighPass(freq, qi, sampleRate);
            }
            break;
        }

        case FilterType::HighCut:
        {
            numBiquads = juce::jlimit(1, kMaxCascade, params.order);
            for (int i = 0; i < numBiquads; ++i)
            {
                double qi = (numBiquads == 1) ? q : kButterworthQ[numBiquads - 2][i];
                coeffs[i] = makeLowPass(freq, qi, sampleRate);
            }
            break;
        }

        case FilterType::Notch:
            coeffs[0] = makeNotch(freq, q, sampleRate);
            break;

        case FilterType::BandPass:
            coeffs[0] = makeBandPass(freq, q, sampleRate);
            break;

        case FilterType::AllPass:
            coeffs[0] = makeAllPass(freq, q, sampleRate);
            break;

        case FilterType::TiltShelf:
            // Cascade: HighShelf(+gain/2) boosts above freq, LowShelf(-gain/2) cuts below
            numBiquads = 2;
            coeffs[0] = makeHighShelf(freq,  gain * 0.5, q, sampleRate);
            coeffs[1] = makeLowShelf (freq, -gain * 0.5, q, sampleRate);
            break;

        default:
            coeffs[0] = BiquadCoeffs{};
            break;
    }
}

BiquadCoeffs EQBand::makeBell(double f0, double dBgain, double Q, double fs)
{
    double w0 = 2.0 * kPi * f0 / fs, cw = std::cos(w0), sw = std::sin(w0);
    double A  = std::pow(10.0, dBgain / 40.0);
    double alpha = sw / (2.0 * Q);
    double a0 = 1.0 + alpha / A;
    return { (1.0 + alpha * A)/a0, (-2.0*cw)/a0, (1.0 - alpha*A)/a0,
             (-2.0*cw)/a0, (1.0 - alpha/A)/a0 };
}

BiquadCoeffs EQBand::makeLowShelf(double f0, double dBgain, double Q, double fs)
{
    double w0 = 2.0 * kPi * f0 / fs, cw = std::cos(w0), sw = std::sin(w0);
    double A  = std::pow(10.0, dBgain / 40.0), sqA = std::sqrt(A);
    double S  = juce::jlimit(0.1, 10.0, 1.0 / (2.0 * Q * Q));
    double alpha = sw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/S - 1.0) + 2.0);
    double a0 = (A+1.0) + (A-1.0)*cw + 2.0*sqA*alpha;
    return { A*((A+1.0)-(A-1.0)*cw+2.0*sqA*alpha)/a0,
             A*2.0*((A-1.0)-(A+1.0)*cw)/a0,
             A*((A+1.0)-(A-1.0)*cw-2.0*sqA*alpha)/a0,
             -2.0*((A-1.0)+(A+1.0)*cw)/a0,
             ((A+1.0)+(A-1.0)*cw-2.0*sqA*alpha)/a0 };
}

BiquadCoeffs EQBand::makeHighShelf(double f0, double dBgain, double Q, double fs)
{
    double w0 = 2.0 * kPi * f0 / fs, cw = std::cos(w0), sw = std::sin(w0);
    double A  = std::pow(10.0, dBgain / 40.0), sqA = std::sqrt(A);
    double S  = juce::jlimit(0.1, 10.0, 1.0 / (2.0 * Q * Q));
    double alpha = sw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/S - 1.0) + 2.0);
    double a0 = (A+1.0) - (A-1.0)*cw + 2.0*sqA*alpha;
    return { A*((A+1.0)+(A-1.0)*cw+2.0*sqA*alpha)/a0,
            -A*2.0*((A-1.0)+(A+1.0)*cw)/a0,
             A*((A+1.0)+(A-1.0)*cw-2.0*sqA*alpha)/a0,
             2.0*((A-1.0)-(A+1.0)*cw)/a0,
             ((A+1.0)-(A-1.0)*cw-2.0*sqA*alpha)/a0 };
}

BiquadCoeffs EQBand::makeLowPass(double f0, double Q, double fs)
{
    double w0 = 2.0*kPi*f0/fs, cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0*Q), a0 = 1.0 + alpha;
    return { (1.0-cw)/2.0/a0, (1.0-cw)/a0, (1.0-cw)/2.0/a0, -2.0*cw/a0, (1.0-alpha)/a0 };
}

BiquadCoeffs EQBand::makeHighPass(double f0, double Q, double fs)
{
    double w0 = 2.0*kPi*f0/fs, cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0*Q), a0 = 1.0 + alpha;
    return { (1.0+cw)/2.0/a0, -(1.0+cw)/a0, (1.0+cw)/2.0/a0, -2.0*cw/a0, (1.0-alpha)/a0 };
}

BiquadCoeffs EQBand::makeNotch(double f0, double Q, double fs)
{
    double w0 = 2.0*kPi*f0/fs, cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0*Q), a0 = 1.0 + alpha;
    return { 1.0/a0, -2.0*cw/a0, 1.0/a0, -2.0*cw/a0, (1.0-alpha)/a0 };
}

BiquadCoeffs EQBand::makeBandPass(double f0, double Q, double fs)
{
    double w0 = 2.0*kPi*f0/fs, cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0*Q), a0 = 1.0 + alpha;
    // Constant 0dB peak gain version
    return { sw*0.5/a0, 0.0, -sw*0.5/a0, -2.0*cw/a0, (1.0-alpha)/a0 };
}

BiquadCoeffs EQBand::makeAllPass(double f0, double Q, double fs)
{
    double w0 = 2.0*kPi*f0/fs, cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0*Q), a0 = 1.0 + alpha;
    return { (1.0-alpha)/a0, -2.0*cw/a0, 1.0, -2.0*cw/a0, (1.0-alpha)/a0 };
}

std::complex<double> EQBand::getFrequencyResponse(double freq, double sampleRate) const noexcept
{
    if (!params.enabled || params.bypassed) return { 1.0, 0.0 };
    const double w = 2.0 * kPi * freq / sampleRate;
    const std::complex<double> z1 = { std::cos(w), -std::sin(w) };
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> H = { 1.0, 0.0 };
    for (int i = 0; i < numBiquads; ++i)
    {
        const auto& c = coeffs[i];
        H *= (c.b0 + c.b1*z1 + c.b2*z2) / (std::complex<double>{1.0,0.0} + c.a1*z1 + c.a2*z2);
    }
    return H;
}
