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
        layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID(p+"dyn_sc",   1), p+"DynSidechain", false));
    }

    layout.add(std::make_unique<juce::AudioParameterFloat> (juce::ParameterID("output_gain",   1), "Output Gain",
        juce::NormalisableRange<float>(-24.f, 24.f, 0.01f), 0.f));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("auto_gain",     1), "Auto Gain",     false));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("spectrum_post", 1), "Spectrum Post", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("oversample",    1), "Oversampling",  kOSNames, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("lin_phase",     1), "Linear Phase",  false));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("monitor_solo",  1), "Monitor Solo",  false));
    layout.add(std::make_unique<juce::AudioParameterBool>  (juce::ParameterID("delta",          1), "Delta",         false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("ms_monitor",     1), "MS Monitor",
        juce::StringArray { "ST","M","S" }, 0));

    return layout;
}

//==============================================================================
MantisVexQProcessor::MantisVexQProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "MantisVexQ", createParameterLayout())
{
    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i + 1) + "_";
        for (auto& s : { "enabled","bypassed","freq","gain","q","type","slope","channel",
                         "dyn","dyn_thr","dyn_atk","dyn_rel","dyn_rat","dyn_sc" })
            apvts.addParameterListener(p + s, this);
        soloStates[i].store(false);
    }
    for (auto& id : { "output_gain","auto_gain","spectrum_post","oversample","lin_phase","monitor_solo","delta","ms_monitor" })
        apvts.addParameterListener(id, this);

    outputGainParam   = apvts.getRawParameterValue("output_gain");
    autoGainParam     = apvts.getRawParameterValue("auto_gain");
    spectrumPostParam = apvts.getRawParameterValue("spectrum_post");
    oversampleParam   = apvts.getRawParameterValue("oversample");
    linPhaseParam     = apvts.getRawParameterValue("lin_phase");
    monitorSoloParam  = apvts.getRawParameterValue("monitor_solo");
    deltaParam        = apvts.getRawParameterValue("delta");
    msMonitorParam    = apvts.getRawParameterValue("ms_monitor");

    // Init A/B states as empty (will be populated on first prepareToPlay or use)
    abState[0] = juce::ValueTree();
    abState[1] = juce::ValueTree();

    // Build stable param list (ID + pointer) for MIDI CC routing
    // Enumerate all known IDs in order so we can reverse-lookup by string
    for (int b = 1; b <= kNumBands; ++b)
    {
        juce::String px = "band" + juce::String(b) + "_";
        for (const char* sfx : { "freq","gain","q","type","slope","channel",
                                  "dyn","dyn_thr","dyn_atk","dyn_rel","dyn_rat","dyn_sc",
                                  "enabled","bypassed" })
        {
            juce::String id = px + sfx;
            if (auto* p = apvts.getParameter(id)) { paramIDs.push_back(id); paramPtrs.push_back(p); }
        }
    }
    for (const char* id : { "output_gain","auto_gain","spectrum_post","oversample",
                             "lin_phase","monitor_solo","delta","ms_monitor" })
        if (auto* p = apvts.getParameter(id)) { paramIDs.push_back(id); paramPtrs.push_back(p); }

    for (auto& a : midiCCMap) a.store(-1);

    // Cache raw parameter pointers per band — eliminates string allocs in updateBand hot path
    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i + 1) + "_";
        auto& c = bandParamCache[i];
        c.enabled  = apvts.getRawParameterValue(p + "enabled");
        c.bypassed = apvts.getRawParameterValue(p + "bypassed");
        c.freq     = apvts.getRawParameterValue(p + "freq");
        c.gain     = apvts.getRawParameterValue(p + "gain");
        c.q        = apvts.getRawParameterValue(p + "q");
        c.type     = apvts.getRawParameterValue(p + "type");
        c.slope    = apvts.getRawParameterValue(p + "slope");
        c.channel  = apvts.getRawParameterValue(p + "channel");
        c.dyn      = apvts.getRawParameterValue(p + "dyn");
        c.dynThr   = apvts.getRawParameterValue(p + "dyn_thr");
        c.dynAtk   = apvts.getRawParameterValue(p + "dyn_atk");
        c.dynRel   = apvts.getRawParameterValue(p + "dyn_rel");
        c.dynRat   = apvts.getRawParameterValue(p + "dyn_rat");
        c.dynSc    = apvts.getRawParameterValue(p + "dyn_sc");
    }
}

