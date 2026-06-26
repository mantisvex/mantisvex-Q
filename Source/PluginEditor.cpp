#include "PluginEditor.h"

static constexpr int kTitleH  = 44;
static constexpr int kStripH  = 200;   // taller knobs + tab row
static constexpr int kInfoH   = 22;
static constexpr int kEditorW = 920;
static constexpr int kEditorH = 582;

//==============================================================================
static void styleKnob(juce::Slider& s, juce::Colour accent, const juce::String& suffix = {})
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setColour(juce::Slider::rotarySliderFillColourId,    accent);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff0c0d1e));
    s.setColour(juce::Slider::thumbColourId,               accent);
    s.setColour(juce::Slider::textBoxTextColourId,         juce::Colour(0xff9999cc));
    s.setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0xff05060d));
    s.setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxHighlightColourId,    accent.withAlpha(0.25f));
    if (suffix.isNotEmpty()) s.setTextValueSuffix(suffix);
}

static void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId,     juce::Colour(0xff0b0c1c));
    c.setColour(juce::ComboBox::textColourId,           juce::Colour(0xffaaaacc));
    c.setColour(juce::ComboBox::arrowColourId,          juce::Colour(0xff5555aa));
    c.setColour(juce::ComboBox::outlineColourId,        juce::Colour(0xff181830));
    c.setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xff282848));
}

static void styleLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setFont(juce::Font(juce::FontOptions().withName("Segoe UI").withHeight(9.5f)));
    l.setColour(juce::Label::textColourId, juce::Colour(0xff7878a8));
    l.setJustificationType(juce::Justification::centred);
}

static void styleBtn(juce::TextButton& b, juce::Colour accent, const juce::String& text)
{
    b.setButtonText(text);
    b.setClickingTogglesState(true);
    b.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff0d0e1c));
    b.setColour(juce::TextButton::buttonOnColourId,  accent);
    b.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xff686888));
    b.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
}

//==============================================================================
// BandControlStrip
//==============================================================================

BandControlStrip::BandControlStrip(MantisVexQProcessor& p) : processor(p)
{
    auto addLabel = [this](juce::Label& l, const char* t) {
        styleLabel(l, t); addAndMakeVisible(l);
    };
    addLabel(labelFreq,    "FREQ");
    addLabel(labelGain,    "GAIN");
    addLabel(labelQ,       "Q");
    addLabel(labelType,    "TYPE");
    addLabel(labelSlope,   "SLOPE");
    addLabel(labelChannel, "CHANNEL");
    addLabel(labelDynThr,  "THRESH");
    addLabel(labelDynAtk,  "ATTACK");
    addLabel(labelDynRel,  "RELEASE");
    addLabel(labelDynRat,  "RATIO");

    styleKnob(sliderFreq,   juce::Colour(0xff5c9eff), " Hz");
    styleKnob(sliderGain,   juce::Colour(0xffffaa22), " dB");
    styleKnob(sliderQ,      juce::Colour(0xff8bc34a), "");
    styleKnob(sliderDynThr, juce::Colour(0xff00b896), " dB");
    styleKnob(sliderDynAtk, juce::Colour(0xff00b896), " ms");
    styleKnob(sliderDynRel, juce::Colour(0xff00b896), " ms");
    styleKnob(sliderDynRat, juce::Colour(0xff00b896), ":1");
    for (auto* s : { &sliderFreq, &sliderGain, &sliderQ,
                     &sliderDynThr, &sliderDynAtk, &sliderDynRel, &sliderDynRat })
        addAndMakeVisible(*s);

    for (auto* c : { &comboType, &comboSlope, &comboChannel })
    { styleCombo(*c); addAndMakeVisible(*c); }

    comboType.addItem("Bell",       1); comboType.addItem("Low Shelf",  2);
    comboType.addItem("High Shelf", 3); comboType.addItem("Low Cut",    4);
    comboType.addItem("High Cut",   5); comboType.addItem("Notch",      6);
    comboType.addItem("Band Pass",  7); comboType.addItem("All Pass",   8);
    comboType.addItem("Tilt Shelf", 9);

    comboSlope.addItem("12 dB/oct", 1); comboSlope.addItem("24 dB/oct", 2);
    comboSlope.addItem("36 dB/oct", 3); comboSlope.addItem("48 dB/oct", 4);

    comboChannel.addItem("Stereo", 1); comboChannel.addItem("Left",  2);
    comboChannel.addItem("Right",  3); comboChannel.addItem("Mid",   4);
    comboChannel.addItem("Side",   5);

    styleBtn(btnEnabled, juce::Colour(0xff5c9eff), "ON");
    styleBtn(btnBypass,  juce::Colour(0xffffaa22), "BYP");
    styleBtn(btnSolo,    juce::Colour(0xffff5252), "SOLO");
    styleBtn(btnDyn,     juce::Colour(0xff00b896), "DYN");
    styleBtn(btnScDyn,   juce::Colour(0xff00b896), "SC");

    btnSolo.onClick = [this] {
        if (activeBand >= 0) processor.setSoloBand(activeBand, btnSolo.getToggleState());
    };

    for (auto* b : { &btnEnabled, &btnBypass, &btnSolo, &btnDyn, &btnScDyn })
        addAndMakeVisible(*b);

    setActiveBand(-1);
    startTimerHz(10);

    // Capture right-clicks on child sliders for MIDI learn
    addMouseListener(this, true);
}

