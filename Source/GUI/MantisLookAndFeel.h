#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>

// Mantis Vex brand colours
namespace MantisColors {
    static const juce::Colour Bg      { 0xff03040a };   // near-black
    static const juce::Colour Surface { 0xff06070f };   // control surface
    static const juce::Colour Border  { 0xff0e0f1c };   // subtle border
    static const juce::Colour Dim     { 0xff272740 };   // inactive text
    static const juce::Colour Accent  { 0xff00b896 };   // teal brand primary
    static const juce::Colour Danger  { 0xffff3a3a };   // red (solo/danger)
    static const juce::Colour Warn    { 0xffffaa22 };   // amber (bypass / title)
}

class MantisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MantisLookAndFeel()
    {
        orbitronTypeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::OrbitronBlack_ttf, BinaryData::OrbitronBlack_ttfSize);

        // Popup menu colours
        setColour(juce::PopupMenu::backgroundColourId,            juce::Colour(0xff04050e));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(0xff9999bb));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff0c0d1e));
        setColour(juce::PopupMenu::highlightedTextColourId,       MantisColors::Accent);
        setColour(juce::ScrollBar::thumbColourId,                 juce::Colour(0xff111128));
    }

    //==========================================================================
    // Rotary knob — layered 3-D style matching MASS plugin aesthetics
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        const float cx = (float)x + (float)width  * 0.5f;
        const float cy = (float)y + (float)height * 0.5f;
        const float r  = juce::jmin((float)width, (float)height) * 0.5f - 3.f;
        if (r < 6.f) return;

        const float angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto  accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const float arcR   = r - 4.f;
        const float bodyR  = r - 10.f;
        const float innerR = bodyR * 0.68f;

        // Outer ambient glow — value-dependent (MASS signature)
        if (sliderPos > 0.03f)
        {
            const float gr = r + 10.f;
            juce::ColourGradient outerGlow(accent.withAlpha(sliderPos * 0.13f), cx, cy,
                                           accent.withAlpha(0.f), cx + gr, cy, true);
            g.setGradientFill(outerGlow);
            g.fillEllipse(cx - gr, cy - gr, gr * 2.f, gr * 2.f);
        }

        // Arc track
        {
            juce::Path t;
            t.addCentredArc(cx, cy, arcR, arcR, 0.f, startAngle, endAngle, true);
            g.setColour(juce::Colour(0xff0d0e20));
            g.strokePath(t, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        }

        // Value arc fill
        if (sliderPos > 0.001f)
        {
            juce::Path v;
            v.addCentredArc(cx, cy, arcR, arcR, 0.f, startAngle, angle, true);
            g.setColour(accent);
            g.strokePath(v, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

            // Bright tip cap
            const float tx = cx + arcR * std::sin(angle);
            const float ty = cy - arcR * std::cos(angle);
            g.setColour(accent.withAlpha(0.35f));
            g.fillEllipse(tx - 4.5f, ty - 4.5f, 9.f, 9.f);
            g.setColour(accent.brighter(0.5f));
            g.fillEllipse(tx - 2.5f, ty - 2.5f, 5.f, 5.f);
        }

        // Knob body — radial gradient for 3-D feel (MASS style)
        {
            juce::ColourGradient bodyG(
                juce::Colour(0xff1e2038), cx - bodyR * 0.28f, cy - bodyR * 0.28f,
                juce::Colour(0xff090a16), cx + bodyR * 0.32f, cy + bodyR * 0.32f, false);
            g.setGradientFill(bodyG);
            g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.f, bodyR * 2.f);
        }
        g.setColour(sliderPos > 0.01f ? accent.withAlpha(0.28f) : juce::Colour(0xff181830));
        g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2.f, bodyR * 2.f, 0.8f);

        // Inner glow when active
        if (sliderPos > 0.04f)
        {
            juce::ColourGradient glow(accent.withAlpha(sliderPos * 0.20f), cx, cy,
                                       accent.withAlpha(0.f), cx + innerR, cy, true);
            g.setGradientFill(glow);
            g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.f, innerR * 2.f);
        }

        // Inner ring — dark centre disc
        g.setColour(juce::Colour(0xff07080e));
        g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.f, innerR * 2.f);
        g.setColour(sliderPos > 0.01f ? accent.withAlpha(0.18f) : juce::Colour(0xff131325));
        g.drawEllipse(cx - innerR, cy - innerR, innerR * 2.f, innerR * 2.f, 0.7f);

        // Pointer line + bright dot tip (MASS style)
        if (!slider.isMouseButtonDown())
        {
            const float px = cx + (innerR - 4.f) * std::sin(angle);
            const float py = cy - (innerR - 4.f) * std::cos(angle);
            g.setColour(sliderPos > 0.001f ? accent : juce::Colour(0xff252540));
            g.drawLine(cx + innerR * 0.26f * std::sin(angle),
                       cy - innerR * 0.26f * std::cos(angle),
                       px, py, 2.0f);
            g.setColour(sliderPos > 0.001f ? accent.brighter(0.3f) : juce::Colour(0xff1e1f35));
            g.fillEllipse(px - 3.8f, py - 3.8f, 7.6f, 7.6f);
        }

        // Value text inside the inner disc: full display while dragging, dim on hover
        const bool isDown = slider.isMouseButtonDown();
        const bool isHov  = slider.isMouseOver() && !isDown;
        if (isDown || isHov)
        {
            auto   suffix = slider.getTextValueSuffix();
            double val    = slider.getValue();
            juce::String valStr;

            if      (suffix == " Hz")
            {
                if      (val >= 10000.0) valStr = juce::String(val / 1000.0, 1) + "k";
                else if (val >= 1000.0)  valStr = juce::String(val / 1000.0, 2) + "k";
                else                     valStr = juce::String((int)std::round(val));
            }
            else if (suffix == " dB") { valStr = (val >= 0.0 ? "+" : "") + juce::String(val, 1); }
            else if (suffix == " ms") { valStr = val < 10.0 ? juce::String(val, 1) : juce::String((int)std::round(val)); }
            else if (suffix == ":1")  { valStr = juce::String(val, 1); }
            else                      { valStr = juce::String(val, 2); }

            if (isDown)
            {
                // Semi-transparent fill behind value to mask the inner glow
                g.setColour(juce::Colour(0xcc06070e));
                g.fillEllipse(cx - innerR + 1.f, cy - innerR + 1.f,
                              (innerR - 1.f) * 2.f, (innerR - 1.f) * 2.f);
                g.setColour(accent.brighter(0.2f));
            }
            else  // hover — no backdrop, just dim text so the pointer stays visible
            {
                g.setColour(accent.withAlpha(0.52f));
            }
            g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(7.5f)));
            g.drawText(valStr,
                       static_cast<int>(cx - innerR), static_cast<int>(cy - innerR),
                       static_cast<int>(innerR * 2.f), static_cast<int>(innerR * 2.f),
                       juce::Justification::centred);
        }
    }

    //==========================================================================
    // Combo box — premium selector field style
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                      juce::ComboBox& box) override
    {
        const bool focused = box.hasKeyboardFocus(true) || isButtonDown;

        // Background gradient
        juce::ColourGradient bg(juce::Colour(0xff0f1025), 0.f, 0.f,
                                 juce::Colour(0xff090a18), 0.f, (float)height, false);
        g.setGradientFill(bg);
        g.fillRect(0.f, 0.f, (float)width, (float)height);

        // Full dim border
        g.setColour(juce::Colour(0xff1c1d32));
        g.drawRect(0.5f, 0.5f, (float)width - 1.f, (float)height - 1.f, 0.8f);

        // Bottom accent line — the "active field" indicator
        g.setColour(MantisColors::Accent.withAlpha(focused ? 0.70f : 0.22f));
        g.fillRect(0.f, (float)height - 1.5f, (float)width, 1.5f);

        // Chevron — right side, smaller and cleaner
        const float aw = 4.f;
        const float ax = (float)width - 10.f;
        const float ay = (float)height * 0.5f;
        juce::Path ch;
        ch.startNewSubPath(ax - aw * 0.5f, ay - aw * 0.25f);
        ch.lineTo(ax,                      ay + aw * 0.30f);
        ch.lineTo(ax + aw * 0.5f,          ay - aw * 0.25f);
        g.setColour(MantisColors::Accent.withAlpha(focused ? 0.80f : 0.35f));
        g.strokePath(ch, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions().withName("Segoe UI").withHeight(11.5f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(9, 0, box.getWidth() - 22, box.getHeight());
        label.setFont(getComboBoxFont(box));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffccccee));
    }

    //==========================================================================
    // Buttons — rounded, gradient fill + indicator dot when ON (Mantis Vex brand)
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
        const bool on = button.getToggleState();

        // Fill
        if (on)
        {
            juce::ColourGradient grad(backgroundColour.withAlpha(0.22f),
                                     bounds.getX(), bounds.getY(),
                                     backgroundColour.withAlpha(0.07f),
                                     bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill(grad);
        }
        else
        {
            g.setColour(highlighted ? juce::Colour(0xff121324) : juce::Colour(0xff0d0e1c));
            if (down) g.setColour(juce::Colour(0xff141530));
        }
        g.fillRoundedRectangle(bounds, 4.f);

        // Border
        g.setColour(on ? backgroundColour.withAlpha(0.80f) : juce::Colour(0xff2a2b45));
        g.drawRoundedRectangle(bounds, 4.f, on ? 1.2f : 0.6f);

        // Indicator dot — top-right corner, Mantis Vex brand signature
        const float dotR = 2.8f;
        const float dotX = bounds.getRight() - dotR - 4.f;
        const float dotY = bounds.getY() + dotR + 4.f;
        if (on)
        {
            // Glow halo behind dot
            g.setColour(backgroundColour.withAlpha(0.22f));
            g.fillEllipse(dotX - dotR * 1.8f, dotY - dotR * 1.8f,
                          dotR * 3.6f, dotR * 3.6f);
        }
        g.setColour(on ? backgroundColour : juce::Colour(0xff2a2b42));
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.f, dotR * 2.f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool highlighted, bool /*down*/) override
    {
        const bool on = button.getToggleState();
        const auto accent = button.findColour(juce::TextButton::buttonOnColourId);

        g.setFont(juce::Font(juce::FontOptions().withName("Segoe UI").withHeight(10.f)));

        juce::Colour col;
        if (on)               col = accent.brighter(0.15f);
        else if (highlighted) col = juce::Colour(0xff8888aa);
        else                  col = juce::Colour(0xff686888);

        // Leave room for the indicator dot on the right
        auto area = button.getLocalBounds().reduced(2, 0).withTrimmedRight(12);
        g.setColour(col);
        g.drawText(button.getButtonText(), area, juce::Justification::centred, false);
    }

    //==========================================================================
    // Popup menu â€" dark, sharp, monospace
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.setColour(findColour(juce::PopupMenu::backgroundColourId));
        g.fillRect(0, 0, width, height);
        g.setColour(juce::Colour(0xff0e0f1e));
        g.drawRect(0.5f, 0.5f, (float)width - 1.f, (float)height - 1.f, 1.f);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override
    {
        juce::ignoreUnused(shortcutText, icon);

        if (isSeparator)
        {
            g.setColour(juce::Colour(0xff0d0e1c));
            g.drawHorizontalLine(area.getCentreY(),
                                 (float)area.getX() + 8.f,
                                 (float)area.getRight() - 8.f);
            return;
        }

        auto col = isActive ? juce::Colour(0xff8888aa) : juce::Colour(0xff303048);
        if (textColour) col = *textColour;

        if (isHighlighted && isActive)
        {
            g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRect(area.toFloat().reduced(2.f, 1.f));
            // Left stripe for highlighted item
            g.setColour(MantisColors::Accent);
            g.fillRect((float)area.getX(), (float)area.getY() + 1.f,
                       2.f, (float)area.getHeight() - 2.f);
            col = findColour(juce::PopupMenu::highlightedTextColourId);
        }

        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions().withName("Consolas").withHeight(11.5f)));

        auto textArea = area.withTrimmedLeft(isTicked ? 22 : 10)
                            .withTrimmedRight(hasSubMenu ? 16 : 6);
        g.drawText(text, textArea, juce::Justification::centredLeft, true);

        if (isTicked)
        {
            g.setColour(MantisColors::Accent);
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
            g.drawText(">", area.withWidth(18), juce::Justification::centred, false);
        }

        if (hasSubMenu)
        {
            const float aw = 4.f;
            const float ax = (float)area.getRight() - 10.f;
            const float ay = (float)area.getCentreY();
            juce::Path arrow;
            arrow.startNewSubPath(ax - aw * 0.3f, ay - aw * 0.5f);
            arrow.lineTo(ax + aw * 0.3f, ay);
            arrow.lineTo(ax - aw * 0.3f, ay + aw * 0.5f);
            g.strokePath(arrow, juce::PathStrokeType(1.2f));
        }
    }

    int getMenuWindowFlags() override { return 0; }

    juce::Font getOrbitronFont(float height) const
    {
        return juce::Font(juce::FontOptions(orbitronTypeface).withHeight(height));
    }

private:
    juce::Typeface::Ptr orbitronTypeface;
};



