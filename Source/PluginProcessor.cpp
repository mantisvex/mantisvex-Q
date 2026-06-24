#include "PluginProcessor.h"
#include "PluginEditor.h"

static const juce::StringArray kFilterTypeNames {
    "Bell","Low Shelf","High Shelf","Low Cut","High Cut","Notch",
    "Band Pass","All Pass","Tilt Shelf"
};
static const juce::StringArray kSlopeNames    { "12 dB/oct","24 dB/oct","36 dB/oct","48 dB/oct" };
static const juce::StringArray kChannelNames  { "Stereo","Left","Right","Mid","Side" };
static const juce::StringArray kOSNames       { "1x","2x","4x","8x" };

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MantisVexQProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = juce::NormalisableRange<float>(
        20.0f, 20000.0f,
        [](float s, float e, float v) { return s * std::pow(e / s, v); },
        [](float s, float e, float v) { return std::log(v / s) / std::log(e / s); });

    auto qRange = juce::NormalisableRange<float>(
        0.1f, 40.0f,
        [](float s, float e, float v) { return s * std::pow(e / s, v); },
        [](float s, float e, float v) { return std::log(v / s) / std::log(e / s); });

    float defaultFreqs[kNumBands] = {
        25.f,40.f,63.f,100.f,160.f,250.f,400.f,630.f,
        1000.f,1600.f,2500.f,4000.f,6300.f,8000.f,10000.f,12000.f,
        14000.f,16000.f,17000.f,18000.f,19000.f,19500.f,20000.f,20000.f
    };

    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i + 1) + "_";
        layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID(p+"enabled",  1), p+"Enabled",  false));
        layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID(p+"bypassed", 1), p+"Bypassed", false));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"freq",     1), p+"Freq",  freqRange, defaultFreqs[i]));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"gain",     1), p+"Gain",  juce::NormalisableRange<float>(-30.f,30.f,0.01f), 0.f));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"q",        1), p+"Q",     qRange, 0.707f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(p+"type",     1), p+"Type",  kFilterTypeNames, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(p+"slope",    1), p+"Slope", kSlopeNames,      1));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(p+"channel",  1), p+"Channel", kChannelNames,  0));
        // Dynamic EQ params
        layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID(p+"dyn",      1), p+"DynEnabled", false));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"dyn_thr",  1), p+"DynThresh",
            juce::NormalisableRange<float>(-60.f, 0.f, 0.1f), -18.f));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"dyn_atk",  1), p+"DynAttack",
            juce::NormalisableRange<float>(0.1f, 200.f, 0.1f, 0.5f), 10.f));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"dyn_rel",  1), p+"DynRelease",
            juce::NormalisableRange<float>(1.f, 1000.f, 0.1f, 0.5f), 100.f));
        layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID(p+"dyn_rat",  1), p+"DynRatio",
            juce::NormalisableRange<float>(1.f, 20.f, 0.01f, 0.5f), 4.f));
    }

    layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID("output_gain",   1), "Output Gain",
        juce::NormalisableRange<float>(-24.f, 24.f, 0.01f), 0.f));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("auto_gain",     1), "Auto Gain",     false));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("spectrum_post", 1), "Spectrum Post", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("oversample",    1), "Oversampling",  kOSNames, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("lin_phase",     1), "Linear Phase",  false));

    return layout;
}

//==============================================================================
MantisVexQProcessor::MantisVexQProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "MantisVexQ", createParameterLayout())
{
    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i + 1) + "_";
        for (auto& s : { "enabled","bypassed","freq","gain","q","type","slope","channel",
                         "dyn","dyn_thr","dyn_atk","dyn_rel","dyn_rat" })
            apvts.addParameterListener(p + s, this);
        soloStates[i].store(false);
    }
    for (auto& id : { "output_gain","auto_gain","spectrum_post","oversample","lin_phase" })
        apvts.addParameterListener(id, this);

    outputGainParam   = apvts.getRawParameterValue("output_gain");
    autoGainParam     = apvts.getRawParameterValue("auto_gain");
    spectrumPostParam = apvts.getRawParameterValue("spectrum_post");
    oversampleParam   = apvts.getRawParameterValue("oversample");
    linPhaseParam     = apvts.getRawParameterValue("lin_phase");

    // Init A/B states as empty (will be populated on first prepareToPlay or use)
    abState[0] = juce::ValueTree();
    abState[1] = juce::ValueTree();
}