BandControlStrip::~BandControlStrip()
{
    stopTimer();
    disconnectFromParams();
}

void BandControlStrip::timerCallback()
{
    if (activeBand >= 0)
    {
        bool soloed = processor.isBandSoloed(activeBand);
        if (btnSolo.getToggleState() != soloed)
            btnSolo.setToggleState(soloed, juce::dontSendNotification);

        FilterType type       = processor.getBand(activeBand).getParams().type;
        bool gainRelevant     = !filterTypeIgnoresGain(type);
        bool slopeRelevant    = (type == FilterType::LowCut || type == FilterType::HighCut);

        sliderGain.setEnabled(gainRelevant);
        labelGain .setEnabled(gainRelevant);
        comboSlope.setEnabled(slopeRelevant);
        labelSlope.setEnabled(slopeRelevant);
    }

    // Repaint when MIDI learn status changes (so the CC# indicators stay current)
    bool nowLearning = processor.isMidiLearning();
    if (nowLearning != wasLearning) { wasLearning = nowLearning; repaint(); }
}

void BandControlStrip::mouseDown(const juce::MouseEvent& e)
{
    // Right-click on any slider → MIDI learn menu
    if (e.mods.isRightButtonDown())
    {
        if (auto* sl = dynamic_cast<juce::Slider*>(e.eventComponent))
        {
            juce::String pid = getParamIDForSlider(sl);
            if (pid.isNotEmpty()) { showMidiLearnMenu(pid); return; }
        }
    }

    // Remaining handling only applies to clicks on the BandControlStrip itself (tab row)
    if (e.eventComponent != this) return;
    if (e.y >= kTabH) return;

    const int tabIndex = juce::jlimit(0, 23,
        (int)(e.x / ((float)getWidth() / 24.f)));

    if (onBandSelected)
        onBandSelected(tabIndex);
}

juce::String BandControlStrip::getParamIDForSlider(juce::Slider* sl) const
{
    if (activeBand < 0 || sl == nullptr) return {};
    juce::String px = "band" + juce::String(activeBand + 1) + "_";
    if (sl == &sliderFreq)   return px + "freq";
    if (sl == &sliderGain)   return px + "gain";
    if (sl == &sliderQ)      return px + "q";
    if (sl == &sliderDynThr) return px + "dyn_thr";
    if (sl == &sliderDynAtk) return px + "dyn_atk";
    if (sl == &sliderDynRel) return px + "dyn_rel";
    if (sl == &sliderDynRat) return px + "dyn_rat";
    return {};
}

void BandControlStrip::showMidiLearnMenu(const juce::String& paramID)
{
    int currentCC = processor.getMidiCC(paramID);
    juce::PopupMenu menu;
    menu.addItem(1, "Assign MIDI CC" + (currentCC >= 0 ? " (currently CC#" + juce::String(currentCC) + ")" : ""));
    menu.addItem(2, "Clear MIDI CC", currentCC >= 0);

    juce::Component::SafePointer<BandControlStrip> safe(this);
    juce::String pid = paramID;
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, pid](int result) {
        if (safe == nullptr) return;
        if (result == 1) safe->processor.startMidiLearn(pid);
        if (result == 2) safe->processor.clearMidiCC(pid);
        safe->repaint();
    });
}

void BandControlStrip::disconnectFromParams()
{
    attachFreq.reset(); attachGain.reset(); attachQ.reset();
    attachType.reset(); attachSlope.reset(); attachChannel.reset();
    attachEnabled.reset(); attachBypass.reset(); attachDyn.reset(); attachScDyn.reset();
    attachDynThr.reset(); attachDynAtk.reset(); attachDynRel.reset(); attachDynRat.reset();
}

void BandControlStrip::connectToParams()
{
    disconnectFromParams();
    if (activeBand < 0) return;

    juce::String px = "band" + juce::String(activeBand + 1) + "_";
    auto& av = processor.getAPVTS();
    attachFreq    = std::make_unique<APVTS::SliderAttachment>(av, px+"freq",     sliderFreq);
    attachGain    = std::make_unique<APVTS::SliderAttachment>(av, px+"gain",     sliderGain);
    attachQ       = std::make_unique<APVTS::SliderAttachment>(av, px+"q",        sliderQ);
    attachType    = std::make_unique<APVTS::ComboBoxAttachment>(av, px+"type",   comboType);
    attachSlope   = std::make_unique<APVTS::ComboBoxAttachment>(av, px+"slope",  comboSlope);
    attachChannel = std::make_unique<APVTS::ComboBoxAttachment>(av, px+"channel",comboChannel);
    attachEnabled = std::make_unique<APVTS::ButtonAttachment>(av, px+"enabled",  btnEnabled);
    attachBypass  = std::make_unique<APVTS::ButtonAttachment>(av, px+"bypassed", btnBypass);
    attachDyn     = std::make_unique<APVTS::ButtonAttachment>(av, px+"dyn",      btnDyn);
    attachScDyn   = std::make_unique<APVTS::ButtonAttachment>(av, px+"dyn_sc",   btnScDyn);
    attachDynThr  = std::make_unique<APVTS::SliderAttachment>(av, px+"dyn_thr",  sliderDynThr);
    attachDynAtk  = std::make_unique<APVTS::SliderAttachment>(av, px+"dyn_atk",  sliderDynAtk);
    attachDynRel  = std::make_unique<APVTS::SliderAttachment>(av, px+"dyn_rel",  sliderDynRel);
    attachDynRat  = std::make_unique<APVTS::SliderAttachment>(av, px+"dyn_rat",  sliderDynRat);
}

