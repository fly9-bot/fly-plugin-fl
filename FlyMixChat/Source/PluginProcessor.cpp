#include "PluginProcessor.h"
#include "PluginEditor.h"

FlyMixChatProcessor::FlyMixChatProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    chatLog.add (juce::String::fromUTF8 ("FLY: Escribime qué mix querés. Ej: \"mix type drake\", \"type cro con más reverb\", \"más oscuro\", \"sin delay\"."));
}

juce::AudioProcessorValueTreeState::ParameterLayout FlyMixChatProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto add = [&l] (const char* id, const char* name, float lo, float hi, float step, float def, const char* unit)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, String::fromUTF8 (name),
                                                      NormalisableRange<float> (lo, hi, step), def,
                                                      AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("corte",      "Corte de graves", 20, 200, 1, 90, "Hz");
    add ("graves",     "Graves",     -6, 6, 0.1f, 0, "dB");
    add ("barro",      "Barro",      -8, 2, 0.1f, -2, "dB");
    add ("presencia",  "Presencia",  -6, 8, 0.1f, 1.5f, "dB");
    add ("aire",       "Aire",       -8, 8, 0.1f, 2, "dB");
    add ("compresion", "Compresión", 0, 100, 1, 50, "%");
    add ("saturacion", "Saturación", 0, 100, 1, 10, "%");
    add ("deesser",    "De-esser",   0, 100, 1, 50, "%");
    add ("delay",      "Delay",      0, 100, 1, 10, "%");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "delaynota", 1 }, "Nota delay",
                                                   StringArray { "1/4", "1/8 .", "1/8", "1/16" }, 0));
    add ("reverb",     "Reverb",     0, 100, 1, 15, "%");
    add ("sala",       "Sala",       0, 100, 1, 60, "%");
    add ("ancho",      "Ancho",      0, 100, 1, 20, "%");
    add ("salida",     "Salida",     -12, 12, 0.1f, 0, "dB");
    return l;
}

bool FlyMixChatProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FlyMixChatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    for (auto& f : filt)
    {
        f.hpf.reset(); f.low.reset(); f.mud.reset(); f.pres.reset(); f.air.reset(); f.deessHp.reset();
        f.deessHp.setHighpass (fs, 5500.0, 0.707);
    }
    lastFilterValues.fill (-999.0f);
    compEnv = deessEnv = 0.0f;

    widthBuf.assign ((size_t) (fs * 0.05) + 2, 0.0f);
    widthPos = 0;
    for (auto& d : delayBuf) d.assign ((size_t) (fs * 2.5) + 2, 0.0f);
    delayPos = 0;
    delayLp.fill (0.0f);
    delayTimeSamples.reset (sampleRate, 0.3);
    delayTimeSamples.setCurrentAndTargetValue ((float) (fs * 0.5));

    reverb.reset();
    reverb.setSampleRate (sampleRate);
    wetBuf.setSize (2, samplesPerBlock, false, false, true);
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("salida")));
}

MixSettings FlyMixChatProcessor::readSettings() const
{
    MixSettings m;
    for (int i = 0; i < MixSettings::count; ++i)
        m[i] = p (MixSettings::ids[i]);
    return m;
}

juce::String FlyMixChatProcessor::sendChat (const juce::String& message)
{
    auto result = StyleEngine::interpret (message, readSettings());
    if (result.understood)
    {
        for (int i = 0; i < MixSettings::count; ++i)
            if (auto* prm = apvts.getParameter (MixSettings::ids[i]))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (result.settings[i]));
                prm->endChangeGesture();
            }
    }
    chatLog.add (juce::String::fromUTF8 ("Vos: ") + message.trim());
    chatLog.add ("FLY: " + result.reply);
    while (chatLog.size() > 40) chatLog.remove (0);
    return result.reply;
}

void FlyMixChatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = juce::jmin (2, getTotalNumOutputChannels());
    if (numIn == 1 && numOut == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n); // mono -> estéreo
    for (int ch = numOut; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (wetBuf.getNumSamples() < n) return; // no debería pasar

    // ---- Filtros: solo se recalculan si cambió algo ----
    const float vals[5] = { p ("corte"), p ("graves"), p ("barro"), p ("presencia"), p ("aire") };
    if (vals[0] != lastFilterValues[0] || vals[1] != lastFilterValues[1] || vals[2] != lastFilterValues[2]
        || vals[3] != lastFilterValues[3] || vals[4] != lastFilterValues[4])
    {
        for (auto& f : filt)
        {
            f.hpf.setHighpass (fs, vals[0], 0.707);
            f.low.setShelf (fs, 140.0, vals[1], false);
            f.mud.setPeak (fs, 320.0, 1.0, vals[2]);
            f.pres.setPeak (fs, 3200.0, 0.9, vals[3]);
            f.air.setShelf (fs, juce::jmin (11000.0, fs * 0.4), vals[4], true);
        }
        for (int i = 0; i < 5; ++i) lastFilterValues[(size_t) i] = vals[i];
    }

    // ---- Dinámica ----
    const float compAmt = p ("compresion") / 100.0f;
    const float thr = -6.0f - 24.0f * compAmt;
    const float ratio = 1.5f + 4.5f * compAmt;
    const float makeup = compAmt > 0.0f ? -thr * (1.0f - 1.0f / ratio) * 0.35f : 0.0f;
    const float atk = std::exp (-1.0f / (0.008f * (float) fs)), rel = std::exp (-1.0f / (0.12f * (float) fs));

    const float deAmt = p ("deesser") / 100.0f;
    const float deThr = -12.0f - 26.0f * deAmt;
    const float deAtk = std::exp (-1.0f / (0.001f * (float) fs)), deRel = std::exp (-1.0f / (0.06f * (float) fs));

    const float satAmt = p ("saturacion") / 100.0f;
    const float drive = 1.0f + 8.0f * satAmt;

    const float width = p ("ancho") / 100.0f;
    const int wA = (int) (0.011 * fs), wB = (int) (0.019 * fs);
    const int wSize = (int) widthBuf.size();

    // ---- Delay sincronizado al tempo del proyecto ----
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = juce::jlimit (40.0, 240.0, *b);
    static constexpr double beats[4] = { 1.0, 0.75, 0.5, 0.25 };
    const int nota = juce::jlimit (0, 3, (int) p ("delaynota"));
    delayTimeSamples.setTargetValue ((float) juce::jmin (fs * 2.4, fs * 60.0 / bpm * beats[nota]));
    const float delayMix = p ("delay") / 100.0f * 0.55f;
    const float fb = 0.25f + 0.2f * (p ("delay") / 100.0f);
    const float lpCoef = std::exp (-2.0f * juce::MathConstants<float>::pi * 4000.0f / (float) fs);
    const int dSize = (int) delayBuf[0].size();

    float* ch[2] = { buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr };
    float* wet[2] = { wetBuf.getWritePointer (0), wetBuf.getWritePointer (1) };
    float maxGr = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float x[2] {};
        for (int c = 0; c < numOut; ++c)
        {
            auto& f = filt[(size_t) c];
            float s = ch[c][i];
            s = f.hpf.process (s, c);
            s = f.low.process (s, c);
            s = f.mud.process (s, c);
            s = f.pres.process (s, c);
            s = f.air.process (s, c);
            x[c] = s;
        }

        // De-esser: separa las eses (agudos) y las baja solo cuando aparecen
        float hi[2] {}, hiPeak = 0.0f;
        for (int c = 0; c < numOut; ++c) { hi[c] = filt[(size_t) c].deessHp.process (x[c], c); hiPeak = juce::jmax (hiPeak, std::abs (hi[c])); }
        deessEnv = hiPeak > deessEnv ? deAtk * deessEnv + (1 - deAtk) * hiPeak : deRel * deessEnv + (1 - deRel) * hiPeak;
        const float deDb = juce::Decibels::gainToDecibels (deessEnv, -100.0f);
        const float deGain = (deAmt > 0.0f && deDb > deThr) ? juce::Decibels::decibelsToGain (-(deDb - deThr) * 0.75f) : 1.0f;
        for (int c = 0; c < numOut; ++c) x[c] -= hi[c] * (1.0f - deGain);

        // Compresor
        float peak = 0.0f;
        for (int c = 0; c < numOut; ++c) peak = juce::jmax (peak, std::abs (x[c]));
        compEnv = peak > compEnv ? atk * compEnv + (1 - atk) * peak : rel * compEnv + (1 - rel) * peak;
        const float envDb = juce::Decibels::gainToDecibels (compEnv, -100.0f);
        const float gr = (compAmt > 0.0f && envDb > thr) ? (envDb - thr) * (1.0f - 1.0f / ratio) : 0.0f;
        maxGr = juce::jmax (maxGr, gr);
        const float cg = juce::Decibels::decibelsToGain (makeup - gr);

        for (int c = 0; c < numOut; ++c)
        {
            float s = x[c] * cg;
            if (satAmt > 0.0f)
                s = s + satAmt * (std::tanh (drive * s) / drive - s); // calidez / suciedad
            x[c] = s;
        }

        // Ancho: agrega un lado estéreo que desaparece en mono (no rompe la compatibilidad)
        const float mono = numOut > 1 ? 0.5f * (x[0] + x[1]) : x[0];
        widthBuf[(size_t) widthPos] = mono;
        if (numOut > 1 && width > 0.0f)
        {
            const float side = 0.5f * width * (widthBuf[(size_t) ((widthPos - wA + wSize) % wSize)]
                                             - widthBuf[(size_t) ((widthPos - wB + wSize) % wSize)]);
            x[0] += side; x[1] -= side;
        }
        widthPos = (widthPos + 1) % wSize;

        // Delay ping-pong con repeticiones cada vez más oscuras
        const int dt = juce::jlimit (1, dSize - 1, (int) delayTimeSamples.getNextValue());
        const int rp = (delayPos - dt + dSize) % dSize;
        const float dl = delayBuf[0][(size_t) rp], dr = delayBuf[1][(size_t) rp];
        delayLp[0] = lpCoef * delayLp[0] + (1 - lpCoef) * dr;
        delayLp[1] = lpCoef * delayLp[1] + (1 - lpCoef) * dl;
        delayBuf[0][(size_t) delayPos] = mono + fb * delayLp[0];
        delayBuf[1][(size_t) delayPos] = fb * delayLp[1];
        delayPos = (delayPos + 1) % dSize;
        if (numOut > 1) { x[0] += delayMix * dl; x[1] += delayMix * dr; }
        else            { x[0] += delayMix * 0.5f * (dl + dr); }

        for (int c = 0; c < numOut; ++c) { ch[c][i] = x[c]; wet[c][i] = x[c]; }
    }
    grDisplay = maxGr;

    // Reverb en paralelo
    const float revAmt = p ("reverb") / 100.0f;
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.35f + 0.6f * (p ("sala") / 100.0f);
    rp.damping = 0.55f;
    rp.width = 0.6f + 0.4f * width;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    reverb.setParameters (rp);
    if (revAmt > 0.0f)
    {
        if (numOut > 1) reverb.processStereo (wet[0], wet[1], n);
        else            reverb.processMono (wet[0], n);
        for (int c = 0; c < numOut; ++c)
            buffer.addFrom (c, 0, wetBuf, c, 0, n, revAmt * 0.35f);
    }

    outGain.setTargetValue (juce::Decibels::decibelsToGain (p ("salida")));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int c = 0; c < numOut; ++c) ch[c][i] *= g;
    }
}

void FlyMixChatProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("chat", chatLog.joinIntoString ("\n"), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FlyMixChatProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        const auto chat = state.getProperty ("chat").toString();
        if (chat.isNotEmpty()) chatLog = juce::StringArray::fromLines (chat);
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* FlyMixChatProcessor::createEditor() { return new FlyMixChatEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FlyMixChatProcessor(); }
