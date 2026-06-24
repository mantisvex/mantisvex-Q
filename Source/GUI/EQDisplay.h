#pragma once

#include <JuceHeader.h>
#include "../DSP/EQBand.h"
#include "../DSP/SpectrumAnalyzer.h"

class MantisVexQProcessor;

class EQDisplay : public juce::Component,
                  private juce::Timer
{
public:
    explicit EQDisplay(MantisVexQProcessor& p);
    ~EQDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseMove        (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove   (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseExit        (const juce::MouseEvent& e) override;

    int  getSelectedBand() const noexcept { return selectedBand; }
    void setSelectedBand(int band);

    std::function<void(int)> onBandSelected;

private:
    void timerCallback() override;

    // Coordinate helpers — use view range for zoom support
    float freqToX (float freq) const noexcept;
    float xToFreq (float x)    const noexcept;
    float dbToY   (float db)   const noexcept;
    float yToDb   (float y)    const noexcept;

    // Curve cache — rebuilt once per timer tick and on freq-axis zoom
    std::array<std::array<float, 512>, 24> bandMagCache   {};
    std::array<std::array<float, 512>, 24> bandPhaseCache {};  // degrees, per-band
    std::array<float, 24>                  bandCenterMag  {};  // |H| at band centre, for GR readout
    bool curveCacheDirty = true;
    void rebuildCurveCache();

    // Band clipboard for copy / paste
    struct BandClipboard {
        EQBandParams params {};
        ChannelMode  channel  = ChannelMode::Stereo;
        bool dynOn = false, scOn = false;
        float dynThr = -18.f, dynAtk = 10.f, dynRel = 100.f, dynRat = 4.f;
        bool valid = false;
    } clipboard;

    // Drawing
    void drawBackground (juce::Graphics& g);
    void drawGrid       (juce::Graphics& g);
    void drawSpectrum   (juce::Graphics& g);
    void drawBandFills  (juce::Graphics& g);
    void drawEQCurve    (juce::Graphics& g);
    void drawBandNodes  (juce::Graphics& g);
    void drawTooltip    (juce::Graphics& g);
    void drawGhostNode  (juce::Graphics& g);

    // Interaction
    int  bandNodeAtPoint (juce::Point<float> pt) const noexcept;
    bool tryAddBand      (juce::Point<float> pos);
    void showContextMenu    (int bandIndex, juce::Point<int> screenPos);
    void showDefaultTypeMenu(juce::Point<int> screenPos);
    void handleContextMenuResult (int result, int bandIndex);
    void setParamNorm (const juce::String& paramID, float normalised);

    // Zoom helpers
    void zoomFreqAxis (float pivotX, float factor);
    void zoomDbAxis   (float factor);
    void resetZoom    ();

    // Drawing extras
    void drawLevelMeters  (juce::Graphics& g);
    void drawPianoRoll    (juce::Graphics& g);
    void drawCollisions   (juce::Graphics& g);
    void drawGainScaleBtn (juce::Graphics& g);
    void drawPhaseResponse(juce::Graphics& g);

    MantisVexQProcessor& processor;

    // Spectrum — pre (always) and post (overlay when POST EQ active)
    std::array<float, SpectrumAnalyzer::kFFTSize> spectrumData{};
    std::array<float, SpectrumAnalyzer::kFFTSize> spectrumPeak{};
    bool spectrumInitialized = false;
    std::array<float, SpectrumAnalyzer::kFFTSize> spectrumDataPost{};
    std::array<float, SpectrumAnalyzer::kFFTSize> spectrumPeakPost{};
    bool spectrumPostInitialized = false;
    static constexpr float kPeakDecay = 0.9985f;

    // Interaction state
    int  selectedBand   = -1;
    int  hoveredBand    = -1;
    bool dragging       = false;
    juce::Point<float> dragStart;
    float dragStartFreq = 0.0f;
    float dragStartGain = 0.0f;

    // Shift-drag axis lock
    bool  axisDetermined = false;
    bool  axisLockedFreq = false;
    bool  axisLockedGain = false;

    // Ghost node hover
    juce::Point<float> ghostPos;
    bool showGhost = false;

    // View (zoom) range — mutable for zoom
    float viewFreqMin =    20.f;
    float viewFreqMax = 20000.f;
    float viewDbMin   =   -30.f;
    float viewDbMax   =    30.f;

    // Gain scale presets (half-range in dB)
    static constexpr float kGainScales[]  = { 3.f, 6.f, 12.f, 24.f, 36.f };
    static constexpr int   kNumGainScales = 5;
    int  gainScaleIndex = 3;   // default ±24 dB

    // Default filter type for newly added bands
    FilterType defaultAddType = FilterType::Bell;

    // Overlay toggles
    bool showPianoRoll   = false;
    bool showPhase       = false;
    bool spectrumFrozen  = false;
    bool spectrumAvg     = false;
    juce::Rectangle<float> pianoRollBtnBounds;
    juce::Rectangle<float> gainScaleBtnBounds;
    juce::Rectangle<float> phaseBtnBounds;
    juce::Rectangle<float> freezeBtnBounds;
    juce::Rectangle<float> rmsAvgBtnBounds;

    // Level meter GUI-side state (decay handled in timer)
    float mtrInL  = -90.f, mtrInR  = -90.f;
    float mtrOutL = -90.f, mtrOutR = -90.f;
    float holdInL = -90.f, holdInR = -90.f;
    float holdOutL= -90.f, holdOutR= -90.f;
    bool  clipInL  = false, clipInR  = false;
    bool  clipOutL = false, clipOutR = false;

    static constexpr float kNodeRadius  =  6.5f;
    static constexpr int   kCurvePoints = 512;
    static constexpr int   kMaxBands    = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQDisplay)
};