void BandControlStrip::setActiveBand(int bi)
{
    activeBand = bi;
    connectToParams();

    bool has = (bi >= 0);
    for (auto* c : std::initializer_list<juce::Component*>{
            &sliderFreq,&sliderGain,&sliderQ,
            &comboType,&comboSlope,&comboChannel,
            &btnEnabled,&btnBypass,&btnSolo,&btnDyn,&btnScDyn,
            &sliderDynThr,&sliderDynAtk,&sliderDynRel,&sliderDynRat })
        c->setEnabled(has);

    if (has) btnSolo.setToggleState(processor.isBandSoloed(bi), juce::dontSendNotification);
    repaint();
}

void BandControlStrip::resized()
{
    const int margin     = 14;
    const int sectionGap = 14;
    const int elemGap    = 8;
    const int btnW       = 50;
    const int btnH       = 22;
    const int btnGap     = 5;
    const int lh         = 13;
    const int h          = getHeight();
    const int rowH       = (h - 8 - kTabH) / 2;  // two rows below the tab strip
    const int y1         = 4 + kTabH;
    const int y2         = y1 + rowH;
    int bx = margin;

    // 5 Toggle buttons: ON, BYP, SOLO, DYN, SC (DYN + SC share last row)
    const int totalControlH = h - 8 - kTabH;
    const int totalBtnH     = 4 * btnH + 3 * btnGap;
    const int btnY          = y1 + (totalControlH - totalBtnH) / 2;
    const int scW           = 22;
    const int dynW          = btnW - scW - 2;
    btnEnabled.setBounds(bx, btnY,                   btnW,  btnH);
    btnBypass.setBounds (bx, btnY + btnH + btnGap,   btnW,  btnH);
    btnSolo.setBounds   (bx, btnY + 2*(btnH+btnGap), btnW,  btnH);
    btnDyn.setBounds    (bx, btnY + 3*(btnH+btnGap), dynW,  btnH);
    btnScDyn.setBounds  (bx + dynW + 2, btnY + 3*(btnH+btnGap), scW, btnH);
    bx += btnW + sectionGap;

    // Row 1: Freq, Gain, Q knobs + Type, Slope, Channel combos
    const int totalCtrlW = getWidth() - bx - margin - sectionGap - 4 * elemGap;
    const int ctrlW      = totalCtrlW / 6;

    juce::Label*  kLabels[]  = { &labelFreq, &labelGain, &labelQ };
    juce::Slider* kSliders[] = { &sliderFreq, &sliderGain, &sliderQ };
    for (int k = 0; k < 3; ++k)
    {
        kLabels[k]->setBounds(bx, y1, ctrlW, lh);
        kSliders[k]->setBounds(bx, y1 + lh, ctrlW, rowH - lh - 16);
        valX[k] = bx;
        bx += ctrlW + (k < 2 ? elemGap : sectionGap);
    }
    valW = ctrlW;
    valY = y1 + rowH - 14;

    juce::Label*    cLabels[] = { &labelType, &labelSlope, &labelChannel };
    juce::ComboBox* combos[]  = { &comboType, &comboSlope, &comboChannel };
    const int comboH = 26;
    const int comboY = y1 + lh + (rowH - lh - comboH) / 2;
    for (int c = 0; c < 3; ++c)
    {
        cLabels[c]->setBounds(bx, y1, ctrlW, lh);
        combos[c]->setBounds(bx, comboY, ctrlW, comboH);
        bx += ctrlW + (c < 2 ? elemGap : 0);
    }

    // Row 2: Dynamic EQ controls (Thresh, Attack, Release, Ratio)
    bx = margin + btnW + sectionGap;
    const int dynKnobH = rowH - lh - 14;
    juce::Label*  dLabels[]  = { &labelDynThr, &labelDynAtk, &labelDynRel, &labelDynRat };
    juce::Slider* dSliders[] = { &sliderDynThr, &sliderDynAtk, &sliderDynRel, &sliderDynRat };
    const int dw = ctrlW;
    for (int k = 0; k < 4; ++k)
    {
        dLabels[k]->setBounds(bx, y2, dw, lh);
        dSliders[k]->setBounds(bx, y2 + lh, dw, dynKnobH);
        dynValX[k] = bx;
        bx += dw + elemGap;
    }
    dynValY = y2 + lh + dynKnobH;
}

