#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/EQDisplay.h"
#include "GUI/BandColors.h"
#include "GUI/MantisLookAndFeel.h"

//==============================================================================
class BandControlStrip : public juce::Component,
                         private juce::Timer
{
public:
    explicit BandControlStrip(MantisVexQProcessor& p);
    ~BandControlStrip() override;

    void setActiveBand(int bandIndex);
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    std::function<void(int)> onBandSelected;

    static constexpr int kTabH = 22;

private:
    void timerCallback() override;
    void connectToParams();
    void disconnectFromParams();

    MantisVexQProcessor& processor;
    int activeBand = -1;

    // Cached layout for paint()-side value rendering
    int valX[3] = {};
    int valW    = 0;
    int valY    = 0;

    juce::Label  labelFreq, labelGain, labelQ, labelType, labelSlope, labelChannel;
    juce::Label  labelDynThr, labelDynAtk, labelDynRel, labelDynRat;
    juce::Slider sliderFreq, sliderGain, sliderQ;
    juce::Slider sliderDynThr, sliderDynAtk, sliderDynRel, sliderDynRat;
    juce::ComboBox comboType, comboSlope, comboChannel;
    juce::TextButton btnEnabled, btnBypass, btnSolo, btnDyn;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment>   attachFreq, attachGain, attachQ;
    std::unique_ptr<APVTS::SliderAttachment>   attachDynThr, attachDynAtk, attachDynRel, attachDynRat;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachType, attachSlope, attachChannel;
    std::unique_ptr<APVTS::ButtonAttachment>   attachEnabled, attachBypass, attachDyn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandControlStrip)
};

//==============================================================================
class MantisVexQEditor : public juce::AudioProcessorEditor,
                         private juce::KeyListener,
                         private juce::Timer
{
public:
    explicit MantisVexQEditor(MantisVexQProcessor&);
    ~MantisVexQEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void timerCallback() override;

    MantisVexQProcessor& audioProcessor;
    MantisLookAndFeel    lnf;

    EQDisplay        eqDisplay;
    BandControlStrip bandStrip;

    // Global controls
    juce::TextButton btnSpecPost, btnAutoGain;
    juce::TextButton btnLinPhase;
    juce::ComboBox   comboOversample;
    juce::Label      outputGainLabel;
    juce::Slider     outputGainSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment outputGainAttach;
    juce::AudioProcessorValueTreeState::ButtonAttachment specPostAttach, autoGainAttach, linPhaseAttach;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment oversampleAttach;

    // A/B comparison
    juce::TextButton btnA, btnB;

    juce::Label infoLabel;
    juce::Label latencyLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MantisVexQEditor)
};
