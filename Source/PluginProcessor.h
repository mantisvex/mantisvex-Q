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
    bool acceptsMidi()  const override { return false; }
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
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    const EQBand& getBand (int index) const { return bands[index]; }
    ChannelMode getChannelMode (int index) const noexcept { return channelModes[index]; }
    bool isBandBypassed (int index) const noexcept;
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
    void processBlockMinPhase (float* L, float* R, int numSamples);

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

    std::atomic<bool>  parametersChanged { true };
    std::atomic<bool>  irUpdateNeeded    { false };
    double             currentSampleRate  = 44100.0;
    float              currentAutoGain    = 0.0f;

    std::atomic<float>* outputGainParam   = nullptr;
    std::atomic<float>* autoGainParam     = nullptr;
    std::atomic<float>* spectrumPostParam = nullptr;
    std::atomic<float>* oversampleParam   = nullptr;
    std::atomic<float>* linPhaseParam     = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MantisVexQProcessor)
};