MantisVexQProcessor::~MantisVexQProcessor()
{
    cancelPendingUpdate();
    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i + 1) + "_";
        for (auto& s : { "enabled","bypassed","freq","gain","q","type","slope","channel",
                         "dyn","dyn_thr","dyn_atk","dyn_rel","dyn_rat" })
            apvts.removeParameterListener(p + s, this);
    }
    for (auto& id : { "output_gain","auto_gain","spectrum_post","oversample","lin_phase" })
        apvts.removeParameterListener(id, this);
}

//==============================================================================
void MantisVexQProcessor::rebuildOversampler()
{
    int choice = (int)oversampleParam->load();
    lastOversampleChoice = choice;

    if (choice == 0)
    {
        oversampler.reset();
        setLatencySamples(0);
        return;
    }

    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, choice,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing((size_t)maxBlockSize);

    int osLatency = (int)oversampler->getLatencyInSamples();
    setLatencySamples(osLatency);
}

void MantisVexQProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize      = samplesPerBlock;

    for (auto& b : bands)    b.reset();
    for (auto& d : dynBands) d.reset();
    updateAllBands();

    rebuildOversampler();

    // Prepare linear phase convolution
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels      = 2;
    linearPhaseConv.prepare(spec);
    linearPhasePrepared = true;

    if (*linPhaseParam > 0.5f)
        rebuildLinearPhaseIR();
}

void MantisVexQProcessor::releaseResources() {}

//==============================================================================
void MantisVexQProcessor::processBlockMinPhase(float* L, float* R, int numSamples)
{
    const bool anyBandSoloed = isAnySoloed();

    for (int s = 0; s < numSamples; ++s)
    {
        float sL = L[s], sR = R[s];

        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = bands[b].getParams();
            if (!p.enabled || p.bypassed) continue;
            if (anyBandSoloed && !soloStates[b].load(std::memory_order_relaxed)) continue;

            // Check dynamic EQ
            float blend = 1.f;
            bool dynOn = *apvts.getRawParameterValue("band" + juce::String(b+1) + "_dyn") > 0.5f;
            if (dynOn)
            {
                blend = dynBands[b].compute(sL, sR);
                dynBlendState[b].store(blend, std::memory_order_relaxed);
            }

            if (blend < 0.0001f) continue;

            switch (channelModes[b])
            {
                case ChannelMode::Stereo:
                {
                    float fL = bands[b].processSampleL(sL);
                    float fR = bands[b].processSampleR(sR);
                    sL = sL + (fL - sL) * blend;
                    sR = sR + (fR - sR) * blend;
                    break;
                }
                case ChannelMode::Left:
                {
                    float fL = bands[b].processSampleL(sL);
                    sL = sL + (fL - sL) * blend;
                    break;
                }
                case ChannelMode::Right:
                {
                    float fR = bands[b].processSampleR(sR);
                    sR = sR + (fR - sR) * blend;
                    break;
                }
                case ChannelMode::Mid:
                {
                    float m = (sL + sR) * 0.5f, sd = (sL - sR) * 0.5f;
                    float fm = bands[b].processSampleL(m);
                    float nm = m + (fm - m) * blend;
                    sL = nm + sd; sR = nm - sd;
                    break;
                }
                case ChannelMode::Side:
                {
                    float m = (sL + sR) * 0.5f, sd = (sL - sR) * 0.5f;
                    float fs = bands[b].processSampleR(sd);
                    float ns = sd + (fs - sd) * blend;
                    sL = m + ns; sR = m - ns;
                    break;
                }
                default: break;
            }
        }

        L[s] = sL;
        R[s] = sR;
    }
}

void MantisVexQProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (parametersChanged.exchange(false))
    {
        // Check if oversampling factor changed
        int newChoice = (int)oversampleParam->load();
        if (newChoice != lastOversampleChoice)
            rebuildOversampler();

        updateAllBands();
        currentAutoGain = (*autoGainParam > 0.5f) ? computeAutoGain() : 0.0f;
    }

    const int numChannels = buffer.getNumChannels();
    float* L = numChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    float* R = numChannels > 1 ? buffer.getWritePointer(1) : L;
    const int numSamples = buffer.getNumSamples();

    if (L) spectrumPreL.pushSamples(L, numSamples);
    if (R && R != L) spectrumPreR.pushSamples(R, numSamples);

    // Track input peak levels
    {
        auto peak = [&](float* ch) {
            float pk = 0.f;
            for (int s = 0; s < numSamples; ++s) pk = std::max(pk, std::abs(ch[s]));
            return pk;
        };
        if (L) inputLevel.L.store(peak(L), std::memory_order_relaxed);
        if (R && R != L) inputLevel.R.store(peak(R), std::memory_order_relaxed);
        else if (L)      inputLevel.R.store(inputLevel.L.load(std::memory_order_relaxed),
                                            std::memory_order_relaxed);
    }

    const bool useLinPhase = linearPhasePrepared && (*linPhaseParam > 0.5f);

    if (useLinPhase)
    {
        // Linear phase: run through convolution engine
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        linearPhaseConv.process(ctx);
    }
    else if (oversampler && (int)oversampleParam->load() > 0)
    {
        // Oversampled min-phase
        juce::dsp::AudioBlock<float> inputBlock(buffer);
        auto osBlock = oversampler->processSamplesUp(inputBlock);

        float* osL = osBlock.getChannelPointer(0);
        float* osR = osBlock.getNumChannels() > 1 ? osBlock.getChannelPointer(1) : osL;
        processBlockMinPhase(osL, osR, (int)osBlock.getNumSamples());

        oversampler->processSamplesDown(inputBlock);
    }
    else
    {
        if (L) processBlockMinPhase(L, R && R != L ? R : L, numSamples);
    }

    if (L) spectrumPostL.pushSamples(L, numSamples);
    if (R && R != L) spectrumPostR.pushSamples(R, numSamples);

    // Track output peak levels
    {
        auto peak = [&](float* ch) {
            float pk = 0.f;
            for (int s = 0; s < numSamples; ++s) pk = std::max(pk, std::abs(ch[s]));
            return pk;
        };
        if (L) outputLevel.L.store(peak(L), std::memory_order_relaxed);
        if (R && R != L) outputLevel.R.store(peak(R), std::memory_order_relaxed);
        else if (L)      outputLevel.R.store(outputLevel.L.load(std::memory_order_relaxed),
                                             std::memory_order_relaxed);
    }

    const float totalGain = outputGainParam->load() + currentAutoGain;
    buffer.applyGain(juce::Decibels::decibelsToGain(totalGain));
}

//==============================================================================
void MantisVexQProcessor::parameterChanged(const juce::String&, float)
{
    parametersChanged.store(true);
    if (*linPhaseParam > 0.5f)
        triggerAsyncUpdate();
}

void MantisVexQProcessor::handleAsyncUpdate()
{
    rebuildLinearPhaseIR();
}

