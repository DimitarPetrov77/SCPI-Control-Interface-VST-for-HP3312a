#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * VimLookAndFeel - Custom JUCE LookAndFeel for vim console aesthetic
 * 
 * Colors:
 * - Background: Black (#000000)
 * - Primary text: Green (#4ADE80)
 * - Headers: Yellow (#FACC15)
 * - Accents: Cyan (#22D3EE)
 * - Errors: Red (#EF4444)
 * - Success: Green (#22C55E)
 * - Warning: Orange (#F97316)
 */
class VimLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Color palette
    static inline const juce::Colour bgColor        = juce::Colour(0xFF000000);  // Black
    static inline const juce::Colour bgDark         = juce::Colour(0xFF0A0A0A);  // Slightly lighter for contrast
    static inline const juce::Colour bgPanel        = juce::Colour(0xFF111111);  // Panel background
    static inline const juce::Colour borderColor    = juce::Colour(0xFF4ADE80);  // Green border
    static inline const juce::Colour textPrimary    = juce::Colour(0xFFE5E5E5);  // White-ish text
    static inline const juce::Colour textGreen      = juce::Colour(0xFF4ADE80);  // Green text
    static inline const juce::Colour textCyan       = juce::Colour(0xFF22D3EE);  // Cyan for labels
    static inline const juce::Colour textYellow     = juce::Colour(0xFFFACC15);  // Yellow for headers
    static inline const juce::Colour textGray       = juce::Colour(0xFF6B7280);  // Gray for secondary text
    static inline const juce::Colour textRed        = juce::Colour(0xFFEF4444);  // Red for errors
    static inline const juce::Colour textOrange     = juce::Colour(0xFFF97316);  // Orange for warnings
    static inline const juce::Colour sliderThumb    = juce::Colour(0xFF4ADE80);  // Green thumb
    static inline const juce::Colour sliderTrack    = juce::Colour(0xFF374151);  // Dark gray track
    static inline const juce::Colour buttonBg       = juce::Colour(0xFF1F2937);  // Button background
    static inline const juce::Colour buttonHover    = juce::Colour(0xFF166534);  // Green hover
    static inline const juce::Colour toggleOn       = juce::Colour(0xFF166534);  // Toggle on (darker green)
    static inline const juce::Colour toggleOff      = juce::Colour(0xFF374151);  // Toggle off (gray)

    VimLookAndFeel()
    {
        // Set default colors
        setColour(juce::ResizableWindow::backgroundColourId, bgColor);
        setColour(juce::DocumentWindow::backgroundColourId, bgColor);
        
        // Labels
        setColour(juce::Label::textColourId, textPrimary);
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        
        // TextEditor
        setColour(juce::TextEditor::backgroundColourId, bgPanel);
        setColour(juce::TextEditor::textColourId, textGreen);
        setColour(juce::TextEditor::outlineColourId, borderColor.withAlpha(0.5f));
        setColour(juce::TextEditor::focusedOutlineColourId, textYellow);
        setColour(juce::TextEditor::highlightColourId, textGreen.withAlpha(0.3f));
        setColour(juce::CaretComponent::caretColourId, textGreen);
        
        // TextButton
        setColour(juce::TextButton::buttonColourId, buttonBg);
        setColour(juce::TextButton::buttonOnColourId, toggleOn);
        setColour(juce::TextButton::textColourOffId, textGreen);
        setColour(juce::TextButton::textColourOnId, textGreen);
        
        // ToggleButton
        setColour(juce::ToggleButton::textColourId, textCyan);
        setColour(juce::ToggleButton::tickColourId, textGreen);
        setColour(juce::ToggleButton::tickDisabledColourId, textGray);
        
        // ComboBox
        setColour(juce::ComboBox::backgroundColourId, bgPanel);
        setColour(juce::ComboBox::textColourId, textGreen);
        setColour(juce::ComboBox::outlineColourId, borderColor.withAlpha(0.5f));
        setColour(juce::ComboBox::arrowColourId, textGreen);
        setColour(juce::ComboBox::focusedOutlineColourId, textYellow);
        
        // PopupMenu
        setColour(juce::PopupMenu::backgroundColourId, bgPanel);
        setColour(juce::PopupMenu::textColourId, textGreen);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, toggleOn);
        setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);
        
        // Slider
        setColour(juce::Slider::backgroundColourId, sliderTrack);
        setColour(juce::Slider::thumbColourId, sliderThumb);
        setColour(juce::Slider::trackColourId, sliderTrack);
        setColour(juce::Slider::textBoxTextColourId, textGreen);
        setColour(juce::Slider::textBoxBackgroundColourId, bgPanel);
        setColour(juce::Slider::textBoxOutlineColourId, borderColor.withAlpha(0.5f));
        setColour(juce::Slider::textBoxHighlightColourId, textGreen.withAlpha(0.3f));
        
        // GroupComponent
        setColour(juce::GroupComponent::outlineColourId, borderColor);
        setColour(juce::GroupComponent::textColourId, textYellow);
        
        // ScrollBar
        setColour(juce::ScrollBar::thumbColourId, textGreen.withAlpha(0.5f));
        setColour(juce::ScrollBar::trackColourId, bgPanel);
        
        // Set monospace font as default
        setDefaultSansSerifTypefaceName(juce::Font::getDefaultMonospacedFontName());
    }

    //==============================================================================
    // Helper to create monospace font
    //==============================================================================
    static juce::Font getMonoFont(float height)
    {
        juce::Font f(height);
        f.setTypefaceName(juce::Font::getDefaultMonospacedFontName());
        return f;
    }

    //==============================================================================
    // Custom drawing methods
    //==============================================================================

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, 
                              const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        
        juce::Colour baseColour = backgroundColour;
        
        if (button.getToggleState())
            baseColour = toggleOn;
        else if (isButtonDown)
            baseColour = buttonHover;
        else if (isMouseOverButton)
            baseColour = buttonBg.brighter(0.2f);
        
        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 3.0f);
        
        g.setColour(borderColor.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        (void)minSliderPos;
        (void)maxSliderPos;
        (void)style;
        
        auto trackWidth = juce::jmin(4.0f, (float)height * 0.25f);
        
        juce::Point<float> startPoint((float)x, (float)y + (float)height * 0.5f);
        juce::Point<float> endPoint((float)(x + width), startPoint.y);
        
        juce::Path backgroundTrack;
        backgroundTrack.startNewSubPath(startPoint);
        backgroundTrack.lineTo(endPoint);
        g.setColour(sliderTrack);
        g.strokePath(backgroundTrack, {trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded});
        
        juce::Path valueTrack;
        juce::Point<float> thumbPoint(sliderPos, startPoint.y);
        valueTrack.startNewSubPath(startPoint);
        valueTrack.lineTo(thumbPoint);
        g.setColour(slider.isEnabled() ? sliderThumb : textGray);
        g.strokePath(valueTrack, {trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded});
        
        auto thumbWidth = 12.0f;
        g.setColour(slider.isEnabled() ? sliderThumb : textGray);
        g.fillRoundedRectangle(juce::Rectangle<float>(thumbWidth, thumbWidth).withCentre(thumbPoint), 2.0f);
        
        g.setColour(borderColor);
        g.drawRoundedRectangle(juce::Rectangle<float>(thumbWidth, thumbWidth).withCentre(thumbPoint), 2.0f, 1.0f);
    }

    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
                                   const juce::String& text, const juce::Justification& position,
                                   juce::GroupComponent& group) override
    {
        (void)position;
        
        const float textH = 15.0f;
        const float indent = 3.0f;
        const float textEdgeGap = 4.0f;
        auto cs = 5.0f;

        juce::Font f = getMonoFont(textH);

        juce::Path p;
        auto x = indent;
        auto y = f.getAscent() - 3.0f;
        auto w = juce::jmax(0.0f, (float)width - x * 2.0f);
        auto h = juce::jmax(0.0f, (float)height - y - indent);
        cs = juce::jmin(cs, w * 0.5f, h * 0.5f);
        auto cs2 = 2.0f * cs;

        // Create vim-style header text: [ SECTION_NAME ]
        juce::String vimText = "[ " + text.toUpperCase() + " ]";
        auto textW = f.getStringWidth(vimText) + textEdgeGap * 2;
        auto textX = cs + textEdgeGap;

        p.startNewSubPath(x + textX + textW, y);
        p.lineTo(x + w - cs, y);

        p.addArc(x + w - cs2, y, cs2, cs2, 0, juce::MathConstants<float>::halfPi);
        p.lineTo(x + w, y + h - cs);

        p.addArc(x + w - cs2, y + h - cs2, cs2, cs2, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi);
        p.lineTo(x + cs, y + h);

        p.addArc(x, y + h - cs2, cs2, cs2, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f);
        p.lineTo(x, y + cs);

        p.addArc(x, y, cs2, cs2, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi);
        p.lineTo(x + textX, y);

        g.setColour(group.findColour(juce::GroupComponent::outlineColourId));
        g.strokePath(p, juce::PathStrokeType(1.0f));

        g.setColour(group.findColour(juce::GroupComponent::textColourId));
        g.setFont(f);
        g.drawText(vimText, juce::roundToInt(x + textX), 0, juce::roundToInt(textW), juce::roundToInt(textH),
                   juce::Justification::centred, true);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto fontSize = juce::jmin(15.0f, (float)button.getHeight() * 0.75f);
        auto tickWidth = fontSize * 1.1f;

        drawTickBox(g, button, 4.0f, ((float)button.getHeight() - tickWidth) * 0.5f,
                    tickWidth, tickWidth,
                    button.getToggleState(),
                    button.isEnabled(),
                    shouldDrawButtonAsHighlighted,
                    shouldDrawButtonAsDown);

        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(getMonoFont(fontSize));

        if (!button.isEnabled())
            g.setOpacity(0.5f);

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
                                                .withTrimmedRight(2),
                         juce::Justification::centredLeft, 10);
    }

    void drawTickBox(juce::Graphics& g, juce::Component& component,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled,
                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        (void)component;
        (void)shouldDrawButtonAsHighlighted;
        (void)shouldDrawButtonAsDown;
        
        juce::Rectangle<float> tickBounds(x, y, w, h);

        g.setColour(ticked ? toggleOn : toggleOff);
        g.fillRoundedRectangle(tickBounds, 3.0f);
        
        g.setColour(isEnabled ? borderColor : textGray);
        g.drawRoundedRectangle(tickBounds, 3.0f, 1.0f);

        if (ticked)
        {
            g.setColour(isEnabled ? textGreen : textGray);
            auto tick = getTickShape(0.75f);
            g.fillPath(tick, tick.getTransformToScaleToFit(tickBounds.reduced(4, 5).toFloat(), true));
        }
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        auto cornerSize = 3.0f;
        juce::Rectangle<int> boxBounds(0, 0, width, height);

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

        g.setColour(box.findColour(box.hasKeyboardFocus(true) ? juce::ComboBox::focusedOutlineColourId
                                                              : juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);

        juce::Rectangle<int> arrowZone(buttonX, buttonY, buttonW, buttonH);
        juce::Path path;
        path.startNewSubPath((float)arrowZone.getX() + 3.0f, (float)arrowZone.getCentreY() - 2.0f);
        path.lineTo((float)arrowZone.getCentreX(), (float)arrowZone.getCentreY() + 3.0f);
        path.lineTo((float)arrowZone.getRight() - 3.0f, (float)arrowZone.getCentreY() - 2.0f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha((box.isEnabled() ? 0.9f : 0.2f)));
        g.strokePath(path, juce::PathStrokeType(2.0f));
        
        (void)isButtonDown;
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        return getMonoFont(label.getFont().getHeight());
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return getMonoFont(juce::jmin(15.0f, (float)buttonHeight * 0.6f));
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override
    {
        return getMonoFont(juce::jmin(15.0f, (float)box.getHeight() * 0.85f));
    }

    juce::Font getPopupMenuFont() override
    {
        return getMonoFont(14.0f);
    }

    juce::Font getSliderPopupFont(juce::Slider&) override
    {
        return getMonoFont(14.0f);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VimLookAndFeel)
};