MantisVexQProcessor::~MantisVexQProcessor()
{
    cancelPendingUpdate();
    for (int i = 0; i < kNumBands; ++i)
    {
        juce::String p = "band" + juce::String(i + 1) + "_";
        for (auto& s : { "enabled","bypassed","freq","gain","q","type","slope","channel",
                         "dyn","dyn_thr","dyn_atk","dyn_rel","dyn_rat","dyn_sc" })
            apvts.removeParameterListener(p + s, this);
    }
    for (auto& id : { "output_gain","auto_gain","spectrum_post","oversample","lin_phase","monitor_solo","delta","ms_monitor" })
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

    dryBuf.setSize(2, samplesPerBlock, false, true, true);

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
void MantisVexQProcessor::processBlockMinPhase(float* L, float* R, const float* scL, const float* scR, int numSamples)
{
    const bool anyBandSoloed = isAnySoloed();

    bool soloCache[kNumBands] {};
    if (anyBandSoloed)
        for (int b = 0; b < kNumBands; ++b)
            soloCache[b] = soloStates[b].load(std::memory_order_relaxed);

    // Bands-outer / samples-inner: each band's biquad state stays in L1 cache
    // for the entire inner sample loop instead of being evicted every sample.
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bands[b].getParams();
        if (!p.enabled || p.bypassed) continue;
        if (anyBandSoloed && !soloCache[b]) continue;

        if (dynOnCache[b])
        {
            // Dynamic EQ: blend varies per sample, must stay sample-by-sample.
            // Atomic GUI update reduced from N stores/block to 1.
            float lastBlend = 0.f;
            for (int s = 0; s < numSamples; ++s)
            {
                float detL = scOnCache[b] ? scL[s] : L[s];
                float detR = scOnCache[b] ? scR[s] : R[s];
                float blend = dynBands[b].compute(detL, detR);
                lastBlend = blend;
                if (blend < 0.0001f) continue;

                switch (channelModes[b])
                {
                    case ChannelMode::Stereo:
                    {
                        float fL = bands[b].processSampleL(L[s]);
                        float fR = bands[b].processSampleR(R[s]);
                        L[s] = L[s] + (fL - L[s]) * blend;
                        R[s] = R[s] + (fR - R[s]) * blend;
                        break;
                    }
                    case ChannelMode::Left:
                    {
                        float fL = bands[b].processSampleL(L[s]);
                        L[s] = L[s] + (fL - L[s]) * blend;
                        break;
                    }
                    case ChannelMode::Right:
                    {
                        float fR = bands[b].processSampleR(R[s]);
                        R[s] = R[s] + (fR - R[s]) * blend;
                        break;
                    }
                    case ChannelMode::Mid:
                    {
                        float m = (L[s] + R[s]) * 0.5f, sd = (L[s] - R[s]) * 0.5f;
                        float fm = bands[b].processSampleL(m);
                        float nm = m + (fm - m) * blend;
                        L[s] = nm + sd; R[s] = nm - sd;
                        break;
                    }
                    case ChannelMode::Side:
                    {
                        float m = (L[s] + R[s]) * 0.5f, sd = (L[s] - R[s]) * 0.5f;
                        float fs = bands[b].processSampleR(sd);
                        float ns = sd + (fs - sd) * blend;
                        L[s] = m + ns; R[s] = m - ns;
                        break;
                    }
                    default: break;
                }
            }
            dynBlendState[b].store(lastBlend, std::memory_order_relaxed);
        }
        else
        {
            // Static band: tight per-buffer loop — no branching, compiler can vectorize.
            switch (channelModes[b])
            {
                case ChannelMode::Stereo:
                    for (int s = 0; s < numSamples; ++s)
                    {
                        L[s] = bands[b].processSampleL(L[s]);
                        R[s] = bands[b].processSampleR(R[s]);
                    }
                    break;
                case ChannelMode::Left:
                    for (int s = 0; s < numSamples; ++s)
                        L[s] = bands[b].processSampleL(L[s]);
                    break;
                case ChannelMode::Right:
                    for (int s = 0; s < numSamples; ++s)
                        R[s] = bands[b].processSampleR(R[s]);
                    break;
                case ChannelMode::Mid:
                    for (int s = 0; s < numSamples; ++s)
                    {
                        float m = (L[s] + R[s]) * 0.5f, sd = (L[s] - R[s]) * 0.5f;
                        L[s] = bands[b].processSampleL(m) + sd;
                        R[s] = L[s] - 2.f * sd;  // nm + sd=L[s], nm - sd = L[s] - 2*sd
                    }
                    break;
                case ChannelMode::Side:
                    for (int s = 0; s < numSamples; ++s)
                    {
                        float m = (L[s] + R[s]) * 0.5f, sd = (L[s] - R[s]) * 0.5f;
                        float ns = bands[b].processSampleR(sd);
                        L[s] = m + ns; R[s] = m - ns;
                    }
                    break;
                default: break;
            }
        }
    }
}

void MantisVexQProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI CC routing — handle learn + live control
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int idx = midiLearnIdx.load();
            if (idx >= 0)
            {
                midiCCMap[cc].store(idx);
                midiLearnIdx.store(-1);
            }
            else
            {
                const int pidx = midiCCMap[cc].load();
                if (pidx >= 0 && pidx < (int)paramPtrs.size())
                    paramPtrs[pidx]->setValueNotifyingHost(msg.getControllerValue() / 127.f);
            }
        }
    }
    midiMessages.clear();

    if (parametersChanged.exchange(false, std::memory_order_acq_rel))
    {
        const bool anyBandUpdated = updateDirtyBands();
        if (anyBandUpdated && *autoGainParam > 0.5f)
            currentAutoGain = computeAutoGain();
        else if (!(*autoGainParam > 0.5f))
            currentAutoGain = 0.f;
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

    // Capture dry for delta-monitoring mode (no heap alloc — buffer pre-sized in prepareToPlay)
    const bool deltaMode = *deltaParam > 0.5f;
    if (deltaMode)
    {
        for (int ch = 0; ch < juce::jmin(numChannels, dryBuf.getNumChannels()); ++ch)
            dryBuf.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // Capture sidechain bus if connected
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount(true) > 1)
    {
        auto scBuf = getBusBuffer(buffer, true, 1);
        if (scBuf.getNumChannels() > 0 && scBuf.getNumSamples() > 0)
        {
            scL = scBuf.getReadPointer(0);
            scR = scBuf.getNumChannels() > 1 ? scBuf.getReadPointer(1) : scL;
        }
    }

    const bool useLinPhase = linearPhasePrepared && (*linPhaseParam > 0.5f);

    if (useLinPhase)
    {
        // Linear phase: run through convolution engine (DYN/SC not supported in lin-phase mode)
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
        processBlockMinPhase(osL, osR, scL, scR, (int)osBlock.getNumSamples());

        oversampler->processSamplesDown(inputBlock);
    }
    else
    {
        if (L) processBlockMinPhase(L, R && R != L ? R : L, scL, scR, numSamples);
    }

    const float totalGain = outputGainParam->load() + currentAutoGain;
    buffer.applyGain(juce::Decibels::decibelsToGain(totalGain));

    // Solo monitoring — bandpass at the soloed band's frequency when monitor mode is on
    if (*monitorSoloParam > 0.5f && isAnySoloed())
    {
        int soloIdx = -1;
        for (int b = 0; b < kNumBands; ++b)
            if (soloStates[b].load(std::memory_order_relaxed) && bands[b].getParams().enabled)
                { soloIdx = b; break; }

        if (soloIdx >= 0)
        {
            const auto& bp = bands[soloIdx].getParams();
            if (soloIdx != lastMonitorBand || std::abs(bp.freq - lastMonitorFreq) > 0.5f)
            {
                const double kPiD = juce::MathConstants<double>::pi;
                double w0    = 2.0 * kPiD * static_cast<double>(bp.freq) / currentSampleRate;
                double sw    = std::sin(w0), cw = std::cos(w0);
                double q     = juce::jlimit(0.1, 10.0, static_cast<double>(bp.q));
                double alpha = sw / (2.0 * q), a0 = 1.0 + alpha;
                monitorCoeffs = { sw * 0.5 / a0, 0.0, -sw * 0.5 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
                monitorL.reset(); monitorR.reset();
                lastMonitorBand = soloIdx;
                lastMonitorFreq = bp.freq;
            }
            for (int s = 0; s < numSamples; ++s)
            {
                if (L) L[s] = static_cast<float>(monitorL.process(L[s], monitorCoeffs));
                if (R && R != L) R[s] = static_cast<float>(monitorR.process(R[s], monitorCoeffs));
            }
        }
    }

    // Delta mode — subtract dry signal so output = wet - dry (EQ diff only)
    if (deltaMode)
    {
        for (int ch = 0; ch < juce::jmin(numChannels, dryBuf.getNumChannels()); ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = dryBuf.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                wet[s] -= dry[s];
        }
    }

    // M/S output monitor — fold stereo to mid-only or side-only
    {
        const int msMode = (int)msMonitorParam->load();
        if (msMode == 1 && L && R && R != L)       // Mid only
        {
            for (int s = 0; s < numSamples; ++s) { float m=(L[s]+R[s])*0.5f; L[s]=R[s]=m; }
        }
        else if (msMode == 2 && L && R && R != L)  // Side only
        {
            for (int s = 0; s < numSamples; ++s) { float sd=(L[s]-R[s])*0.5f; L[s]=R[s]=sd; }
        }
    }

    // Capture POST-gain spectrum and output levels (includes output trim)
    if (L) spectrumPostL.pushSamples(L, numSamples);
    if (R && R != L) spectrumPostR.pushSamples(R, numSamples);

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
}

//==============================================================================
void MantisVexQProcessor::parameterChanged(const juce::String& paramID, float)
{
    // Mark only the changed band dirty rather than all 24
    if (paramID.startsWith("band"))
    {
        int bandNum = 0;
        for (int ci = 4; ci < paramID.length(); ++ci)
        {
            auto ch = paramID[ci];
            if (ch == '_') break;
            if (ch >= '0' && ch <= '9') bandNum = bandNum * 10 + (int)(ch - '0');
        }
        if (bandNum >= 1 && bandNum <= kNumBands)
            bandsDirtyMask.fetch_or(1u << (bandNum - 1), std::memory_order_relaxed);
    }

    parametersChanged.store(true, std::memory_order_relaxed);

    if (paramID == "oversample")
        oversampleNeedsRebuild.store(true, std::memory_order_relaxed);

    if (*linPhaseParam > 0.5f || paramID == "oversample")
        triggerAsyncUpdate();
}

void MantisVexQProcessor::handleAsyncUpdate()
{
    const bool needsOS       = oversampleNeedsRebuild.exchange(false, std::memory_order_relaxed);
    const bool needsLinPhase = (*linPhaseParam > 0.5f);

    if (needsOS || needsLinPhase)
    {
        // suspendProcessing ensures audio thread is idle while we read bands[] / rebuild
        suspendProcessing(true);
        if (needsOS)       rebuildOversampler();
        if (needsLinPhase) rebuildLinearPhaseIR();
        suspendProcessing(false);
    }
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
    const auto& c = bandParamCache[i];

    EQBandParams bp;
    bp.enabled  = *c.enabled  > 0.5f;
    bp.bypassed = *c.bypassed > 0.5f;
    bp.freq     = *c.freq;
    bp.gainDB   = *c.gain;
    bp.q        = *c.q;
    bp.type     = static_cast<FilterType>(static_cast<int>(*c.type));
    bp.order    = static_cast<int>(*c.slope) + 1;

    ChannelMode newMode = static_cast<ChannelMode>(static_cast<int>(*c.channel));

    bool resetNeeded = (bp.type != bands[i].getParams().type) || (newMode != channelModes[i]);
    bands[i].setParams(bp, currentSampleRate);
    channelModes[i] = newMode;
    if (resetNeeded) bands[i].reset();

    bool dynOn = *c.dyn > 0.5f;
    dynOnCache[i] = dynOn;
    scOnCache [i] = dynOn && (*c.dynSc > 0.5f);
    if (dynOn)
        dynBands[i].prepare(bp.freq, bp.q, *c.dynThr, *c.dynAtk, *c.dynRel, *c.dynRat, currentSampleRate);
    else
        dynBlendState[i].store(1.f, std::memory_order_relaxed);

    bandUpdateSeq.fetch_add(1, std::memory_order_relaxed);
}

bool MantisVexQProcessor::updateDirtyBands()
{
    uint32_t mask = bandsDirtyMask.exchange(0, std::memory_order_acquire);
    if (mask == 0) return false;
    for (int i = 0; i < kNumBands; ++i)
        if (mask & (1u << i))
            updateBand(i);
    return true;
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
    if      (gotL && gotR) for (int i = 0; i < SpectrumAnalyzer::kFFTSize; ++i) dest[i] = (dest[i] + tmpR[i]) * 0.5f;
    else if (!gotL && gotR) dest = tmpR;
    return gotL || gotR;
}

bool MantisVexQProcessor::getNextPostSpectrumData(std::array<float, SpectrumAnalyzer::kFFTSize>& dest)
{
    std::array<float, SpectrumAnalyzer::kFFTSize> tmpR;
    bool gotL = spectrumPostL.getNextFFTData(dest);
    bool gotR = spectrumPostR.getNextFFTData(tmpR);
    if      (gotL && gotR) for (int i = 0; i < SpectrumAnalyzer::kFFTSize; ++i) dest[i] = (dest[i] + tmpR[i]) * 0.5f;
    else if (!gotL && gotR) dest = tmpR;
    return gotL || gotR;
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

    // Embed MIDI CC map
    juce::ValueTree ccTree("MidiCC");
    for (int cc = 0; cc < kNumMidiCC; ++cc)
    {
        int idx = midiCCMap[cc].load();
        if (idx >= 0 && idx < (int)paramIDs.size())
        {
            juce::ValueTree e("e");
            e.setProperty("cc",  cc,             nullptr);
            e.setProperty("pid", paramIDs[idx],  nullptr);
            ccTree.addChild(e, -1, nullptr);
        }
    }
    state.addChild(ccTree, -1, nullptr);

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

        // Restore MIDI CC map
        auto ccTree = state.getChildWithName("MidiCC");
        if (ccTree.isValid())
        {
            state.removeChild(ccTree, nullptr);
            for (auto& a : midiCCMap) a.store(-1);
            for (auto e : ccTree)
            {
                int cc = (int)e.getProperty("cc", -1);
                juce::String pid = e.getProperty("pid", "");
                if (cc >= 0 && cc < kNumMidiCC)
                    assignMidiCC(cc, pid);
            }
        }

        apvts.replaceState(state);
        parametersChanged.store(true);
    }
}

