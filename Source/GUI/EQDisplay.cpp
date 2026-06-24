#include "EQDisplay.h"
#include "../PluginProcessor.h"
#include "BandColors.h"

namespace Colors {
    static const juce::Colour BG         { 0xff06070e };
    static const juce::Colour BG2        { 0xff0a0b16 };
    static const juce::Colour Grid       { 0x0caaaacc };
    static const juce::Colour GridMajor  { 0x15aaaacc };
    static const juce::Colour GridZero   { 0x2a5599ee };
    static const juce::Colour GridText   { 0xff3a3a62 };
    static const juce::Colour SpecBase   { 0xff00ddaa };
    static const juce::Colour SpecPeak   { 0xff55ffcc };
    static const juce::Colour Ghost      { 0x5588bbff };
}

// Out-of-line definition required for ODR-use of static constexpr array
constexpr float EQDisplay::kGainScales[];

static juce::String freqToNoteName(float freq)
{
    if (freq < 16.f || freq > 25000.f) return {};
    int midi = juce::roundToInt(12.f * std::log2(freq / 440.f) + 69.f);
    if (midi < 0 || midi > 127) return {};
    const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
}

EQDisplay::EQDisplay(MantisVexQProcessor& p) : processor(p)
{
    startTimerHz(30);
    setRepaintsOnMouseActivity(false);
}

EQDisplay::~EQDisplay() { stopTimer(); }

void EQDisplay::timerCallback()
{
    std::array<float, SpectrumAnalyzer::kFFTSize> newData;
    if (!spectrumFrozen)
    {
        if (processor.getNextPreSpectrumData(newData))
        {
            for (int i = 0; i < SpectrumAnalyzer::kFFTSize; ++i)
            {
                float db = juce::Decibels::gainToDecibels(newData[i]);
                if (spectrumAvg)
                    spectrumData[i] = spectrumData[i] * 0.965f + 0.035f * db;  // slow exponential avg
                else
                    spectrumData[i] = std::max(spectrumData[i] * 0.84f, db);   // peak envelope
                spectrumPeak[i] = std::max(spectrumPeak[i] * kPeakDecay, spectrumData[i]);
            }
            spectrumInitialized = true;
        }
        if (processor.getNextPostSpectrumData(newData))
        {
            for (int i = 0; i < SpectrumAnalyzer::kFFTSize; ++i)
            {
                float db = juce::Decibels::gainToDecibels(newData[i]);
                if (spectrumAvg)
                    spectrumDataPost[i] = spectrumDataPost[i] * 0.965f + 0.035f * db;
                else
                    spectrumDataPost[i] = std::max(spectrumDataPost[i] * 0.84f, db);
                spectrumPeakPost[i] = std::max(spectrumPeakPost[i] * kPeakDecay, spectrumDataPost[i]);
            }
            spectrumPostInitialized = true;
        }
    }

    // Decay level meters — 30 Hz, ~1.5 dB/frame fast decay, 0.1 dB/frame hold
    auto toDb = [](float linear) { return 20.f * std::log10(std::max(linear, 1e-9f)); };
    auto decay = [](float& meter, float& hold, float raw) {
        float db = raw;
        meter = meter - 1.5f;
        if (db > meter) meter = db;
        hold  = hold  - 0.08f;
        if (db > hold)  hold  = db;
        meter = std::max(meter, -90.f);
        hold  = std::max(hold,  -90.f);
    };

    const auto& il = processor.getInputLevel();
    const auto& ol = processor.getOutputLevel();
    decay(mtrInL,  holdInL,  toDb(il.L.load(std::memory_order_relaxed)));
    decay(mtrInR,  holdInR,  toDb(il.R.load(std::memory_order_relaxed)));
    decay(mtrOutL, holdOutL, toDb(ol.L.load(std::memory_order_relaxed)));
    decay(mtrOutR, holdOutR, toDb(ol.R.load(std::memory_order_relaxed)));

    // Latch clip indicators (cleared by clicking on the meter)
    if (holdInL  > -0.1f) clipInL  = true;
    if (holdInR  > -0.1f) clipInR  = true;
    if (holdOutL > -0.1f) clipOutL = true;
    if (holdOutR > -0.1f) clipOutR = true;

    curveCacheDirty = true;
    repaint();
}

void EQDisplay::resized() { curveCacheDirty = true; }

void EQDisplay::rebuildCurveCache()
{
    static constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;
    const double fs = processor.getCurrentSampleRate();
    for (int b = 0; b < kNumBands; ++b)
    {
        if (!processor.getBand(b).getParams().enabled)
        {
            bandMagCache  [b].fill(1.f);
            bandPhaseCache[b].fill(0.f);
            continue;
        }
        for (int i = 0; i < kCurvePoints; ++i)
        {
            float t    = static_cast<float>(i) / (kCurvePoints - 1);
            float freq = viewFreqMin * std::pow(viewFreqMax / viewFreqMin, t);
            auto  H    = processor.getBand(b).getFrequencyResponse(static_cast<double>(freq), fs);
            bandMagCache  [b][i] = static_cast<float>(std::abs(H));
            bandPhaseCache[b][i] = static_cast<float>(std::arg(H)) * kRadToDeg;
        }
        // Centre-frequency magnitude for GR readout (one extra call per enabled band)
        auto Hc = processor.getBand(b).getFrequencyResponse(
            static_cast<double>(processor.getBand(b).getParams().freq), fs);
        bandCenterMag[b] = static_cast<float>(std::abs(Hc));
    }
    curveCacheDirty = false;
}

//==============================================================================
float EQDisplay::freqToX(float freq) const noexcept
{
    return static_cast<float>(getWidth()) *
           std::log10(freq / viewFreqMin) / std::log10(viewFreqMax / viewFreqMin);
}
float EQDisplay::xToFreq(float x) const noexcept
{
    return viewFreqMin * std::pow(viewFreqMax / viewFreqMin, x / static_cast<float>(getWidth()));
}
float EQDisplay::dbToY(float db) const noexcept
{
    const float h = static_cast<float>(getHeight());
    return h * (1.0f - (db - viewDbMin) / (viewDbMax - viewDbMin));
}
float EQDisplay::yToDb(float y) const noexcept
{
    return viewDbMin + (viewDbMax - viewDbMin) * (1.0f - y / static_cast<float>(getHeight()));
}

void EQDisplay::zoomFreqAxis(float pivotX, float factor)
{
    float pivotFreq = xToFreq(pivotX);
    float lo = pivotFreq / std::pow(pivotFreq / viewFreqMin, factor);
    float hi = pivotFreq * std::pow(viewFreqMax / pivotFreq, factor);
    viewFreqMin = juce::jlimit(10.f,    pivotFreq * 0.99f, lo);
    viewFreqMax = juce::jlimit(pivotFreq * 1.01f, 24000.f, hi);
    curveCacheDirty = true;
}

void EQDisplay::zoomDbAxis(float factor)
{
    float mid = (viewDbMin + viewDbMax) * 0.5f;
    float half = (viewDbMax - viewDbMin) * 0.5f * factor;
    half = juce::jlimit(3.f, 36.f, half);
    viewDbMin = mid - half;
    viewDbMax = mid + half;
}

void EQDisplay::resetZoom()
{
    viewFreqMin =    20.f;
    viewFreqMax = 20000.f;
    viewDbMin   =   -30.f;
    viewDbMax   =    30.f;
    curveCacheDirty = true;
}

