#include "PluginEditor.h"

namespace col
{
    const juce::Colour bg     { 0xff0e0f14 };
    const juce::Colour panel  { 0xff181a22 };
    const juce::Colour accent { 0xff7cf0c4 };
    const juce::Colour warn   { 0xffff7a59 };
    const juce::Colour text   { 0xffe8eaf0 };
    const juce::Colour dim    { 0xff7d8394 };
}

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

FlyVozEQEditor::FlyVozEQEditor (FlyVozEQProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    lnf.setColour (juce::TextButton::buttonColourId,   col::panel);
    lnf.setColour (juce::TextButton::buttonOnColourId, col::accent);
    lnf.setColour (juce::TextButton::textColourOffId,  col::text);
    lnf.setColour (juce::TextButton::textColourOnId,   col::bg);
    lnf.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    lnf.setColour (juce::Slider::rotarySliderFillColourId,    col::accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, col::panel.brighter (0.15f));
    lnf.setColour (juce::Slider::thumbColourId,               col::text);
    lnf.setColour (juce::Slider::textBoxTextColourId,         col::text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    lnf.setColour (juce::ToggleButton::textColourId,          col::text);
    lnf.setColour (juce::ToggleButton::tickColourId,          col::accent);
    lnf.setColour (juce::Label::textColourId,                 col::dim);
    lnf.setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
    lnf.setColour (juce::TextEditor::focusedOutlineColourId,  col::accent);
    lnf.setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setLookAndFeel (&lnf);

    analyzeButton.setButtonText ("ANALIZAR VOZ");
    analyzeButton.onClick = [this]
    {
        if (proc.isAnalyzing()) proc.cancelAnalysis();
        else                    proc.startAnalysis();
    };
    addAndMakeVisible (analyzeButton);

    clearButton.setButtonText (u8 ("Borrar análisis"));
    clearButton.onClick = [this] { proc.clearAnalysis(); };
    addAndMakeVisible (clearButton);

    const auto names = FlyVozEQProcessor::modeNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = modeButtons.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i]
        {
            if (auto* param = proc.apvts.getParameter ("modo"))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (b);
    }

    for (auto* s : { &intensity, &output })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
    }
    intensityAtt = std::make_unique<SA> (proc.apvts, "intensidad", intensity);
    outputAtt    = std::make_unique<SA> (proc.apvts, "salida", output);
    intensity.setTextValueSuffix (" %");
    output.setTextValueSuffix (" dB");

    intensityLabel.setText ("INTENSIDAD", juce::dontSendNotification);
    outputLabel.setText ("SALIDA", juce::dontSendNotification);
    for (auto* l : { &intensityLabel, &outputLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }

    hpfButton.setButtonText ("Quitar graves (80 Hz)");
    hpfAtt = std::make_unique<BA> (proc.apvts, "hpf", hpfButton);
    addAndMakeVisible (hpfButton);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (statusLabel);

    setSize (620, 470);
    timerCallback();
    startTimerHz (30);
}

FlyVozEQEditor::~FlyVozEQEditor() { setLookAndFeel (nullptr); }

void FlyVozEQEditor::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("modo")->load();
    for (int i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setToggleState (i == mode, juce::dontSendNotification);

    if (proc.isAnalyzing())
    {
        analyzeButton.setButtonText ("CANCELAR");
        const bool voice = proc.getInputLevelDb() > -45.0f;
        statusLabel.setText (u8 (voice ? "Escuchando tu voz… " : "Hablá o cantá en el micrófono… ")
                                 + juce::String (juce::roundToInt (proc.getProgress() * 100.0f)) + " %",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, voice ? col::accent : col::warn);
    }
    else
    {
        analyzeButton.setButtonText ("ANALIZAR VOZ");
        statusLabel.setText (proc.hasAnalysisData() ? u8 ("Voz analizada: EQ automático activo")
                                                    : u8 ("Tocá ANALIZAR VOZ y cantá 8 segundos"),
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, proc.hasAnalysisData() ? col::accent : col::dim);
    }
    clearButton.setEnabled (proc.hasAnalysisData());
    repaint (curveArea.toNearestInt().expanded (4));
}