//==============================================================================
// MIDI CC learn
void MantisVexQProcessor::startMidiLearn(const juce::String& paramID)
{
    for (int i = 0; i < (int)paramIDs.size(); ++i)
        if (paramIDs[i] == paramID) { midiLearnIdx.store(i); return; }
}

void MantisVexQProcessor::stopMidiLearn()
{
    midiLearnIdx.store(-1);
}

void MantisVexQProcessor::clearMidiCC(const juce::String& paramID)
{
    for (int i = 0; i < (int)paramIDs.size(); ++i)
        if (paramIDs[i] == paramID)
        {
            for (int cc = 0; cc < kNumMidiCC; ++cc)
                if (midiCCMap[cc].load() == i) midiCCMap[cc].store(-1);
            return;
        }
}

void MantisVexQProcessor::assignMidiCC(int cc, const juce::String& paramID)
{
    for (int i = 0; i < (int)paramIDs.size(); ++i)
        if (paramIDs[i] == paramID) { midiCCMap[cc].store(i); return; }
}

int MantisVexQProcessor::getMidiCC(const juce::String& paramID) const noexcept
{
    for (int i = 0; i < (int)paramIDs.size(); ++i)
        if (paramIDs[i] == paramID)
        {
            for (int cc = 0; cc < kNumMidiCC; ++cc)
                if (midiCCMap[cc].load() == i) return cc;
            return -1;
        }
    return -1;
}

juce::String MantisVexQProcessor::getMidiLearnParamID() const noexcept
{
    int idx = midiLearnIdx.load();
    if (idx >= 0 && idx < (int)paramIDs.size())
        return paramIDs[idx];
    return {};
}

//==============================================================================
juce::AudioProcessorEditor* MantisVexQProcessor::createEditor()
{
    return new MantisVexQEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MantisVexQProcessor();
}