//==============================================================================
void EQDisplay::paint(juce::Graphics& g)
{
    if (curveCacheDirty) rebuildCurveCache();
    drawBackground(g);
    drawGrid(g);
    if (showPianoRoll) drawPianoRoll(g);
    if (spectrumInitialized) drawSpectrum(g);
    drawBandFills(g);
    drawEQCurve(g);
    if (showPhase) drawPhaseResponse(g);
    drawCollisions(g);
    drawBandNodes(g);
    if (showGhost) drawGhostNode(g);
    int ttBand = (hoveredBand >= 0 && !dragging) ? hoveredBand : selectedBand;
    if (ttBand >= 0) drawTooltip(g);
    drawLevelMeters(g);
    drawGainScaleBtn(g);
}

void EQDisplay::drawBackground(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    juce::ColourGradient mainGrad(Colors::BG2, 0.f, 0.f, Colors::BG, 0.f, h, false);
    g.setGradientFill(mainGrad);
    g.fillRect(bounds);

    g.setColour(juce::Colour(0x07000000));
    for (float sy = 0.f; sy < h; sy += 2.f)
        g.fillRect(0.f, sy, w, 1.f);

    {
        const float cx = w * 0.5f, cy = h * 0.42f;
        juce::ColourGradient radial(Colors::SpecBase.withAlpha(0.030f), cx, cy,
                                     Colors::SpecBase.withAlpha(0.0f),  cx + w * 0.55f, cy, true);
        g.setGradientFill(radial);
        g.fillRect(bounds);
    }
    {
        float edgeW = w * 0.10f;
        juce::ColourGradient vL(juce::Colour(0x28000000), 0.f, 0.f,
                                 juce::Colour(0x00000000), edgeW, 0.f, false);
        g.setGradientFill(vL); g.fillRect(bounds);
        juce::ColourGradient vR(juce::Colour(0x00000000), w - edgeW, 0.f,
                                 juce::Colour(0x28000000), w, 0.f, false);
        g.setGradientFill(vR); g.fillRect(bounds);
    }
    {
        juce::ColourGradient topShadow(juce::Colour(0x35000000), 0.f, 0.f,
                                        juce::Colour(0x00000000), 0.f, 18.f, false);
        g.setGradientFill(topShadow); g.fillRect(bounds);
    }

    g.setColour(juce::Colour(0xff03040a));
    g.fillRect(0.f, h - 18.f, w, 18.f);
    g.setColour(juce::Colour(0x30aaaacc));
    g.drawHorizontalLine(static_cast<int>(h - 18.f), 0.f, w);
}

void EQDisplay::drawGrid(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float labelAreaH = 18.f;
    const float gridH = h - labelAreaH;

    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.5f)));

    // Frequency grid — adapt labels to current view range
    const float allFreqs[] = { 20, 30, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : allFreqs)
    {
        if (f < viewFreqMin * 0.9f || f > viewFreqMax * 1.1f) continue;
        float x = freqToX(f);
        if (x < 0.f || x > w) continue;
        bool major = (f == 100 || f == 1000 || f == 10000 || f == 20 || f == 20000
                      || f == 200 || f == 500 || f == 2000 || f == 5000);
        g.setColour(major ? Colors::GridMajor : Colors::Grid);
        g.drawVerticalLine(static_cast<int>(x), 0.f, gridH);

        if (major)
        {
            g.setColour(Colors::GridText);
            juce::String label = f >= 1000.f ? juce::String(static_cast<int>(f / 1000.f)) + "k"
                                             : juce::String(static_cast<int>(f));
            g.drawText(label, static_cast<int>(x) - 14, static_cast<int>(gridH) + 2, 28, 14,
                       juce::Justification::centred);
        }
    }

    // dB grid — adapt to current view range
    float dbStep = (viewDbMax - viewDbMin) > 48.f ? 12.f : 6.f;
    float firstDb = std::ceil(viewDbMin / dbStep) * dbStep;
    for (float db = firstDb; db <= viewDbMax; db += dbStep)
    {
        float y = dbToY(db);
        if (y < 0.f || y > gridH) continue;
        bool isZero = (std::abs(db) < 0.5f);
        g.setColour(isZero ? Colors::GridZero : Colors::GridMajor);
        g.drawHorizontalLine(static_cast<int>(y), 36.f, w);

        g.setColour(Colors::GridText);
        juce::String label = (db > 0.f ? "+" : "") + juce::String(static_cast<int>(db));
        g.drawText(label, 2, static_cast<int>(y) - 8, 32, 14, juce::Justification::centredRight);
    }
}

void EQDisplay::drawSpectrum(juce::Graphics& g)
{
    const float displayH = static_cast<float>(getHeight()) - 18.f;
    const float nyquist  = static_cast<float>(processor.getCurrentSampleRate() * 0.5);
    const int   bins     = SpectrumAnalyzer::kFFTSize / 2;
    const bool  showPost = *processor.getAPVTS().getRawParameterValue("spectrum_post") > 0.5f;

    auto drawLayer = [&](const std::array<float, SpectrumAnalyzer::kFFTSize>& data,
                         const std::array<float, SpectrumAnalyzer::kFFTSize>& pkData,
                         float fillAlpha, float lineAlpha, float peakAlpha)
    {
        juce::Path fill, line, peakPath;
        bool  started = false;
        float lastX   = 0.f;

        for (int i = 1; i < bins; ++i)
        {
            float binFreq = static_cast<float>(i) * nyquist / static_cast<float>(bins);
            if (binFreq < viewFreqMin || binFreq > viewFreqMax) continue;

            float x  = freqToX(binFreq);
            float db = juce::jlimit(-90.f, 6.f, data[i]);
            float y  = juce::jlimit(0.f, displayH, dbToY(db));
            float pk = juce::jlimit(0.f, displayH, dbToY(juce::jlimit(-90.f, 6.f, pkData[i])));

            if (!started)
            {
                fill.startNewSubPath(x, displayH);
                fill.lineTo(x, y);
                line.startNewSubPath(x, y);
                peakPath.startNewSubPath(x, pk);
                started = true;
            }
            else
            {
                fill.lineTo(x, y);
                line.lineTo(x, y);
                peakPath.lineTo(x, pk);
            }
            lastX = x;
        }

        if (!started) return;
        fill.lineTo(lastX, displayH);
        fill.closeSubPath();

        juce::ColourGradient specGrad(
            Colors::SpecBase.withAlpha(fillAlpha), 0.f, 0.f,
            Colors::SpecBase.withAlpha(0.00f), 0.f, displayH, false);
        specGrad.addColour(0.25, Colors::SpecBase.withAlpha(fillAlpha * 0.6f));
        specGrad.addColour(0.60, Colors::SpecBase.withAlpha(fillAlpha * 0.18f));
        g.setGradientFill(specGrad);
        g.fillPath(fill);

        g.setColour(Colors::SpecBase.withAlpha(lineAlpha));
        g.strokePath(line, juce::PathStrokeType(0.9f, juce::PathStrokeType::curved));

        if (peakAlpha > 0.f)
        {
            g.setColour(Colors::SpecPeak.withAlpha(peakAlpha));
            g.strokePath(peakPath, juce::PathStrokeType(0.8f, juce::PathStrokeType::curved));
        }
    };

    if (spectrumInitialized)
        drawLayer(spectrumData, spectrumPeak,
                  showPost ? 0.05f : 0.16f,
                  showPost ? 0.14f : 0.32f,
                  showPost ? 0.00f : 0.42f);

    if (showPost && spectrumPostInitialized)
        drawLayer(spectrumDataPost, spectrumPeakPost, 0.16f, 0.32f, 0.42f);
}