void FlyVozEQEditor::drawCurve (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);

    const double fs = 48000.0;
    const float maxDb = 15.0f;
    auto xFor = [&] (double f) { return r.getX() + (float) (std::log (f / 20.0) / std::log (20000.0 / 20.0)) * r.getWidth(); };
    auto yFor = [&] (double db) { return r.getCentreY() - (float) (db / maxDb) * (r.getHeight() * 0.45f); };

    // Grilla
    g.setFont (juce::FontOptions (10.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (col::text.withAlpha (0.06f));
        g.drawVerticalLine ((int) xFor (f), r.getY() + 4, r.getBottom() - 4);
        g.setColour (col::dim.withAlpha (0.7f));
        g.drawText (f >= 1000 ? juce::String (f / 1000.0) + "k" : juce::String ((int) f),
                    (int) xFor (f) + 3, (int) r.getBottom() - 16, 40, 12, juce::Justification::left);
    }
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        g.setColour (col::text.withAlpha (db == 0.0 ? 0.15f : 0.05f));
        g.drawHorizontalLine ((int) yFor (db), r.getX() + 4, r.getRight() - 4);
    }

    // Curva real de la EQ (suma de las 8 campanas + filtro de graves)
    std::array<Biquad, FlyVozEQProcessor::numBands> bq;
    for (int b = 0; b < FlyVozEQProcessor::numBands; ++b)
        bq[(size_t) b].setPeak (fs, FlyVozEQProcessor::bandFreqs[b], FlyVozEQProcessor::bandQ, proc.getDisplayGain (b));
    Biquad hp; hp.setHighpass (fs, 80.0, 0.707);
    const bool hpfOn = proc.apvts.getRawParameterValue ("hpf")->load() > 0.5f;

    juce::Path curve, fill;
    for (int px = 0; px <= (int) r.getWidth(); px += 2)
    {
        const double f = 20.0 * std::pow (1000.0, px / (double) r.getWidth());
        double db = 0.0;
        for (auto& b : bq) db += b.magnitudeDb (f, fs);
        if (hpfOn) db += hp.magnitudeDb (f, fs);
        const float x = r.getX() + (float) px;
        const float y = juce::jlimit (r.getY() + 2, r.getBottom() - 2, yFor (db));
        if (px == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, yFor (0)); }
        else         curve.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (r.getRight(), yFor (0));
    fill.closeSubPath();

    g.setColour (col::accent.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (col::accent);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));

    // Puntos de cada banda
    for (int b = 0; b < FlyVozEQProcessor::numBands; ++b)
    {
        const float x = xFor (FlyVozEQProcessor::bandFreqs[b]);
        const float y = yFor (proc.getDisplayGain (b));
        g.setColour (col::bg);
        g.fillEllipse (x - 4, y - 4, 8, 8);
        g.setColour (col::accent);
        g.drawEllipse (x - 4, y - 4, 8, 8, 1.5f);
    }
}

void FlyVozEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("FLY", 20, 14, 60, 28, juce::Justification::left);
    g.setColour (col::accent);
    g.drawText ("VozEQ", 68, 14, 120, 28, juce::Justification::left);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (u8 ("EQ automático para voz"), 170, 20, 200, 18, juce::Justification::left);

    drawCurve (g, curveArea);
}

void FlyVozEQEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    r.removeFromTop (34);

    auto top = r.removeFromTop (36);
    analyzeButton.setBounds (top.removeFromLeft (160));
    top.removeFromLeft (10);
    clearButton.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (14);
    statusLabel.setBounds (top);

    r.removeFromTop (12);
    auto bottom = r.removeFromBottom (170);
    curveArea = r.toFloat();

    bottom.removeFromTop (12);
    auto modes = bottom.removeFromTop (34);
    const int bw = (modes.getWidth() - 4 * 8) / 5;
    for (auto* b : modeButtons)
    {
        b->setBounds (modes.removeFromLeft (bw));
        modes.removeFromLeft (8);
    }

    bottom.removeFromTop (6);
    auto knobs = bottom;
    auto k1 = knobs.removeFromLeft (110);
    intensityLabel.setBounds (k1.removeFromTop (14));
    intensity.setBounds (k1);
    auto k2 = knobs.removeFromLeft (110);
    outputLabel.setBounds (k2.removeFromTop (14));
    output.setBounds (k2);
    hpfButton.setBounds (knobs.withTrimmedLeft (20).withSizeKeepingCentre (knobs.getWidth() - 20, 26));
}