void BandControlStrip::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    juce::ColourGradient bg(juce::Colour(0xff0b0c18), 0.f, 0.f,
                             juce::Colour(0xff080910), 0.f, h, false);
    g.setGradientFill(bg);
    g.fillAll();

    // ---- Tab row (always drawn) ----
    {
        g.setColour(juce::Colour(0xff070810));
        g.fillRect(0.f, 0.f, w, (float)kTabH);

        const float tabW = w / 24.f;

        // Helper: format freq for a compact tab label ("1k", "200", "12k")
        auto fmtTabFreq = [](float freq) -> juce::String {
            if (freq >= 10000.f) return juce::String((int)(freq / 1000.f + 0.5f)) + "k";
            if (freq >= 1000.f)  return juce::String(freq / 1000.f, 1) + "k";
            return juce::String((int)(freq + 0.5f));
        };

        for (int i = 0; i < 24; ++i)
        {
            float tx = i * tabW;
            juce::Rectangle<float> tabR(tx, 0.f, tabW, (float)kTabH);

            bool enabled = false;
            if (auto* p = processor.getAPVTS().getRawParameterValue(
                    "band" + juce::String(i + 1) + "_enabled"))
                enabled = *p > 0.5f;

            float tabFreq = 1000.f;
            if (enabled || i == activeBand)
                if (auto* fp = processor.getAPVTS().getRawParameterValue("band" + juce::String(i + 1) + "_freq"))
                    tabFreq = *fp;

            const juce::Colour bc = getBandColor(i);
            const bool isActive   = (i == activeBand);

            // Upper portion for the band number, lower 8px for freq label
            juce::Rectangle<int> numR((int)tx, 0, (int)tabW, kTabH - 8);
            juce::Rectangle<int> fqR ((int)tx, kTabH - 9, (int)tabW, 8);

            if (isActive)
            {
                g.setColour(bc.withAlpha(0.20f));
                g.fillRect(tabR);
                g.setColour(bc.withAlpha(0.90f));
                g.fillRect(tx, 0.f, tabW, 2.f);
                g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.f)));
                g.setColour(bc.brighter(0.4f));
                g.drawText(juce::String(i + 1), numR, juce::Justification::centred, false);
                g.setFont(juce::Font(juce::FontOptions().withHeight(7.f)));
                g.setColour(bc.withAlpha(0.75f));
                g.drawText(fmtTabFreq(tabFreq), fqR, juce::Justification::centred, false);
            }
            else if (enabled)
            {
                g.setColour(bc.withAlpha(0.07f));
                g.fillRect(tabR);
                g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.f)));
                g.setColour(bc.withAlpha(0.65f));
                g.drawText(juce::String(i + 1), numR, juce::Justification::centred, false);
                g.setFont(juce::Font(juce::FontOptions().withHeight(7.f)));
                g.setColour(bc.withAlpha(0.45f));
                g.drawText(fmtTabFreq(tabFreq), fqR, juce::Justification::centred, false);
            }
            else
            {
                g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.f)));
                g.setColour(juce::Colour(0xff252540));
                g.drawText(juce::String(i + 1), tabR.toNearestInt(),
                           juce::Justification::centred, false);
            }

            // Tab separator
            if (i < 23)
            {
                g.setColour(juce::Colour(0xff111126));
                g.drawVerticalLine((int)(tx + tabW), 3.f, (float)kTabH - 3.f);
            }
        }

        // Bottom edge of tab row
        g.setColour(juce::Colour(0xff141428));
        g.fillRect(0.f, (float)kTabH - 1.f, w, 1.f);
    }

    if (activeBand >= 0)
    {
        juce::Colour bc = getBandColor(activeBand);

        g.setColour(bc.withAlpha(0.08f));
        g.fillRect(0.f, (float)kTabH, w, h - kTabH);

        g.setColour(bc.withAlpha(0.90f));
        g.fillRect(0.f, (float)kTabH, w, 2.f);
        g.setColour(bc.withAlpha(0.22f));
        g.fillRect(0.f, (float)kTabH + 2.f, w, 4.f);

        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.5f)));
        g.setColour(bc.withAlpha(0.65f));
        g.drawText("BAND " + juce::String(activeBand + 1),
                   8, kTabH + 6, 60, 14, juce::Justification::centredLeft);

        // Divider between rows (midpoint of the controls area below tabs)
        const float midY = kTabH + (h - kTabH) * 0.5f;
        g.setColour(juce::Colour(0xff181830));
        g.fillRect(0.f, midY - 0.5f, w, 1.f);

        // "DYN" section label in row 2 if dynamic is active
        bool dynOn = activeBand >= 0 && processor.isBandDynEnabled(activeBand);
        if (dynOn)
        {
            g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.5f)));
            g.setColour(juce::Colour(0xff00b896).withAlpha(0.55f));
            g.drawText("// dynamic", 8, (int)midY + 2, 80, 12, juce::Justification::centredLeft);
        }
        else
        {
            g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.5f)));
            g.setColour(juce::Colour(0xff282840));
            g.drawText("// dynamic off", 8, (int)midY + 2, 90, 12, juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setColour(juce::Colour(0xff141428));
        g.fillRect(0.f, (float)kTabH, w, 1.f);
        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(10.f)));
        g.setColour(juce::Colour(0xff282840));
        g.drawText("// select a band", 0, kTabH, getWidth(), getHeight() - kTabH,
                   juce::Justification::centred);
    }

    // Vertical divider after button column
    if (activeBand >= 0)
    {
        const int btnColRight = 14 + 50 + 8;
        g.setColour(juce::Colour(0xff1e1e38));
        g.drawVerticalLine(btnColRight, 6.f, h - 6.f);
    }

    // Value readouts
    if (activeBand >= 0 && valW > 0)
    {
        const auto& params = processor.getBand(activeBand).getParams();
        juce::Colour bc    = getBandColor(activeBand);
        bool bypassed      = processor.isBandBypassed(activeBand);
        auto valCol        = bypassed ? juce::Colour(0xff3a3a58) : bc;

        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(10.5f)));

        juce::String freqStr;
        if      (params.freq >= 10000.f) freqStr = juce::String(params.freq / 1000.f, 1) + "k";
        else if (params.freq >= 1000.f)  freqStr = juce::String(params.freq / 1000.f, 2) + "k";
        else                              freqStr = juce::String(static_cast<int>(params.freq)) + "Hz";
        g.setColour(valCol.withAlpha(0.90f));
        g.drawText(freqStr, valX[0], valY, valW, 14, juce::Justification::centred);

        if (!filterTypeIgnoresGain(params.type))
        {
            auto gainStr = (params.gainDB >= 0.f ? juce::String("+") : juce::String(""))
                           + juce::String(params.gainDB, 1) + "dB";
            g.setColour(valCol.withAlpha(0.90f));
            g.drawText(gainStr, valX[1], valY, valW, 14, juce::Justification::centred);
        }

        g.setColour(valCol.withAlpha(0.75f));
        g.drawText("Q " + juce::String(params.q, 2), valX[2], valY, valW, 14, juce::Justification::centred);
    }

    // DYN value readouts (Thresh / Attack / Release / Ratio)
    if (activeBand >= 0 && dynValY > 0)
    {
        juce::String px = "band" + juce::String(activeBand + 1) + "_";
        bool dynOn  = processor.isBandDynEnabled(activeBand);
        juce::Colour dynCol = dynOn ? juce::Colour(0xff00b896).withAlpha(0.80f)
                                    : juce::Colour(0xff252540);

        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(9.5f)));

        float thrV  = *processor.getAPVTS().getRawParameterValue(px + "dyn_thr");
        float atkV  = *processor.getAPVTS().getRawParameterValue(px + "dyn_atk");
        float relV  = *processor.getAPVTS().getRawParameterValue(px + "dyn_rel");
        float ratV  = *processor.getAPVTS().getRawParameterValue(px + "dyn_rat");

        auto fmtMs = [](float v) -> juce::String {
            return v < 10.f ? juce::String(v, 1) + "ms" : juce::String((int)std::round(v)) + "ms";
        };

        juce::String vals[4] = {
            juce::String((int)std::round(thrV)) + "dB",
            fmtMs(atkV),
            fmtMs(relV),
            juce::String(ratV, 1) + ":1"
        };
        for (int k = 0; k < 4; ++k)
        {
            g.setColour(dynCol);
            g.drawText(vals[k], dynValX[k], dynValY, valW, 13, juce::Justification::centred);
        }
    }

    // MIDI CC indicators — small CC# tag below each knob value readout
    if (activeBand >= 0 && valW > 0)
    {
        const juce::String paramSfx[] = { "freq", "gain", "q" };
        juce::String bpx = "band" + juce::String(activeBand + 1) + "_";
        g.setFont(juce::Font(juce::FontOptions().withHeight(7.f)));

        for (int k = 0; k < 3; ++k)
        {
            juce::String pid = bpx + paramSfx[k];
            bool learning = processor.isMidiLearning() && processor.getMidiLearnParamID() == pid;
            int  cc       = processor.getMidiCC(pid);

            if (learning)
            {
                g.setColour(juce::Colour(0xffffaa22).withAlpha(0.85f));
                g.drawText("LEARN", valX[k], valY + 14, valW, 8, juce::Justification::centred);
            }
            else if (cc >= 0)
            {
                g.setColour(juce::Colour(0xff33bb66).withAlpha(0.80f));
                g.drawText("CC" + juce::String(cc), valX[k], valY + 14, valW, 8, juce::Justification::centred);
            }
        }

        // DYN param CC indicators
        if (dynValY > 0)
        {
            const juce::String dynSfx[] = { "dyn_thr", "dyn_atk", "dyn_rel", "dyn_rat" };
            for (int k = 0; k < 4; ++k)
            {
                juce::String pid = bpx + dynSfx[k];
                bool learning = processor.isMidiLearning() && processor.getMidiLearnParamID() == pid;
                int  cc       = processor.getMidiCC(pid);

                if (learning)
                {
                    g.setColour(juce::Colour(0xffffaa22).withAlpha(0.85f));
                    g.drawText("LEARN", dynValX[k], dynValY + 13, valW, 8, juce::Justification::centred);
                }
                else if (cc >= 0)
                {
                    g.setColour(juce::Colour(0xff33bb66).withAlpha(0.80f));
                    g.drawText("CC" + juce::String(cc), dynValX[k], dynValY + 13, valW, 8, juce::Justification::centred);
                }
            }
        }
    }
}