void MantisVexQProcessor::rebuildLinearPhaseIR()
{
    if (!linearPhasePrepared) return;

    static constexpr int kIROrder = 12;
    static constexpr int kIRSize  = 1 << kIROrder;  // 4096

    // Build packed complex spectrum (interleaved Re/Im pairs for JUCE FFT)
    std::vector<float> spectrum(kIRSize * 2, 0.f);

    int numBins = kIRSize / 2 + 1;
    for (int k = 0; k < numBins; ++k)
    {
        double freq = (double)k * currentSampleRate / (double)kIRSize;
        if (k == 0) freq = 1.0;

        std::complex<double> H = { 1.0, 0.0 };
        for (int b = 0; b < kNumBands; ++b)
            H *= bands[b].getFrequencyResponse(freq, currentSampleRate);

        float mag = (float)std::abs(H);
        spectrum[k * 2]     = mag;
        spectrum[k * 2 + 1] = 0.f;
    }

    // Mirror for the real-only IFFT
    for (int k = 1; k < kIRSize / 2; ++k)
    {
        spectrum[(kIRSize - k) * 2]     = spectrum[k * 2];
        spectrum[(kIRSize - k) * 2 + 1] = 0.f;
    }

    // IFFT
    juce::dsp::FFT ifft(kIROrder);
    ifft.performRealOnlyInverseTransform(spectrum.data());

    // Circular-shift by kIRSize/2 to make it causal (linear phase FIR)
    std::vector<float> causalIR(kIRSize);
    const int half = kIRSize / 2;
    for (int n = 0; n < kIRSize; ++n)
        causalIR[n] = spectrum[((n + half) % kIRSize) * 2] / (float)kIRSize;

    // Apply Hann window
    juce::dsp::WindowingFunction<float> wnd(kIRSize, juce::dsp::WindowingFunction<float>::hann);
    wnd.multiplyWithWindowingTable(causalIR.data(), kIRSize);

    // Load into convolution engine
    juce::AudioBuffer<float> irBuf(1, kIRSize);
    irBuf.copyFrom(0, 0, causalIR.data(), kIRSize);

    linearPhaseConv.loadImpulseResponse(
        std::move(irBuf),
        currentSampleRate,
        juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::no);

    setLatencySamples(half);
}

//==============================================================================
void MantisVexQProcessor::updateBand(int i)
{
    juce::String p = "band" + juce::String(i + 1) + "_";

    EQBandParams bp;
    bp.enabled  = *apvts.getRawParameterValue(p + "enabled")  > 0.5f;
    bp.bypassed = *apvts.getRawParameterValue(p + "bypassed") > 0.5f;
    bp.freq     = *apvts.getRawParameterValue(p + "freq");
    bp.gainDB   = *apvts.getRawParameterValue(p + "gain");
    bp.q        = *apvts.getRawParameterValue(p + "q");
    bp.type     = static_cast<FilterType>(static_cast<int>(*apvts.getRawParameterValue(p + "type")));
    bp.order    = static_cast<int>(*apvts.getRawParameterValue(p + "slope")) + 1;

    ChannelMode newMode = static_cast<ChannelMode>(
        static_cast<int>(*apvts.getRawParameterValue(p + "channel")));

    bool resetNeeded = (bp.type != bands[i].getParams().type) || (newMode != channelModes[i]);
    bands[i].setParams(bp, currentSampleRate);
    channelModes[i] = newMode;
    if (resetNeeded) bands[i].reset();

    // Update dynamic band
    bool dynOn   = *apvts.getRawParameterValue(p + "dyn")     > 0.5f;
    float thrDB  = *apvts.getRawParameterValue(p + "dyn_thr");
    float atk    = *apvts.getRawParameterValue(p + "dyn_atk");
    float rel    = *apvts.getRawParameterValue(p + "dyn_rel");
    float rat    = *apvts.getRawParameterValue(p + "dyn_rat");

    if (dynOn)
        dynBands[i].prepare(bp.freq, bp.q, thrDB, atk, rel, rat, currentSampleRate);
}

void MantisVexQProcessor::updateAllBands()
{
    for (int i = 0; i < kNumBands; ++i) updateBand(i);
}