void EQDisplay::drawBandFills(juce::Graphics& g)
{
    const float wf    = static_cast<float>(getWidth());
    const float hf    = static_cast<float>(getHeight());
    const float zeroY = dbToY(0.f);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& params = processor.getBand(b).getParams();
        if (!params.enabled) continue;

        bool isSelected = (b == selectedBand);
        bool isHovered  = (b == hoveredBand) && !isSelected;
        bool isBypassed = processor.isBandBypassed(b);
        bool isSoloed   = processor.isBandSoloed(b);
        bool dimmed     = processor.isAnySoloed() && !isSoloed;

        float fillAlpha = isSelected ? 0.85f : (isHovered ? 0.68f : 0.52f);
        float lineAlpha = isSelected ? 0.98f : (isHovered ? 0.80f : 0.50f);
        if (isBypassed) { fillAlpha *= 0.25f; lineAlpha *= 0.25f; }
        if (dimmed)     { fillAlpha *= 0.18f; lineAlpha *= 0.18f; }

        juce::Colour col = isBypassed ? juce::Colour(0xff3a3a58) : getBandColor(b);

        juce::Path curvePath, fillPath;
        bool  started = false;
        float lastX   = 0.f;
        float peakY   = zeroY;

        for (int i = 0; i < kCurvePoints; ++i)
        {
            float t   = static_cast<float>(i) / (kCurvePoints - 1);
            float x   = t * wf;
            float db  = juce::jlimit(viewDbMin - 6.f, viewDbMax + 6.f,
                            20.f * std::log10(std::max(bandMagCache[b][i], 1e-9f)));
            float y   = dbToY(db);

            if (std::abs(y - zeroY) > std::abs(peakY - zeroY))
                peakY = y;

            if (!started)
            {
                curvePath.startNewSubPath(x, y);
                fillPath.startNewSubPath(x, zeroY);
                fillPath.lineTo(x, y);
                started = true;
            }
            else
            {
                curvePath.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
            lastX = x;
        }

        if (!started) continue;

        fillPath.lineTo(lastX, zeroY);
        fillPath.closeSubPath();

        juce::ColourGradient grad;
        grad.isRadial = false;
        if (std::abs(peakY - zeroY) < 2.f)
        {
            grad.point1 = { 0.f, 0.f };
            grad.point2 = { 0.f, hf };
            grad.addColour(0.0, col.withAlpha(fillAlpha));
            grad.addColour(1.0, col.withAlpha(fillAlpha));
        }
        else
        {
            grad.point1 = { 0.f, zeroY };
            grad.point2 = { 0.f, peakY };
            grad.addColour(0.0,  col.withAlpha(0.0f));
            grad.addColour(0.4,  col.withAlpha(fillAlpha * 0.6f));
            grad.addColour(1.0,  col.withAlpha(fillAlpha));
        }
        g.setGradientFill(grad);
        g.fillPath(fillPath);

        g.setColour(col.withAlpha(lineAlpha));
        g.strokePath(curvePath, juce::PathStrokeType(
            isSelected ? 2.0f : 1.4f, juce::PathStrokeType::curved));

        // Dynamic GR shadow — shows live effective position when DYN is active
        bool dynOn = *processor.getAPVTS().getRawParameterValue(
            "band" + juce::String(b + 1) + "_dyn") > 0.5f;
        if (dynOn)
        {
            float blend = processor.getDynBlend(b);
            juce::Path shadowPath;
            bool shadowStarted = false;

            for (int i = 0; i < kCurvePoints; ++i)
            {
                float x          = static_cast<float>(i) / (kCurvePoints - 1) * wf;
                float effLinear  = 1.f + blend * (bandMagCache[b][i] - 1.f);
                float effDB      = juce::jlimit(viewDbMin - 6.f, viewDbMax + 6.f,
                                       20.f * std::log10(std::max(effLinear, 1e-9f)));
                float y = dbToY(effDB);
                if (!shadowStarted) { shadowPath.startNewSubPath(x, y); shadowStarted = true; }
                else shadowPath.lineTo(x, y);
            }

            if (shadowStarted)
            {
                g.setColour(juce::Colour(0xff00ddaa).withAlpha(lineAlpha * 0.80f));
                g.strokePath(shadowPath, juce::PathStrokeType(
                    isSelected ? 2.2f : 1.5f, juce::PathStrokeType::curved));
            }
        }
    }
}

void EQDisplay::drawEQCurve(juce::Graphics& g)
{
    const float wf = static_cast<float>(getWidth());

    juce::Path curve;

    for (int i = 0; i < kCurvePoints; ++i)
    {
        float x        = static_cast<float>(i) / (kCurvePoints - 1) * wf;
        float totalMag = 1.f;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = processor.getBand(b).getParams();
            if (!p.enabled || p.bypassed) continue;
            totalMag *= bandMagCache[b][i];
        }
        float db = juce::jlimit(viewDbMin - 6.f, viewDbMax + 6.f,
                       20.f * std::log10(std::max(totalMag, 1e-9f)));
        float y  = dbToY(db);

        if (i == 0) curve.startNewSubPath(x, y);
        else        curve.lineTo(x, y);
    }

    if (curve.isEmpty()) return;

    g.setColour(juce::Colour(0x12ffffff));
    g.strokePath(curve, juce::PathStrokeType(12.f, juce::PathStrokeType::curved));
    g.setColour(juce::Colour(0x22ffffff));
    g.strokePath(curve, juce::PathStrokeType(5.f, juce::PathStrokeType::curved));
    g.setColour(juce::Colour(0x38ffffff));
    g.strokePath(curve, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved));
    g.setColour(juce::Colour(0xeeffffff));
    g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void EQDisplay::drawPhaseResponse(juce::Graphics& g)
{
    const float w      = static_cast<float>(getWidth());
    const float h      = static_cast<float>(getHeight()) - 18.f;
    const float cy     = h * 0.5f;

    // Draw ±90° and 0° reference lines
    g.setColour(juce::Colour(0x18aa88ff));
    g.drawHorizontalLine(static_cast<int>(cy),             0.f, w);
    g.setColour(juce::Colour(0x0caa88ff));
    g.drawHorizontalLine(static_cast<int>(h * 0.25f),      0.f, w);  // +90°
    g.drawHorizontalLine(static_cast<int>(h * 0.75f),      0.f, w);  // -90°

    // Axis labels
    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(7.5f)));
    g.setColour(juce::Colour(0x28aa88ff));
    g.drawText("+180", (int)(w - 36.f), 1, 34, 9, juce::Justification::centredRight);
    g.drawText(" +90", (int)(w - 36.f), (int)(h * 0.25f) - 4, 34, 9, juce::Justification::centredRight);
    g.drawText("   0", (int)(w - 36.f), (int)(cy) - 4, 34, 9, juce::Justification::centredRight);
    g.drawText(" -90", (int)(w - 36.f), (int)(h * 0.75f) - 4, 34, 9, juce::Justification::centredRight);
    g.drawText("-180", (int)(w - 36.f), (int)(h - 11.f), 34, 9, juce::Justification::centredRight);

    // Build combined phase curve (sum of enabled, non-bypassed band phases)
    juce::Path curve;
    for (int i = 0; i < kCurvePoints; ++i)
    {
        float x = static_cast<float>(i) / (kCurvePoints - 1) * w;
        float totalPhase = 0.f;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = processor.getBand(b).getParams();
            if (!p.enabled || p.bypassed) continue;
            totalPhase += bandPhaseCache[b][i];
        }
        // Clamp to ±180° for display — wrap wrap is common but clamping is clearer for EQ use
        totalPhase = juce::jlimit(-180.f, 180.f, totalPhase);
        float y = cy * (1.f - totalPhase / 180.f);

        if (i == 0) curve.startNewSubPath(x, y);
        else        curve.lineTo(x, y);
    }

    if (!curve.isEmpty())
    {
        g.setColour(juce::Colour(0x22aa88ff));
        g.strokePath(curve, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved));
        g.setColour(juce::Colour(0x88aa88ff));
        g.strokePath(curve, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved));
    }
}

