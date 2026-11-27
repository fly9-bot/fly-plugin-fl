#include "PluginEditor.h"

namespace col
{
    const juce::Colour bg     { 0xff0d0e13 };
    const juce::Colour panel  { 0xff171922 };
    const juce::Colour accent { 0xffb48cff };
    const juce::Colour accent2{ 0xff7cf0c4 };
    const juce::Colour text   { 0xffe8eaf0 };
    const juce::Colour dim    { 0xff7d8394 };
}

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

FlyMixChatEditor::FlyMixChatEditor (FlyMixChatProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    lnf.setColour (juce::TextButton::buttonColourId,   col::panel);
    lnf.setColour (juce::TextButton::textColourOffId,  col::text);
    lnf.setColour (juce::TextEditor::backgroundColourId, col::panel);
    lnf.setColour (juce::TextEditor::textColourId,     col::text);
    lnf.setColour (juce::TextEditor::outlineColourId,  juce::Colours::transparentBlack);
    lnf.setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    lnf.setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.3f));
    lnf.setColour (juce::CaretComponent::caretColourId, col::accent);
    lnf.setColour (juce::Slider::rotarySliderFillColourId,    col::accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, col::panel.brighter (0.15f));
    lnf.setColour (juce::Slider::thumbColourId,               col::text);
    lnf.setColour (juce::Slider::textBoxTextColourId,         col::text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    lnf.setColour (juce::Label::textColourId,                 col::dim);
    lnf.setColour (juce::ComboBox::backgroundColourId,        col::panel);
    lnf.setColour (juce::ComboBox::textColourId,              col::text);
    lnf.setColour (juce::ComboBox::outlineColourId,           juce::Colours::transparentBlack);
    lnf.setColour (juce::ComboBox::arrowColourId,             col::accent);
    lnf.setColour (juce::PopupMenu::backgroundColourId,       col::panel);
    lnf.setColour (juce::PopupMenu::textColourId,             col::text);
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
    lnf.setColour (juce::ScrollBar::thumbColourId, col::accent.withAlpha (0.6f));
    setLookAndFeel (&lnf);

    log.setMultiLine (true, true);
    log.setReadOnly (true);
    log.setCaretVisible (false);
    log.setScrollbarsShown (true);
    log.setFont (juce::FontOptions (14.0f));
    log.setIndents (12, 10);
    addAndMakeVisible (log);

    input.setTextToShowWhenEmpty (u8 ("Escribí: mix type drake, type cro con más reverb…"), col::dim);
    input.setFont (juce::FontOptions (16.0f));
    input.setIndents (12, 10);
    input.onReturnKey = [this] { send (input.getText()); };
    addAndMakeVisible (input);

    sendButton.setColour (juce::TextButton::buttonColourId, col::accent);
    sendButton.setColour (juce::TextButton::textColourOffId, col::bg);
    sendButton.onClick = [this] { send (input.getText()); };
    addAndMakeVisible (sendButton);

    for (auto& name : StyleEngine::quickStyles())
    {
        auto* b = chips.add (new juce::TextButton (name));
        b->onClick = [this, name] { send ("type " + name); };
        addAndMakeVisible (b);
    }

    const char* knobIds[] = { "corte", "graves", "barro", "presencia", "aire", "deesser", "compresion",
                              "saturacion", "ancho", "delay", "reverb", "sala", "salida" };
    const juce::String knobNames[] = { "CORTE", "GRAVES", "BARRO", "PRESENCIA", "AIRE", "DE-ESSER", u8 ("COMPRESIÓN"),
                                       u8 ("SATURACIÓN"), "ANCHO", "DELAY", "REVERB", "SALA", "SALIDA" };
    for (int i = 0; i < 13; ++i)
    {
        auto* k = knobs.add (new Knob());
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->label.setText (knobNames[i], juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, knobIds[i], k->slider);
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
    }
    // Las de efecto en otro color
    for (int i : { 9, 10, 11 })
        knobs[i]->slider.setColour (juce::Slider::rotarySliderFillColourId, col::accent2);

    delayNote.addItemList ({ "1/4", "1/8 .", "1/8", "1/16" }, 1);
    delayNoteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "delaynota", delayNote);
    delayNoteLabel.setText ("NOTA DELAY", juce::dontSendNotification);
    delayNoteLabel.setJustificationType (juce::Justification::centred);
    delayNoteLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (delayNote);
    addAndMakeVisible (delayNoteLabel);

    setSize (780, 600);
    refreshLog();
    startTimerHz (20);
}

FlyMixChatEditor::~FlyMixChatEditor() { setLookAndFeel (nullptr); }

void FlyMixChatEditor::send (const juce::String& text)
{
    if (text.trim().isEmpty()) return;
    proc.sendChat (text);
    input.clear();
    refreshLog();
}

void FlyMixChatEditor::refreshLog()
{
    log.setText (proc.chatLog.joinIntoString ("\n\n"), false);
    log.moveCaretToEnd();
    lastLogSize = proc.chatLog.size();
}

void FlyMixChatEditor::timerCallback()
{
    if (proc.chatLog.size() != lastLogSize) refreshLog();
    repaint (meterArea);
}

void FlyMixChatEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.setColour (col::text);
    g.drawText ("FLY", 20, 14, 60, 28, juce::Justification::left);
    g.setColour (col::accent);
    g.drawText ("MixChat", 68, 14, 140, 28, juce::Justification::left);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (u8 ("Pedí el mix con palabras"), 190, 20, 220, 18, juce::Justification::left);

    // Medidor de compresión
    const float gr = juce::jlimit (0.0f, 20.0f, proc.getGainReductionDb());
    auto m = meterArea.toFloat();
    g.setColour (col::dim);
    g.drawText (u8 ("COMPRESIÓN"), m.removeFromLeft (90).toNearestInt(), juce::Justification::centredRight);
    m.removeFromLeft (8);
    auto bar = m.withSizeKeepingCentre (m.getWidth(), 8.0f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (col::accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * gr / 20.0f), 4.0f);

    g.setColour (col::panel);
    g.fillRoundedRectangle (knobArea.toFloat().expanded (6), 10.0f);
}

void FlyMixChatEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto header = r.removeFromTop (34);
    meterArea = header.removeFromRight (260).withTrimmedTop (6).withHeight (20);

    r.removeFromTop (8);
    log.setBounds (r.removeFromTop (150));
    r.removeFromTop (10);
    auto in = r.removeFromTop (42);
    sendButton.setBounds (in.removeFromRight (120));
    in.removeFromRight (8);
    input.setBounds (in);

    r.removeFromTop (10);
    auto chipRow = r.removeFromTop (30);
    const int cw = (chipRow.getWidth() - 7 * 6) / 8;
    for (auto* c : chips) { c->setBounds (chipRow.removeFromLeft (cw)); chipRow.removeFromLeft (6); }

    r.removeFromTop (18);
    knobArea = r.reduced (6);
    auto grid = knobArea;
    const int cols = 7;
    const int cellW = grid.getWidth() / cols, cellH = grid.getHeight() / 2;
    for (int i = 0; i < 14; ++i)
    {
        auto cell = juce::Rectangle<int> (grid.getX() + (i % cols) * cellW, grid.getY() + (i / cols) * cellH, cellW, cellH).reduced (4);
        auto lab = cell.removeFromTop (16);
        if (i < 13)
        {
            knobs[i]->label.setBounds (lab);
            knobs[i]->slider.setBounds (cell);
        }
        else
        {
            delayNoteLabel.setBounds (lab);
            delayNote.setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 10, 28));
        }
    }
}