//==============================================================================
bool MantisVexQProcessor::getNextPreSpectrumData(std::array<float, SpectrumAnalyzer::kFFTSize>& dest)
{
    std::array<float, SpectrumAnalyzer::kFFTSize> tmpR;
    bool gotL = spectrumPreL.getNextFFTData(dest);
    bool gotR = spectrumPreR.getNextFFTData(tmpR);
    if (gotL && gotR)
        for (int i = 0; i < SpectrumAnalyzer::kFFTSize; ++i)
            dest[i] = (dest[i] + tmpR[i]) * 0.5f;
    return gotL || gotR;
}

bool MantisVexQProcessor::getNextPostSpectrumData(std::array<float, SpectrumAnalyzer::kFFTSize>& dest)
{
    std::array<float, SpectrumAnalyzer::kFFTSize> tmpR;
    bool gotL = spectrumPostL.getNextFFTData(dest);
    bool gotR = spectrumPostR.getNextFFTData(tmpR);
    if (gotL && gotR)
        for (int i = 0; i < SpectrumAnalyzer::kFFTSize; ++i)
            dest[i] = (dest[i] + tmpR[i]) * 0.5f;
    return gotL || gotR;
}

bool MantisVexQProcessor::isBandBypassed(int i) const noexcept
{
    return *apvts.getRawParameterValue("band" + juce::String(i+1) + "_bypassed") > 0.5f;
}

void MantisVexQProcessor::setSoloBand(int i, bool solo) noexcept
{
    soloStates[i].store(solo, std::memory_order_relaxed);
}

bool MantisVexQProcessor::isBandSoloed(int i) const noexcept
{
    return soloStates[i].load(std::memory_order_relaxed);
}

bool MantisVexQProcessor::isAnySoloed() const noexcept
{
    for (int i = 0; i < kNumBands; ++i)
        if (soloStates[i].load(std::memory_order_relaxed)) return true;
    return false;
}

float MantisVexQProcessor::computeAutoGain() const
{
    double weightedSum = 0.0, weightSum = 0.0;
    for (int i = 0; i < 128; ++i)
    {
        double freq   = 20.0 * std::pow(20000.0 / 20.0, (double)i / 127.0);
        std::complex<double> H = { 1.0, 0.0 };
        for (int b = 0; b < kNumBands; ++b)
            H *= bands[b].getFrequencyResponse(freq, currentSampleRate);
        double db     = 20.0 * std::log10(std::max(std::abs(H), 1e-10));
        double weight = 1.0 / freq;
        weightedSum  += db * weight;
        weightSum    += weight;
    }
    return -(float)(weightedSum / weightSum);
}

//==============================================================================
void MantisVexQProcessor::copyToAB(int slot)
{
    abState[slot] = apvts.copyState();
    activeABSlot = slot;
}

void MantisVexQProcessor::loadAB(int slot)
{
    if (!abState[slot].isValid()) return;
    activeABSlot = slot;
    apvts.replaceState(abState[slot].createCopy());
    parametersChanged.store(true);
}

//==============================================================================
void MantisVexQProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Embed A/B states
    juce::ValueTree ab("AB");
    if (abState[0].isValid()) ab.addChild(abState[0].createCopy(), -1, nullptr);
    if (abState[1].isValid()) ab.addChild(abState[1].createCopy(), -1, nullptr);
    state.addChild(ab, -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MantisVexQProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml(*xml);

        // Restore A/B states
        auto ab = state.getChildWithName("AB");
        if (ab.isValid())
        {
            state.removeChild(ab, nullptr);
            if (ab.getNumChildren() > 0) abState[0] = ab.getChild(0).createCopy();
            if (ab.getNumChildren() > 1) abState[1] = ab.getChild(1).createCopy();
        }

        apvts.replaceState(state);
        parametersChanged.store(true);
    }
}

juce::AudioProcessorEditor* MantisVexQProcessor::createEditor()
{
    return new MantisVexQEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MantisVexQProcessor();
}