void EQDisplay::drawBandNodes(juce::Graphics& g)
{
    const float dispH   = static_cast<float>(getHeight()) - 18.f;
    const bool  anySolo = processor.isAnySoloed();

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& params = processor.getBand(i).getParams();
        if (!params.enabled) continue;

        bool bypassed   = processor.isBandBypassed(i);
        bool soloed     = processor.isBandSoloed(i);
        bool dimmed     = anySolo && !soloed;
        bool isSelected = (i == selectedBand);
        bool isHovered  = (i == hoveredBand);

        float x = freqToX(params.freq);
        if (x < -kNodeRadius - 4.f || x > static_cast<float>(getWidth()) + kNodeRadius + 4.f)
            continue;

        float y = filterTypeIgnoresGain(params.type) ? dbToY(0.f) : dbToY(params.gainDB);
        y = juce::jlimit(kNodeRadius + 2.f, dispH - kNodeRadius - 2.f, y);

        float r     = kNodeRadius + (isHovered && !isSelected ? 2.0f : 0.f);
        float alpha = dimmed ? 0.18f : (bypassed ? 0.45f : 1.0f);

        juce::Colour base = bypassed ? juce::Colour(0xff363654) : getBandColor(i);

        if (isSelected || soloed)
        {
            float gr = r + 7.f;
            g.setColour(base.withAlpha(alpha * 0.05f));
            g.fillEllipse(x - gr - 4.f, y - gr - 4.f, (gr + 4.f) * 2.f, (gr + 4.f) * 2.f);
            g.setColour(base.withAlpha(alpha * 0.10f));
            g.fillEllipse(x - gr - 2.f, y - gr - 2.f, (gr + 2.f) * 2.f, (gr + 2.f) * 2.f);
            g.setColour(base.withAlpha(alpha * 0.20f));
            g.fillEllipse(x - gr, y - gr, gr * 2.f, gr * 2.f);
            g.setColour(base.withAlpha(alpha * 0.55f));
            g.drawEllipse(x - gr, y - gr, gr * 2.f, gr * 2.f, 1.0f);
        }

        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.fillEllipse(x - r + 1.5f, y - r + 2.f, r * 2.f, r * 2.f);
        g.setColour(juce::Colours::black.withAlpha(0.20f));
        g.fillEllipse(x - r + 0.5f, y - r + 1.f, r * 2.f + 2.f, r * 2.f + 2.f);

        g.setColour(base.withAlpha(alpha * 0.82f));
        g.fillEllipse(x - r, y - r, r * 2.f, r * 2.f);

        {
            juce::ColourGradient hl(juce::Colours::white.withAlpha(alpha * 0.38f),
                                    x - r * 0.35f, y - r * 0.4f,
                                    juce::Colours::transparentWhite,
                                    x + r * 0.4f,  y + r * 0.4f, true);
            g.setGradientFill(hl);
            g.fillEllipse(x - r, y - r, r * 2.f, r * 2.f);
        }

        g.setColour(base.brighter(0.35f).withAlpha(alpha * 0.85f));
        g.drawEllipse(x - r, y - r, r * 2.f, r * 2.f, isSelected ? 1.8f : 1.0f);

        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.f)));
        g.setColour(juce::Colours::white.withAlpha(alpha * 0.88f));
        g.drawText(juce::String(i + 1),
                   static_cast<int>(x - r), static_cast<int>(y - r),
                   static_cast<int>(r * 2.f), static_cast<int>(r * 2.f),
                   juce::Justification::centred);

        if (bypassed)
        {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawLine(x - r + 2.f, y, x + r - 2.f, y, 1.5f);
        }

        ChannelMode mode = processor.getChannelMode(i);
        if (mode != ChannelMode::Stereo)
        {
            const char* badge = (mode == ChannelMode::Left)  ? "L" :
                                (mode == ChannelMode::Right) ? "R" :
                                (mode == ChannelMode::Mid)   ? "M" : "S";
            g.setFont(juce::Font(juce::FontOptions().withHeight(7.5f).withStyle("Bold")));
            g.setColour(base.brighter(0.8f).withAlpha(alpha));
            g.drawText(badge,
                       static_cast<int>(x + r - 7.f), static_cast<int>(y - r - 3.f),
                       12, 10, juce::Justification::centred);
        }

        // Dynamic EQ badge + GR activity ring
        bool dynOn = *processor.getAPVTS().getRawParameterValue(
            "band" + juce::String(i+1) + "_dyn") > 0.5f;
        if (dynOn)
        {
            g.setFont(juce::Font(juce::FontOptions().withHeight(7.f).withStyle("Bold")));
            g.setColour(juce::Colour(0xff00ddaa).withAlpha(alpha * 0.9f));
            g.drawText("D", static_cast<int>(x - r - 8.f), static_cast<int>(y - r - 3.f),
                       10, 10, juce::Justification::centred);

            // GR activity ring: grows clockwise from top as blend increases toward 1
            float blend = processor.getDynBlend(i);
            float gr = r + 5.f;
            // Faint full-circle outline
            g.setColour(juce::Colour(0xff00ddaa).withAlpha(alpha * 0.14f));
            g.drawEllipse(x - gr, y - gr, gr * 2.f, gr * 2.f, 0.8f);
            // Solid arc proportional to blend
            if (blend > 0.01f)
            {
                float startAngle = -juce::MathConstants<float>::halfPi;
                float sweepAngle = blend * juce::MathConstants<float>::twoPi;
                juce::Path ring;
                ring.addArc(x - gr, y - gr, gr * 2.f, gr * 2.f,
                            startAngle, startAngle + sweepAngle, true);
                g.setColour(juce::Colour(0xff00ddaa).withAlpha(alpha * (0.55f + blend * 0.35f)));
                g.strokePath(ring, juce::PathStrokeType(1.6f));
            }
            // Effective gain readout below node when partially engaged
            if (blend > 0.02f && blend < 0.98f)
            {
                float fullMag = bandCenterMag[i];
                float effMag  = 1.f + blend * (fullMag - 1.f);
                float effDB   = 20.f * std::log10(std::max(effMag, 1e-9f));
                juce::String readout = (effDB >= 0.f ? "+" : "") + juce::String(effDB, 1);
                g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(7.5f)));
                g.setColour(juce::Colour(0xff00ddaa).withAlpha(alpha * 0.80f));
                g.drawText(readout,
                           static_cast<int>(x - 22.f), static_cast<int>(y + gr + 1.f),
                           44, 10, juce::Justification::centred);
            }
        }
    }
}