//==============================================================================
// MantisVexQEditor
//==============================================================================

MantisVexQEditor::MantisVexQEditor(MantisVexQProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      eqDisplay(p),
      bandStrip(p),
      outputGainAttach (p.getAPVTS(), "output_gain",   outputGainSlider),
      specPostAttach   (p.getAPVTS(), "spectrum_post",  btnSpecPost),
      autoGainAttach   (p.getAPVTS(), "auto_gain",      btnAutoGain),
      linPhaseAttach   (p.getAPVTS(), "lin_phase",      btnLinPhase),
      monitorAttach    (p.getAPVTS(), "monitor_solo",   btnMonitor),
      deltaAttach      (p.getAPVTS(), "delta",           btnDelta),
      oversampleAttach (p.getAPVTS(), "oversample",     comboOversample),
      msMonitorAttach  (p.getAPVTS(), "ms_monitor",     comboMsMonitor)
{
    setLookAndFeel(&lnf);

    setSize(kEditorW, kEditorH);
    setResizable(true, true);
    setResizeLimits(640, 380, 1800, 1100);
    addKeyListener(this);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(eqDisplay);
    eqDisplay.onBandSelected = [this](int b) { bandStrip.setActiveBand(b); };

    addAndMakeVisible(bandStrip);
    bandStrip.onBandSelected = [this](int b) {
        eqDisplay.setSelectedBand(b);
        bandStrip.setActiveBand(b);
    };

    styleBtn(btnSpecPost,  juce::Colour(0xff5c9eff), "POST EQ");
    styleBtn(btnAutoGain,  juce::Colour(0xff8bc34a), "AUTO GAIN");
    styleBtn(btnLinPhase,  juce::Colour(0xffaa88ff), "LIN PHASE");
    styleBtn(btnMonitor,   juce::Colour(0xffff8844), "MON SOLO");
    styleBtn(btnDelta,     juce::Colour(0xffff4466), "DELTA");
    addAndMakeVisible(btnSpecPost);
    addAndMakeVisible(btnAutoGain);
    addAndMakeVisible(btnLinPhase);
    addAndMakeVisible(btnMonitor);
    addAndMakeVisible(btnDelta);

    styleCombo(comboOversample);
    comboOversample.addItem("1x OS", 1);
    comboOversample.addItem("2x OS", 2);
    comboOversample.addItem("4x OS", 3);
    comboOversample.addItem("8x OS", 4);
    addAndMakeVisible(comboOversample);

    styleCombo(comboMsMonitor);
    comboMsMonitor.addItem("ST", 1);
    comboMsMonitor.addItem("M",  2);
    comboMsMonitor.addItem("S",  3);
    addAndMakeVisible(comboMsMonitor);

    styleKnob(outputGainSlider, juce::Colour(0xff5c9eff), " dB");
    outputGainSlider.setDoubleClickReturnValue(true, 0.0);
    addAndMakeVisible(outputGainSlider);
    styleLabel(outputGainLabel, "OUT GAIN");
    outputGainLabel.setColour(juce::Label::textColourId, juce::Colour(0xff50507a));
    addAndMakeVisible(outputGainLabel);

    // A/B comparison buttons
    styleBtn(btnA, juce::Colour(0xff5c9eff), "A");
    styleBtn(btnB, juce::Colour(0xff5c9eff), "B");
    btnA.setClickingTogglesState(false);
    btnB.setClickingTogglesState(false);
    auto syncABButtons = [this] {
        btnA.setToggleState(audioProcessor.getActiveABSlot() == 0 && audioProcessor.hasABState(0),
                            juce::dontSendNotification);
        btnB.setToggleState(audioProcessor.getActiveABSlot() == 1 && audioProcessor.hasABState(1),
                            juce::dontSendNotification);
    };
    btnA.onClick = [this, syncABButtons] {
        if (audioProcessor.getActiveABSlot() == 1)
        {
            audioProcessor.copyToAB(1);
            if (!audioProcessor.hasABState(0))
                audioProcessor.copyToAB(0);
            audioProcessor.loadAB(0);
        }
        else
        {
            audioProcessor.copyToAB(0);
        }
        syncABButtons();
    };
    btnB.onClick = [this, syncABButtons] {
        if (audioProcessor.getActiveABSlot() == 0)
        {
            audioProcessor.copyToAB(0);
            if (!audioProcessor.hasABState(1))
                audioProcessor.copyToAB(1);
            audioProcessor.loadAB(1);
        }
        else
        {
            audioProcessor.copyToAB(1);
        }
        syncABButtons();
    };
    syncABButtons();
    addAndMakeVisible(btnA);
    addAndMakeVisible(btnB);

    infoLabel.setFont(juce::Font(juce::FontOptions().withName("Segoe UI").withHeight(9.5f)));
    infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff383858));
    infoLabel.setJustificationType(juce::Justification::centred);
    infoLabel.setText(
        "click: add  //  drag: freq/gain  //  scroll: Q  //  dbl-click: delete  //  E/B/D/S: toggle on/byp/dyn/solo  //  Ctrl+C/V: copy/paste  //  right-click: menu + MIDI learn",
        juce::dontSendNotification);
    addAndMakeVisible(infoLabel);

    latencyLabel.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(8.f)));
    latencyLabel.setColour(juce::Label::textColourId, juce::Colour(0xff444464));
    latencyLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(latencyLabel);

    startTimerHz(5);
}

