#pragma once

#include <JuceHeader.h>
#include "DSP/EQBand.h"
#include "DSP/DynBand.h"
#include "DSP/SpectrumAnalyzer.h"

static constexpr int kNumBands = 24;

class MantisVexQProcessor : public juce::AudioProcessor,
                             public juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
{
public:
    MantisVexQProcessor();
    ~MantisVexQProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mantis Vex Q"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    juce::UndoManager& getUndoManager() { return undoManager; }

    // GUI thread accessors
    bool getNextPreSpectrumData (std::array<float, SpectrumAnalyzer::kFFTSize>& dest);
    bool getNextPostSpectrumData(std::array<float, SpectrumAnalyzer::kFFTSize>& dest);
    double   getCurrentSampleRate() const noexcept { return currentSampleRate; }
    uint32_t getBandUpdateSeq()     const noexcept { return bandUpdateSeq.load(std::memory_order_relaxed); }
    const EQBand& getBand (int index) const { return bands[index]; }
    ChannelMode getChannelMode (int index) const noexcept { return channelModes[index]; }
    bool isBandBypassed   (int index) const noexcept;
    bool isBandDynEnabled (int index) const noexcept
    {
        auto* p = bandParamCache[index].dyn;
        return p != nullptr && *p > 0.5f;
    }
    float getDynBlend (int index) const noexcept { return dynBlendState[index].load(std::memory_order_relaxed); }

    // Peak level meters (written audio thread, read GUI thread)
    struct LevelData {
        std::atomic<float> L { 0.f }, R { 0.f };
    };
    const LevelData& getInputLevel()  const noexcept { return inputLevel; }
    const LevelData& getOutputLevel() const noexcept { return outputLevel; }

    // Solo
    void setSoloBand (int index, bool solo) noexcept;
    bool isBandSoloed (int index) const noexcept;
    bool isAnySoloed() const noexcept;

    float computeAutoGain() const;

    // MIDI CC learn
    void startMidiLearn (const juce::String& paramID);
    void stopMidiLearn  ();
    void clearMidiCC    (const juce::String& paramID);
    void assignMidiCC   (int cc, const juce::String& paramID);  // direct (no learn state)
    int  getMidiCC      (const juce::String& paramID) const noexcept;
    bool isMidiLearning () const noexcept { return midiLearnIdx.load() >= 0; }
    juce::String getMidiLearnParamID() const noexcept;

    // A/B comparison
    void copyToAB (int slot);     // slot 0=A, 1=B
    void loadAB   (int slot);
    bool hasABState (int slot) const noexcept { return abState[slot].isValid(); }
    int  getActiveABSlot() const noexcept { return activeABSlot; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void handleAsyncUpdate() override;   // rebuilds linear-phase IR on message thread
    void updateBand (int bandIndex);
    void updateAllBands();
    void rebuildOversampler();
    void rebuildLinearPhaseIR();
    void processBlockMinPhase (float* L, float* R, const float* scL, const float* scR, int numSamples);

    juce::UndoManager                          undoManager;
    juce::AudioProcessorValueTreeState         apvts;

    std::array<EQBand,      kNumBands>         bands;
    std::array<DynBand,     kNumBands>         dynBands;
    std::array<ChannelMode, kNumBands>         channelModes {};
    std::array<std::atomic<bool>, kNumBands>   soloStates   {};

    SpectrumAnalyzer spectrumPreL,  spectrumPreR;
    SpectrumAnalyzer spectrumPostL, spectrumPostR;

    // Oversampling — recreated on factor change
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int  lastOversampleChoice = 0;
    int  maxBlockSize         = 512;

    // Linear phase convolution
    juce::dsp::Convolution linearPhaseConv;
    bool linearPhasePrepared = false;

    // A/B state
    juce::ValueTree abState[2];
    int             activeABSlot = 0;

    LevelData inputLevel, outputLevel;

    std::array<std::atomic<float>, kNumBands> dynBlendState {};  // written audio thread, read GUI thread
    bool dynOnCache[kNumBands] {};  // updated on message thread in updateBand, read on audio thread
    bool scOnCache [kNumBands] {};  // sidechain-for-dyn toggle, same threading model as dynOnCache

    std::atomic<bool>     parametersChanged { true };
    std::atomic<bool>     irUpdateNeeded    { false };
    double                currentSampleRate  = 44100.0;
    float                 currentAutoGain    = 0.0f;

    // Per-band cached raw parameter pointers — set once in constructor, read-only after
    struct BandParamCache {
        std::atomic<float>* enabled = nullptr; std::atomic<float>* bypassed = nullptr;
        std::atomic<float>* freq    = nullptr; std::atomic<float>* gain     = nullptr;
        std::atomic<float>* q       = nullptr; std::atomic<float>* type     = nullptr;
        std::atomic<float>* slope   = nullptr; std::atomic<float>* channel  = nullptr;
        std::atomic<float>* dyn     = nullptr; std::atomic<float>* dynThr   = nullptr;
        std::atomic<float>* dynAtk  = nullptr; std::atomic<float>* dynRel   = nullptr;
        std::atomic<float>* dynRat  = nullptr; std::atomic<float>* dynSc    = nullptr;
    };
    std::array<BandParamCache, kNumBands> bandParamCache {};

    std::atomic<uint32_t>  bandsDirtyMask        { (1u << kNumBands) - 1u };
    std::atomic<bool>      oversampleNeedsRebuild { false };
    std::atomic<uint32_t>  bandUpdateSeq          { 0 };  // incremented each updateBand call

    bool updateDirtyBands();  // returns true if any band was updated

    std::atomic<float>* outputGainParam    = nullptr;
    std::atomic<float>* autoGainParam     = nullptr;
    std::atomic<float>* spectrumPostParam = nullptr;
    std::atomic<float>* oversampleParam   = nullptr;
    std::atomic<float>* linPhaseParam     = nullptr;
    std::atomic<float>* monitorSoloParam  = nullptr;
    std::atomic<float>* deltaParam        = nullptr;
    std::atomic<float>* msMonitorParam    = nullptr;

    juce::AudioBuffer<float> dryBuf;

    // MIDI CC learn — built once in constructor, never mutated after
    static constexpr int kNumMidiCC = 128;
    std::vector<juce::AudioProcessorParameter*> paramPtrs;   // parallel to paramIDs
    std::vector<juce::String>                   paramIDs;    // parallel to paramPtrs
    std::array<std::atomic<int>, kNumMidiCC>    midiCCMap  {};  // CC# → paramPtrs index, -1=none
    std::atomic<int>                            midiLearnIdx { -1 }; // idx being learned, -1=off

    // Solo monitoring bandpass filter — applied to output when monitor mode + solo active
    BiquadCoeffs monitorCoeffs {};
    BiquadState  monitorL {}, monitorR {};
    int          lastMonitorBand  = -1;
    float        lastMonitorFreq  = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MantisVexQProcessor)
};