void EQDisplay::drawTooltip(juce::Graphics& g)
{
    int band = (hoveredBand >= 0 && !dragging) ? hoveredBand : selectedBand;
    if (band < 0) return;

    const auto& params = processor.getBand(band).getParams();
    bool showGain = !filterTypeIgnoresGain(params.type);

    auto formatFreq = [](float f) -> juce::String {
        if (f >= 10000.f) return juce::String(f / 1000.f, 1) + " kHz";
        if (f >= 1000.f)  return juce::String(f / 1000.f, 2) + " kHz";
        return juce::String(static_cast<int>(f)) + " Hz";
    };

    juce::String note = freqToNoteName(params.freq);
    juce::String text = formatFreq(params.freq);
    if (note.isNotEmpty()) text += " (" + note + ")";
    if (showGain)
        text += "   " + (params.gainDB >= 0.f ? juce::String("+") : juce::String(""))
              + juce::String(params.gainDB, 1) + " dB";
    text += "   Q " + juce::String(params.q, 2);

    bool dynOn = *processor.getAPVTS().getRawParameterValue(
        "band" + juce::String(band+1) + "_dyn") > 0.5f;
    if (dynOn)
    {
        float thr = *processor.getAPVTS().getRawParameterValue("band" + juce::String(band+1) + "_dyn_thr");
        text += "   T" + juce::String(thr, 0) + "dB";
    }

    const float accentW = 3.f, pad = 9.f, th = 26.f;
    const float tw = dynOn ? 340.f : 290.f;

    float nx = freqToX(params.freq);
    float ny = showGain ? dbToY(params.gainDB) : dbToY(0.f);
    float tx = juce::jlimit(0.f, static_cast<float>(getWidth()) - tw - 2.f, nx - tw * 0.5f);
    float ty = ny > th + 32.f ? ny - th - 16.f : ny + 18.f;

    juce::Colour col = getBandColor(band);

    g.setColour(juce::Colour(0xee0b0c1c));
    g.fillRoundedRectangle(tx, ty, tw, th, 5.f);
    g.setColour(col);
    g.fillRoundedRectangle(tx, ty + 2.f, accentW, th - 4.f, 2.f);
    g.setColour(col.withAlpha(0.38f));
    g.drawRoundedRectangle(tx, ty, tw, th, 5.f, 1.f);

    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(11.f)));
    g.setColour(juce::Colours::white.withAlpha(0.88f));
    g.drawText(text,
               static_cast<int>(tx + accentW + pad), static_cast<int>(ty),
               static_cast<int>(tw - accentW - pad * 2.f), static_cast<int>(th),
               juce::Justification::centredLeft);
}

void EQDisplay::drawGhostNode(juce::Graphics& g)
{
    g.setColour(Colors::Ghost.withAlpha(0.55f));
    g.drawEllipse(ghostPos.x - kNodeRadius, ghostPos.y - kNodeRadius,
                  kNodeRadius * 2.f, kNodeRadius * 2.f, 1.2f);
    const float cs = 5.f;
    g.setColour(Colors::Ghost.withAlpha(0.35f));
    g.drawLine(ghostPos.x - cs, ghostPos.y, ghostPos.x + cs, ghostPos.y, 1.0f);
    g.drawLine(ghostPos.x, ghostPos.y - cs, ghostPos.x, ghostPos.y + cs, 1.0f);

    juce::String note = freqToNoteName(xToFreq(ghostPos.x));
    if (note.isNotEmpty())
    {
        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.f)));
        g.setColour(Colors::Ghost.withAlpha(0.60f));
        float tx = juce::jmin(ghostPos.x + kNodeRadius + 4.f,
                              static_cast<float>(getWidth()) - 38.f);
        g.drawText(note, (int)tx, (int)(ghostPos.y - 13.f), 36, 11,
                   juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// Level meters — narrow strips on left (IN) and right (OUT) edges
void EQDisplay::drawLevelMeters(juce::Graphics& g)
{
    const float h     = static_cast<float>(getHeight()) - 18.f;
    const float mW    = 7.f;   // each strip width
    const float gap   = 2.f;   // gap between L and R strips
    const float minDB = -60.f;
    const float maxDB =  6.f;
    const float range = maxDB - minDB;

    auto drawStrip = [&](float x, float levelDB, float holdDB, bool clipped)
    {
        // Dark background
        g.setColour(juce::Colour(0xff02030a));
        g.fillRect(x, 0.f, mW, h);

        float level = juce::jlimit(0.f, 1.f, (levelDB - minDB) / range);
        float levelH = h * level;
        if (levelH > 0.f)
        {
            // Gradient: teal bottom → yellow upper → red clip
            juce::ColourGradient grad(juce::Colour(0xff007755), x, h,
                                      juce::Colour(0xffff3333), x, h - h * ((maxDB - minDB) / range), false);
            grad.addColour(0.75, juce::Colour(0xff00ddaa));
            grad.addColour(0.92, juce::Colour(0xffffcc00));
            g.setGradientFill(grad);
            g.fillRect(x, h - levelH, mW, levelH);
        }

        // Peak hold tick
        if (holdDB > minDB)
        {
            float holdLevel = juce::jlimit(0.f, 1.f, (holdDB - minDB) / range);
            float holdY = h - h * holdLevel;
            g.setColour(holdDB > -1.f ? juce::Colour(0xffff5555) : juce::Colours::white.withAlpha(0.7f));
            g.fillRect(x, holdY, mW, 1.5f);
        }

        // Clip indicator — persistent red bar at top until click-reset
        if (clipped)
        {
            g.setColour(juce::Colour(0xffff1122));
            g.fillRect(x, 0.f, mW, 3.f);
        }

        // Thin border
        g.setColour(juce::Colour(0xff0a0b18));
        g.drawRect(x, 0.f, mW, h, 0.5f);
    };

    // Left side: IN
    drawStrip(0.f,        mtrInL,  holdInL,  clipInL);
    drawStrip(mW + gap,   mtrInR,  holdInR,  clipInR);

    // Right side: OUT
    const float w = static_cast<float>(getWidth());
    drawStrip(w - 2.f * (mW + gap), mtrOutL, holdOutL, clipOutL);
    drawStrip(w - mW - gap,         mtrOutR, holdOutR, clipOutR);

    // Labels
    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(7.5f)));
    g.setColour(juce::Colour(0xff2a2a50));
    g.drawText("IN",  0, (int)(h * 0.5f) - 5, (int)(2.f * mW + gap) + 4, 10,
               juce::Justification::centred);
    g.drawText("OUT", (int)(w - 2.f * (mW + gap)) - 2, (int)(h * 0.5f) - 5,
               (int)(2.f * mW + gap) + 4, 10, juce::Justification::centred);
}

//==============================================================================
// Piano roll — C note markers in the grid label area + subtle vertical lines
void EQDisplay::drawPianoRoll(juce::Graphics& g)
{
    const float w     = static_cast<float>(getWidth());
    const float h     = static_cast<float>(getHeight());
    const float gridH = h - 18.f;

    // C note frequencies: C0=16.35 Hz .. C10=16744 Hz
    static const float cFreqs[]  = { 16.35f, 32.7f, 65.4f, 130.8f, 261.6f,
                                      523.3f, 1046.5f, 2093.f, 4186.f, 8372.f, 16744.f };
    static const char* cNames[]  = { "C0","C1","C2","C3","C4","C5","C6","C7","C8","C9","C10" };

    // A note frequencies (A=440Hz reference points)
    static const float aFreqs[]  = { 27.5f, 55.f, 110.f, 220.f, 440.f, 880.f, 1760.f, 3520.f, 7040.f, 14080.f };

    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(7.5f)));

    for (int i = 0; i < 11; ++i)
    {
        float f = cFreqs[i];
        if (f < viewFreqMin * 0.8f || f > viewFreqMax * 1.2f) continue;
        float x = freqToX(f);
        if (x < 0.f || x > w) continue;

        // Subtle vertical line
        g.setColour(juce::Colour(0x0cffffff));
        g.drawVerticalLine(static_cast<int>(x), 0.f, gridH);

        // Note label just above the freq label row
        g.setColour(juce::Colour(0xff22224a));
        g.drawText(cNames[i], static_cast<int>(x) - 10, static_cast<int>(gridH) - 10, 20, 9,
                   juce::Justification::centred);
    }

    // A notes — even subtler marker
    for (float f : aFreqs)
    {
        if (f < viewFreqMin * 0.8f || f > viewFreqMax * 1.2f) continue;
        float x = freqToX(f);
        if (x < 0.f || x > w) continue;
        g.setColour(juce::Colour(0x08ffffff));
        g.drawVerticalLine(static_cast<int>(x), 0.f, gridH);
    }
}