MantisVexQEditor::~MantisVexQEditor()
{
    stopTimer();
    removeKeyListener(this);
    setLookAndFeel(nullptr);
}

bool MantisVexQEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        int sel = eqDisplay.getSelectedBand();
        if (sel >= 0)
        {
            audioProcessor.getUndoManager().beginNewTransaction();
            juce::String prefix = "band" + juce::String(sel + 1) + "_";
            if (auto* ep = audioProcessor.getAPVTS().getParameter(prefix + "enabled"))
                ep->setValueNotifyingHost(0.f);
            eqDisplay.setSelectedBand(-1);
            bandStrip.setActiveBand(-1);
        }
        return true;
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0))
    { audioProcessor.getUndoManager().undo(); return true; }
    if (key == juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0))
    { audioProcessor.getUndoManager().redo(); return true; }
    if (key == juce::KeyPress('c', juce::ModifierKeys::ctrlModifier, 0))
    {
        int sel = eqDisplay.getSelectedBand();
        if (sel >= 0) { eqDisplay.copyBand(sel); return true; }
    }
    if (key == juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0))
    {
        int sel = eqDisplay.getSelectedBand();
        if (sel >= 0 && eqDisplay.hasClipboard()) { eqDisplay.pasteBand(sel); return true; }
    }
    if (key == juce::KeyPress::escapeKey)
    { eqDisplay.setSelectedBand(-1); bandStrip.setActiveBand(-1); return true; }

    // Per-band toggle shortcuts
    {
        const int sel = eqDisplay.getSelectedBand();
        if (sel >= 0)
        {
            juce::String pfx = "band" + juce::String(sel + 1) + "_";
            auto& apvts = audioProcessor.getAPVTS();
            auto toggle = [&](const juce::String& id) {
                if (auto* p = apvts.getParameter(pfx + id))
                {
                    audioProcessor.getUndoManager().beginNewTransaction();
                    p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.f : 1.f);
                }
            };
            if (key == juce::KeyPress('e', 0, 0)) { toggle("enabled");  return true; }
            if (key == juce::KeyPress('b', 0, 0)) { toggle("bypassed"); return true; }
            if (key == juce::KeyPress('d', 0, 0)) { toggle("dyn");      return true; }
            if (key == juce::KeyPress('s', 0, 0))
            {
                bool nowSoloed = !audioProcessor.isBandSoloed(sel);
                audioProcessor.setSoloBand(sel, nowSoloed);
                return true;
            }
        }
    }
    return false;
}