//==============================================================================
// Band collision highlighting — orange glow between overlapping bands
void EQDisplay::drawCollisions(juce::Graphics& g)
{
    const float dispH = static_cast<float>(getHeight()) - 18.f;

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& pi = processor.getBand(i).getParams();
        if (!pi.enabled || processor.isBandBypassed(i)) continue;

        for (int j = i + 1; j < kNumBands; ++j)
        {
            const auto& pj = processor.getBand(j).getParams();
            if (!pj.enabled || processor.isBandBypassed(j)) continue;

            // Log-space frequency distance vs combined half-bandwidth
            float fi = pi.freq, fj = pj.freq;
            float logDist = std::abs(std::log2(fj / fi));
            float bwi = 1.f / (pi.q * 1.4427f);   // half-bandwidth in octaves
            float bwj = 1.f / (pj.q * 1.4427f);
            float overlap = (bwi + bwj) * 0.5f - logDist;

            if (overlap <= 0.f) continue;

            float intensity = juce::jlimit(0.f, 1.f, overlap / ((bwi + bwj) * 0.5f));
            if (intensity < 0.05f) continue;

            float xi = freqToX(fi);
            float yi = filterTypeIgnoresGain(pi.type) ? dbToY(0.f) : dbToY(pi.gainDB);
            float xj = freqToX(fj);
            float yj = filterTypeIgnoresGain(pj.type) ? dbToY(0.f) : dbToY(pj.gainDB);

            yi = juce::jlimit(0.f, dispH, yi);
            yj = juce::jlimit(0.f, dispH, yj);

            // Draw a connecting arc between the two nodes
            juce::Path arc;
            float mx = (xi + xj) * 0.5f;
            float my = std::min(yi, yj) - 12.f - intensity * 8.f;
            arc.startNewSubPath(xi, yi);
            arc.quadraticTo(mx, my, xj, yj);

            g.setColour(juce::Colour(0xffff8800).withAlpha(intensity * 0.55f));
            g.strokePath(arc, juce::PathStrokeType(1.2f + intensity * 1.2f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

            // Warning dot at midpoint
            float dotX = mx, dotY = my;
            g.setColour(juce::Colour(0xffff8800).withAlpha(intensity * 0.8f));
            g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.f, 5.f);
        }
    }
}

//==============================================================================
// Gain scale + piano roll toggle buttons in the label strip
void EQDisplay::drawGainScaleBtn(juce::Graphics& g)
{
    const float h = static_cast<float>(getHeight());
    const float gridH = h - 18.f;

    // Gain scale button — bottom-left
    float halfRange = kGainScales[gainScaleIndex];
    juce::String scaleText = juce::String(juce::String::formatted("+/-%g", halfRange)) + "dB";
    gainScaleBtnBounds = juce::Rectangle<float>(2.f, gridH + 2.f, 52.f, 13.f);

    g.setColour(juce::Colour(0xff0c0d1c));
    g.fillRect(gainScaleBtnBounds);
    g.setColour(juce::Colour(0xff222240));
    g.drawRect(gainScaleBtnBounds, 0.6f);
    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.f)));
    g.setColour(juce::Colour(0xff3a3a6a));
    g.drawText(scaleText, gainScaleBtnBounds.toNearestInt(), juce::Justification::centred);

    // Freeze button — centred in label strip
    {
        const float fw = static_cast<float>(getWidth());
        freezeBtnBounds = juce::Rectangle<float>(fw * 0.5f - 24.f, gridH + 2.f, 48.f, 13.f);
        g.setColour(spectrumFrozen ? juce::Colour(0xff1a0d08) : juce::Colour(0xff0c0d1c));
        g.fillRect(freezeBtnBounds);
        g.setColour(spectrumFrozen ? juce::Colour(0xff664422) : juce::Colour(0xff222240));
        g.drawRect(freezeBtnBounds, 0.6f);
        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.f)));
        g.setColour(spectrumFrozen ? juce::Colour(0xffffaa44) : juce::Colour(0xff3a3a6a));
        g.drawText("FREEZE", freezeBtnBounds.toNearestInt(), juce::Justification::centred);
    }

    // Phase response toggle — right of gain scale
    phaseBtnBounds = juce::Rectangle<float>(58.f, gridH + 2.f, 38.f, 13.f);
    g.setColour(showPhase ? juce::Colour(0xff0e0c1e) : juce::Colour(0xff0c0d1c));
    g.fillRect(phaseBtnBounds);
    g.setColour(showPhase ? juce::Colour(0xff3a2a66) : juce::Colour(0xff222240));
    g.drawRect(phaseBtnBounds, 0.6f);
    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.f)));
    g.setColour(showPhase ? juce::Colour(0xff9977dd) : juce::Colour(0xff3a3a6a));
    g.drawText("PHASE", phaseBtnBounds.toNearestInt(), juce::Justification::centred);

    // RMS averaging mode toggle — right of phase
    rmsAvgBtnBounds = juce::Rectangle<float>(100.f, gridH + 2.f, 32.f, 13.f);
    g.setColour(spectrumAvg ? juce::Colour(0xff0a1410) : juce::Colour(0xff0c0d1c));
    g.fillRect(rmsAvgBtnBounds);
    g.setColour(spectrumAvg ? juce::Colour(0xff1a4430) : juce::Colour(0xff222240));
    g.drawRect(rmsAvgBtnBounds, 0.6f);
    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.f)));
    g.setColour(spectrumAvg ? juce::Colour(0xff44dd88) : juce::Colour(0xff3a3a6a));
    g.drawText("RMS", rmsAvgBtnBounds.toNearestInt(), juce::Justification::centred);

    // Piano roll toggle — bottom-right (avoid OUT meter, leave 12px margin)
    const float w = static_cast<float>(getWidth());
    pianoRollBtnBounds = juce::Rectangle<float>(w - 38.f, gridH + 2.f, 36.f, 13.f);

    g.setColour(showPianoRoll ? juce::Colour(0xff0d1525) : juce::Colour(0xff0c0d1c));
    g.fillRect(pianoRollBtnBounds);
    g.setColour(showPianoRoll ? juce::Colour(0xff334466) : juce::Colour(0xff222240));
    g.drawRect(pianoRollBtnBounds, 0.6f);
    g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.f)));
    g.setColour(showPianoRoll ? juce::Colour(0xff5577bb) : juce::Colour(0xff3a3a6a));
    g.drawText("NOTES", pianoRollBtnBounds.toNearestInt(), juce::Justification::centred);
}

//==============================================================================
int EQDisplay::bandNodeAtPoint(juce::Point<float> pt) const noexcept
{
    for (int i = kNumBands - 1; i >= 0; --i)
    {
        const auto& p = processor.getBand(i).getParams();
        if (!p.enabled) continue;
        float x = freqToX(p.freq);
        float y = filterTypeIgnoresGain(p.type) ? dbToY(0.f) : dbToY(p.gainDB);
        if (pt.getDistanceFrom({ x, y }) <= kNodeRadius + 3.f) return i;
    }
    return -1;
}