void MantisVexQEditor::timerCallback()
{
    int lat = audioProcessor.getLatencySamples();
    if (lat > 0)
    {
        double ms = static_cast<double>(lat) / audioProcessor.getCurrentSampleRate() * 1000.0;
        latencyLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffaa22).withAlpha(0.60f));
        latencyLabel.setText("+" + juce::String(juce::roundToInt(ms)) + "ms", juce::dontSendNotification);
    }
    else
    {
        latencyLabel.setColour(juce::Label::textColourId, juce::Colour(0xff444464));
        latencyLabel.setText({}, juce::dontSendNotification);
    }
}

void MantisVexQEditor::paint(juce::Graphics& g)
{
    const float w  = static_cast<float>(getWidth());
    const float h  = static_cast<float>(getHeight());

    // Title bar
    {
        juce::ColourGradient tbg(juce::Colour(0xff060714), 0.f, 0.f,
                                  juce::Colour(0xff030409), 0.f, (float)kTitleH, false);
        g.setGradientFill(tbg);
        g.fillRect(0, 0, (int)w, kTitleH);

        // Subtle left-edge accent wash behind the logo
        juce::ColourGradient logoGlow(MantisColors::Accent.withAlpha(0.055f), 0.f, 0.f,
                                      MantisColors::Accent.withAlpha(0.0f), 220.f, 0.f, false);
        g.setGradientFill(logoGlow);
        g.fillRect(0.f, 0.f, 220.f, (float)kTitleH);
    }

    g.setColour(MantisColors::Accent.withAlpha(0.50f));
    g.fillRect(0.f, (float)kTitleH - 1.5f, w, 1.5f);
    g.setColour(MantisColors::Accent.withAlpha(0.10f));
    g.fillRect(0.f, (float)kTitleH - 4.f, w, 2.5f);

    // Brand mark — Orbitron Black (website font), letter-spaced
    {
        const juce::Font fOrb  = lnf.getOrbitronFont(14.f);
        const juce::Font fSub  (juce::FontOptions().withName("Consolas").withHeight(8.5f));
        const float letterGap  = 3.f;   // matches website letter-spacing: 3px
        const juce::String brand = "MANTIS VEX";
        const juce::String qStr  = "Q";

        // Measure spaced string width
        auto spacedWidth = [&](const juce::Font& f, const juce::String& s) -> float {
            juce::GlyphArrangement ga;
            ga.addLineOfText(f, s, 0.f, 0.f);
            return ga.getBoundingBox(0, -1, true).getWidth() + (s.length() - 1) * letterGap;
        };

        // Draw each character with spacing
        auto drawSpaced = [&](const juce::String& s, float x, float cy, float h) {
            juce::GlyphArrangement ga;
            ga.addLineOfText(fOrb, s, 0.f, 0.f);
            float curX = x;
            for (int i = 0; i < ga.getNumGlyphs(); ++i)
            {
                auto glyph = ga.getGlyph(i);
                float gW = glyph.getRight() - glyph.getLeft();
                juce::Path p;
                glyph.createPath(p);
                // Centre vertically
                auto bb = p.getBounds();
                p.applyTransform(juce::AffineTransform::translation(
                    curX - glyph.getLeft(), cy - bb.getCentreY()));
                g.fillPath(p);
                curX += gW + letterGap;
            }
        };

        // Triangle
        const float cy  = (float)kTitleH * 0.5f;
        const float th  = 9.f, tw = 8.f, bx = 14.f;
        juce::Path tri;
        tri.startNewSubPath(bx,           cy + th * 0.5f);
        tri.lineTo         (bx + tw * 0.5f, cy - th * 0.5f);
        tri.lineTo         (bx + tw,       cy + th * 0.5f);
        tri.closeSubPath();
        g.setColour(MantisColors::Accent);
        g.fillPath(tri);

        // "MANTIS VEX" spaced
        float textX = bx + tw + 8.f;
        g.setColour(MantisColors::Accent);
        drawSpaced(brand, textX, cy, (float)kTitleH);

        // "Q" in white, right after
        float qX = textX + spacedWidth(fOrb, brand) + 8.f;
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        drawSpaced(qStr, qX, cy, (float)kTitleH);

        // Sub comment
        g.setFont(fSub);
        g.setColour(juce::Colour(0xff484870));
        g.drawText("// parametric equalizer", (int)bx, kTitleH - 13, 260, 12,
                   juce::Justification::centredLeft);
    }

    g.setColour(juce::Colour(0xff080910));
    g.drawHorizontalLine((int)(h - kInfoH), 0.f, w);

    g.setColour(juce::Colour(0xff141428));
    g.drawRect(getLocalBounds(), 1);

    // Corner tick-marks
    const float tkLen = 14.f, tkW = 1.5f;
    auto lb = getLocalBounds().toFloat();
    g.setColour(MantisColors::Accent.withAlpha(0.75f));
    g.drawLine(lb.getX(),     lb.getY(),      lb.getX() + tkLen, lb.getY(),      tkW);
    g.drawLine(lb.getX(),     lb.getY(),      lb.getX(),         lb.getY() + tkLen, tkW);
    g.drawLine(lb.getRight(), lb.getY(),      lb.getRight() - tkLen, lb.getY(),   tkW);
    g.drawLine(lb.getRight(), lb.getY(),      lb.getRight(),     lb.getY() + tkLen, tkW);
    g.drawLine(lb.getX(),     lb.getBottom(), lb.getX() + tkLen, lb.getBottom(), tkW);
    g.drawLine(lb.getX(),     lb.getBottom(), lb.getX(),         lb.getBottom() - tkLen, tkW);
    g.drawLine(lb.getRight(), lb.getBottom(), lb.getRight() - tkLen, lb.getBottom(), tkW);
    g.drawLine(lb.getRight(), lb.getBottom(), lb.getRight(),     lb.getBottom() - tkLen, tkW);
}

void MantisVexQEditor::resized()
{
    const int w = getWidth(), h = getHeight();
    const int dispH = h - kTitleH - kStripH - kInfoH;

    eqDisplay.setBounds(0, kTitleH, w, dispH);
    bandStrip.setBounds(0, kTitleH + dispH, w, kStripH);
    infoLabel.setBounds(0, h - kInfoH, w - 320, kInfoH);

    // Title-bar controls, right to left
    const int btnH = 24, pad = 6;
    int rx = w - pad;

    // Out gain knob
    const int knobW = 52;
    rx -= knobW;
    outputGainLabel .setBounds(rx, 1, knobW, 12);
    outputGainSlider.setBounds(rx, 13, knobW, kTitleH - 14);
    rx -= pad;

    // Buttons right to left: POST EQ, AUTO GAIN, LIN PHASE, MON SOLO, DELTA, M/S, OS combo, [A] [B]
    const int btnW  = 72;
    const int smW   = 48;   // small buttons (DELTA, M/S combo)
    const int osW   = 58;
    const int abW   = 28;

    rx -= btnW;
    btnSpecPost.setBounds(rx, (kTitleH - btnH) / 2, btnW, btnH);
    rx -= pad;

    rx -= btnW;
    btnAutoGain.setBounds(rx, (kTitleH - btnH) / 2, btnW, btnH);
    rx -= pad;

    rx -= btnW;
    btnLinPhase.setBounds(rx, (kTitleH - btnH) / 2, btnW, btnH);
    rx -= pad;

    rx -= btnW;
    btnMonitor.setBounds(rx, (kTitleH - btnH) / 2, btnW, btnH);
    rx -= pad;

    rx -= smW;
    btnDelta.setBounds(rx, (kTitleH - btnH) / 2, smW, btnH);
    rx -= pad;

    rx -= smW;
    comboMsMonitor.setBounds(rx, (kTitleH - btnH) / 2, smW, btnH);
    rx -= pad;

    rx -= osW;
    comboOversample.setBounds(rx, (kTitleH - btnH) / 2, osW, btnH);
    latencyLabel.setBounds(rx, 34, osW, kTitleH - 34);
    rx -= pad;

    rx -= abW;
    btnB.setBounds(rx, (kTitleH - btnH) / 2, abW, btnH);
    rx -= 3;
    rx -= abW;
    btnA.setBounds(rx, (kTitleH - btnH) / 2, abW, btnH);
}