bool EQDisplay::tryAddBand(juce::Point<float> pos)
{
    for (int i = 0; i < kNumBands; ++i)
    {
        if (!processor.getBand(i).getParams().enabled)
        {
            juce::String prefix = "band" + juce::String(i + 1) + "_";
            float freq = xToFreq(pos.x);
            float gain = filterTypeIgnoresGain(defaultAddType) ? 0.f : yToDb(pos.y);
            float t    = std::log10(freq / viewFreqMin) / std::log10(viewFreqMax / viewFreqMin);

            processor.getUndoManager().beginNewTransaction();
            setParamNorm(prefix + "freq",    juce::jlimit(0.f, 1.f, t));
            setParamNorm(prefix + "gain",    juce::jlimit(0.f, 1.f, (gain + 30.f) / 60.f));
            if (auto* p = processor.getAPVTS().getParameter(prefix + "type"))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(defaultAddType)));
            setParamNorm(prefix + "enabled", 1.0f);

            setSelectedBand(i);
            if (onBandSelected) onBandSelected(i);
            showGhost = false;
            repaint();
            return true;
        }
    }
    return false;
}

void EQDisplay::setParamNorm(const juce::String& paramID, float normalised)
{
    if (auto* p = processor.getAPVTS().getParameter(paramID))
        p->setValueNotifyingHost(normalised);
}

//==============================================================================
void EQDisplay::mouseMove(const juce::MouseEvent& e)
{
    int hit = bandNodeAtPoint(e.position.toFloat());
    if (hit != hoveredBand)
    {
        hoveredBand = hit;
        setMouseCursor(hit >= 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                : juce::MouseCursor::NormalCursor);
        repaint();
    }
    if (hit < 0)
    {
        ghostPos  = e.position.toFloat();
        showGhost = true;
        repaint();
    }
    else { showGhost = false; }
}

void EQDisplay::mouseExit(const juce::MouseEvent&)
{
    hoveredBand = -1;
    showGhost   = false;
    repaint();
}

void EQDisplay::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.position.toFloat();

    // Check label-strip overlay buttons first
    if (gainScaleBtnBounds.contains(pos))
    {
        if (e.mods.isRightButtonDown())
            gainScaleIndex = 3;  // reset to ±24 dB on right-click
        else
            gainScaleIndex = (gainScaleIndex + 1) % kNumGainScales;
        float half = kGainScales[gainScaleIndex];
        viewDbMin = -half; viewDbMax = half;
        repaint();
        return;
    }
    if (freezeBtnBounds.contains(pos))
    {
        spectrumFrozen = !spectrumFrozen;
        repaint();
        return;
    }
    if (phaseBtnBounds.contains(pos))
    {
        showPhase = !showPhase;
        repaint();
        return;
    }
    if (rmsAvgBtnBounds.contains(pos))
    {
        spectrumAvg = !spectrumAvg;
        repaint();
        return;
    }
    if (pianoRollBtnBounds.contains(pos))
    {
        showPianoRoll = !showPianoRoll;
        repaint();
        return;
    }

    // Click on the level meter strips (far left/right edges) resets clip indicators
    {
        const float mW = 7.f, gap = 2.f, h = static_cast<float>(getHeight()) - 18.f;
        const float w  = static_cast<float>(getWidth());
        juce::Rectangle<float> inArea  (0.f,                   0.f, mW * 2.f + gap, h);
        juce::Rectangle<float> outArea (w - 2.f*(mW+gap), 0.f, mW * 2.f + gap, h);
        if (inArea.contains(pos) || outArea.contains(pos))
        {
            clipInL = clipInR = clipOutL = clipOutR = false;
            repaint();
            return;
        }
    }

    int hit = bandNodeAtPoint(pos);

    if (e.mods.isRightButtonDown())
    {
        if (hit >= 0) { setSelectedBand(hit); showContextMenu(hit, e.getScreenPosition()); }
        else if (pos.y < static_cast<float>(getHeight()) - 18.f)
            showDefaultTypeMenu(e.getScreenPosition());
        return;
    }

    if (hit >= 0)
    {
        setSelectedBand(hit);
        if (onBandSelected) onBandSelected(hit);
        dragging       = true;
        axisDetermined = false;
        axisLockedFreq = axisLockedGain = false;
        dragStart      = pos;
        dragStartFreq  = processor.getBand(hit).getParams().freq;
        dragStartGain  = processor.getBand(hit).getParams().gainDB;
    }
    else if (pos.y < static_cast<float>(getHeight()) - 18.f)
    {
        // Spectrum Grab — single click on the EQ area (not label strip) adds a band
        tryAddBand(pos);
    }
}

void EQDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging || selectedBand < 0) return;

    const float dx = e.position.x - dragStart.x;
    const float dy = e.position.y - dragStart.y;
    const float w  = static_cast<float>(getWidth());
    const float h  = static_cast<float>(getHeight());

    if (e.mods.isShiftDown() && !axisDetermined)
    {
        if (std::abs(dx) > 3.f || std::abs(dy) > 3.f)
        {
            axisLockedFreq = std::abs(dx) >= std::abs(dy);
            axisLockedGain = !axisLockedFreq;
            axisDetermined = true;
        }
    }
    else if (!e.mods.isShiftDown())
    {
        axisLockedFreq = axisLockedGain = false;
        axisDetermined = false;
    }

    juce::String prefix = "band" + juce::String(selectedBand + 1) + "_";

    if (!axisLockedGain)
    {
        float t0 = std::log10(dragStartFreq / viewFreqMin) / std::log10(viewFreqMax / viewFreqMin);
        float t1 = juce::jlimit(0.f, 1.f, t0 + dx / w);
        setParamNorm(prefix + "freq", t1);
    }

    const auto& params = processor.getBand(selectedBand).getParams();
    if (!axisLockedFreq && !filterTypeIgnoresGain(params.type))
    {
        float dbRange = viewDbMax - viewDbMin;
        float newGain = juce::jlimit(-30.f, 30.f, dragStartGain - (dy / h) * dbRange);
        setParamNorm(prefix + "gain", juce::jlimit(0.f, 1.f, (newGain + 30.f) / 60.f));
    }

    curveCacheDirty = true;
    repaint();
}

void EQDisplay::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    axisDetermined = axisLockedFreq = axisLockedGain = false;
    processor.getUndoManager().beginNewTransaction();
}

void EQDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    int hit = bandNodeAtPoint(e.position.toFloat());
    if (hit >= 0)
    {
        // Double-click on a node: delete it (Pro-Q style)
        processor.getUndoManager().beginNewTransaction();
        juce::String prefix = "band" + juce::String(hit + 1) + "_";
        if (auto* p = processor.getAPVTS().getParameter(prefix + "enabled"))
            p->setValueNotifyingHost(0.f);
        if (auto* p = processor.getAPVTS().getParameter(prefix + "bypassed"))
            p->setValueNotifyingHost(0.f);
        processor.setSoloBand(hit, false);
        if (selectedBand == hit) { setSelectedBand(-1); if (onBandSelected) onBandSelected(-1); }
        return;
    }

    // Double-click near no node: reset zoom
    resetZoom();
    repaint();
}

void EQDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int hit = bandNodeAtPoint(e.position.toFloat());
    if (hit < 0) hit = selectedBand;

    if (hit >= 0 && !e.mods.isCommandDown())
    {
        // Scroll on a node: adjust Q
        juce::String qID = "band" + juce::String(hit + 1) + "_q";
        if (auto* p = processor.getAPVTS().getParameter(qID))
            p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, p->getValue() + wheel.deltaY * 0.08f));
        repaint();
        return;
    }

    // Scroll on empty area or Cmd held: zoom
    float factor = 1.f - wheel.deltaY * 0.15f;
    if (e.mods.isCtrlDown())
        zoomDbAxis(factor);
    else
        zoomFreqAxis(e.position.x, factor);
    repaint();
}

void EQDisplay::setSelectedBand(int band)
{
    if (selectedBand != band) { selectedBand = band; repaint(); }
}

//==============================================================================
void EQDisplay::showContextMenu(int bi, juce::Point<int> screenPos)
{
    if (bi < 0) return;
    const auto& params = processor.getBand(bi).getParams();
    bool bypassed = processor.isBandBypassed(bi);
    bool soloed   = processor.isBandSoloed(bi);
    ChannelMode ch = processor.getChannelMode(bi);

    juce::PopupMenu typeMenu;
    const char* typeNames[] = {"Bell","Low Shelf","High Shelf","Low Cut","High Cut","Notch","Band Pass","All Pass","Tilt Shelf"};
    for (int t = 0; t < 9; ++t)
        typeMenu.addItem(100 + t, typeNames[t], true, static_cast<int>(params.type) == t);

    juce::PopupMenu slopeMenu;
    const char* slopeNames[] = {"12 dB/oct","24 dB/oct","36 dB/oct","48 dB/oct"};
    for (int s = 0; s < 4; ++s)
        slopeMenu.addItem(200 + s, slopeNames[s], true, params.order - 1 == s);

    juce::PopupMenu chanMenu;
    const char* chanNames[] = {"Stereo","Left","Right","Mid","Side"};
    for (int c = 0; c < 5; ++c)
        chanMenu.addItem(300 + c, chanNames[c], true, static_cast<int>(ch) == c);

    juce::PopupMenu menu;
    menu.addItem(1, bypassed ? "Un-bypass" : "Bypass", true, bypassed);
    menu.addItem(2, soloed   ? "Un-solo"   : "Solo",   true, soloed);
    menu.addSeparator();
    menu.addSubMenu("Filter Type", typeMenu);
    menu.addSubMenu("Slope",       slopeMenu);
    menu.addSubMenu("Channel",     chanMenu);
    menu.addSeparator();
    menu.addItem(400, "Copy Band");
    menu.addItem(401, "Paste Band", clipboard.valid);
    menu.addSeparator();
    menu.addItem(999,  "Delete Band");
    menu.addItem(1000, "Reset to Default");

    juce::Component::SafePointer<EQDisplay> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
        [safe, bi](int result) { if (safe != nullptr) safe->handleContextMenuResult(result, bi); });
}

void EQDisplay::showDefaultTypeMenu(juce::Point<int> screenPos)
{
    const char* typeNames[] = { "Bell","Low Shelf","High Shelf","Low Cut","High Cut",
                                "Notch","Band Pass","All Pass","Tilt Shelf" };
    juce::PopupMenu menu;
    menu.addSectionHeader("Default Filter Type");
    for (int t = 0; t < 9; ++t)
        menu.addItem(t + 1, typeNames[t], true, static_cast<int>(defaultAddType) == t);

    juce::Component::SafePointer<EQDisplay> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
        [safe](int result) {
            if (safe != nullptr && result > 0)
                safe->defaultAddType = static_cast<FilterType>(result - 1);
        });
}

void EQDisplay::handleContextMenuResult(int result, int bi)
{
    if (result == 0) return;
    processor.getUndoManager().beginNewTransaction();
    juce::String prefix = "band" + juce::String(bi + 1) + "_";
    auto& apvts = processor.getAPVTS();

    if (result == 1)
    {
        if (auto* p = apvts.getParameter(prefix + "bypassed"))
            p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.f : 1.f);
    }
    else if (result == 2)
    {
        processor.setSoloBand(bi, !processor.isBandSoloed(bi));
        repaint();
    }
    else if (result >= 100 && result < 109)
    {
        if (auto* p = apvts.getParameter(prefix + "type"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(result - 100)));
    }
    else if (result >= 200 && result < 204)
    {
        if (auto* p = apvts.getParameter(prefix + "slope"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(result - 200)));
    }
    else if (result >= 300 && result < 305)
    {
        if (auto* p = apvts.getParameter(prefix + "channel"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(result - 300)));
    }
    else if (result == 400)
    {
        // Copy — no undo needed, just snapshot
        clipboard.params  = processor.getBand(bi).getParams();
        clipboard.channel = processor.getChannelMode(bi);
        clipboard.dynOn   = *apvts.getRawParameterValue(prefix + "dyn")    > 0.5f;
        clipboard.scOn    = *apvts.getRawParameterValue(prefix + "dyn_sc") > 0.5f;
        clipboard.dynThr  = *apvts.getRawParameterValue(prefix + "dyn_thr");
        clipboard.dynAtk  = *apvts.getRawParameterValue(prefix + "dyn_atk");
        clipboard.dynRel  = *apvts.getRawParameterValue(prefix + "dyn_rel");
        clipboard.dynRat  = *apvts.getRawParameterValue(prefix + "dyn_rat");
        clipboard.valid   = true;
        return;
    }
    else if (result == 401 && clipboard.valid)
    {
        // Paste
        auto setNorm = [&](const juce::String& id, float val) {
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(val));
        };
        auto setBool = [&](const juce::String& id, bool on) {
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(on ? 1.f : 0.f);
        };
        setNorm(prefix + "freq",    clipboard.params.freq);
        setNorm(prefix + "gain",    clipboard.params.gainDB);
        setNorm(prefix + "q",       clipboard.params.q);
        if (auto* p = apvts.getParameter(prefix + "type"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(clipboard.params.type)));
        if (auto* p = apvts.getParameter(prefix + "slope"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(clipboard.params.order - 1)));
        if (auto* p = apvts.getParameter(prefix + "channel"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(clipboard.channel)));
        setBool(prefix + "dyn",    clipboard.dynOn);
        setBool(prefix + "dyn_sc", clipboard.scOn);
        setNorm(prefix + "dyn_thr", clipboard.dynThr);
        setNorm(prefix + "dyn_atk", clipboard.dynAtk);
        setNorm(prefix + "dyn_rel", clipboard.dynRel);
        setNorm(prefix + "dyn_rat", clipboard.dynRat);
        setBool(prefix + "enabled", true);
    }
    else if (result == 999)
    {
        if (auto* p = apvts.getParameter(prefix + "enabled"))  p->setValueNotifyingHost(0.f);
        if (auto* p = apvts.getParameter(prefix + "bypassed")) p->setValueNotifyingHost(0.f);
        processor.setSoloBand(bi, false);
        if (selectedBand == bi) setSelectedBand(-1);
        if (onBandSelected) onBandSelected(-1);
    }
    else if (result == 1000)
    {
        if (auto* p = apvts.getParameter(prefix + "gain"))    p->setValueNotifyingHost(0.5f);
        if (auto* p = apvts.getParameter(prefix + "q"))       p->setValueNotifyingHost(p->convertTo0to1(0.707f));
        if (auto* p = apvts.getParameter(prefix + "type"))    p->setValueNotifyingHost(0.f);
        if (auto* p = apvts.getParameter(prefix + "slope"))   p->setValueNotifyingHost(p->convertTo0to1(1.f));
        if (auto* p = apvts.getParameter(prefix + "channel")) p->setValueNotifyingHost(0.f);
    }
}
