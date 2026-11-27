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

void FlyMixChatProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& f : filt)
    {
        f.hpf.reset(); f.low.reset(); f.mud.reset(); f.pres.reset(); f.air.reset(); f.deessHp.reset();
        f.deessHp.setHighpass (fs, 5500.0, 0.707);
    }
    lastFilterValues.fill (-999.0f);
    compEnv = deessEnv = duckEnv = limEnv = 0.0f;

    // Al envío de efectos le sacamos graves y eses: los efectos quedan limpios y no embarran
    sendHp.reset(); sendHp.setHighpass (fs, 300.0, 0.707);
    sendLp.reset();
    sendLp.setShelf (fs, juce::jmin (6500.0, fs * 0.4), -12.0, true);
    for (auto& f : satLp) { f.reset(); f.setShelf (fs, juce::jmin (9000.0, fs * 0.4), -4.0, true); }

    for (auto& d : delayBuf) d.assign ((size_t) (fs * 2.5) + 4, 0.0f);
    delayPos = 0;
    delayLp.fill (0.0f);
    delayTimeSamples.reset (sampleRate, 0.25);
    delayTimeSamples.setCurrentAndTargetValue ((float) (fs * 0.5));

    reverb.prepare (fs);
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

    // ---- EQ: solo se recalcula si cambió algo ----
    const float vals[5] = { p ("corte"), p ("graves"), p ("barro"), p ("presencia"), p ("aire") };
    bool changed = false;
    for (int i = 0; i < 5; ++i) changed |= vals[i] != lastFilterValues[(size_t) i];
    if (changed)
    {
        for (auto& f : filt)
        {
            f.hpf.setHighpass (fs, vals[0], 0.707);
            f.low.setShelf (fs, 160.0, vals[1], false);
            f.mud.setPeak (fs, 350.0, 1.1, vals[2]);
            f.pres.setPeak (fs, 3500.0, 0.8, vals[3]);
            f.air.setShelf (fs, juce::jmin (10000.0, fs * 0.4), vals[4], true);
        }
        for (int i = 0; i < 5; ++i) lastFilterValues[(size_t) i] = vals[i];
    }

    // ---- Compresor (voz pegada al frente) ----
    const float compAmt = p ("compresion") / 100.0f;
    const float thr = -8.0f - 26.0f * compAmt;
    const float ratio = 2.0f + 6.0f * compAmt;
    const float makeup = -thr * (1.0f - 1.0f / ratio) * 0.45f;
    const float atk = std::exp (-1.0f / (0.002f * (float) fs)), rel = std::exp (-1.0f / (0.09f * (float) fs));

    // ---- De-esser ----
    const float deAmt = p ("deesser") / 100.0f;
    const float deThr = -14.0f - 24.0f * deAmt;
    const float deAtk = std::exp (-1.0f / (0.0007f * (float) fs)), deRel = std::exp (-1.0f / (0.05f * (float) fs));

    // ---- Saturación en paralelo (calienta sin romper) ----
    const float satAmt = p ("saturacion") / 100.0f;
    const float drive = 1.5f + 5.0f * satAmt;
    const float satNorm = 1.0f / std::tanh (drive * 0.5f) * 0.5f;
    const float satMix = 0.7f * satAmt;

    // ---- Envío de efectos ----
    const float width = p ("ancho") / 100.0f;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = juce::jlimit (40.0, 240.0, *b);
    static constexpr double beats[4] = { 1.0, 0.75, 0.5, 0.25 };
    const int nota = juce::jlimit (0, 3, (int) p ("delaynota"));
    delayTimeSamples.setTargetValue ((float) juce::jmin (fs * 2.4, fs * 60.0 / bpm * beats[nota]));
    const float delayAmt = p ("delay") / 100.0f;
    const float delayRet = 2.0f * delayAmt;
    const float fb = 0.30f + 0.25f * delayAmt;
    const float dlpCoef = std::exp (-2.0f * juce::MathConstants<float>::pi * 3500.0f / (float) fs);
    const int dSize = (int) delayBuf[0].size();

    const float revAmt = p ("reverb") / 100.0f;
    const float sala = p ("sala") / 100.0f;
    reverb.setParams (0.8f + 3.2f * sala, 7500.0f - 3000.0f * sala, 15.0f + 45.0f * sala);
    const float revRet = 3.2f * revAmt;
    const float duckAtk = std::exp (-1.0f / (0.01f * (float) fs)), duckRel = std::exp (-1.0f / (0.35f * (float) fs));
    const float fxSide = 0.4f + 1.1f * width;

    float* ch[2] = { buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr };
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

        // De-esser
        float hi[2] {}, hiPeak = 0.0f;
        for (int c = 0; c < numOut; ++c) { hi[c] = filt[(size_t) c].deessHp.process (x[c], c); hiPeak = juce::jmax (hiPeak, std::abs (hi[c])); }
        deessEnv = hiPeak > deessEnv ? deAtk * deessEnv + (1 - deAtk) * hiPeak : deRel * deessEnv + (1 - deRel) * hiPeak;
        const float deDb = juce::Decibels::gainToDecibels (deessEnv, -100.0f);
        const float deGain = (deAmt > 0.0f && deDb > deThr) ? juce::Decibels::decibelsToGain (juce::jmax (-14.0f, -(deDb - deThr) * 0.8f)) : 1.0f;
        for (int c = 0; c < numOut; ++c) x[c] -= hi[c] * (1.0f - deGain);

        // Compresor
        float peak = 0.0f;
        for (int c = 0; c < numOut; ++c) peak = juce::jmax (peak, std::abs (x[c]));
        compEnv = peak > compEnv ? atk * compEnv + (1 - atk) * peak : rel * compEnv + (1 - rel) * peak;
        const float envDb = juce::Decibels::gainToDecibels (compEnv, -100.0f);
        const float gr = envDb > thr ? (envDb - thr) * (1.0f - 1.0f / ratio) : 0.0f;
        maxGr = juce::jmax (maxGr, gr);
        const float cg = juce::Decibels::decibelsToGain (makeup - gr);

        for (int c = 0; c < numOut; ++c)
        {
            float s = x[c] * cg;
            if (satMix > 0.0f)
            {
                const float sat = satLp[c].process (std::tanh (drive * s * 0.5f) * satNorm, c);
                s += satMix * (sat - s) + satMix * 0.5f * sat; // mezcla paralela con cuerpo extra
            }
            x[c] = s;
        }

        // ---- Efectos ----
        const float mono = numOut > 1 ? 0.5f * (x[0] + x[1]) : x[0];
        const float aMono = std::abs (mono);
        duckEnv = aMono > duckEnv ? duckAtk * duckEnv + (1 - duckAtk) * aMono : duckRel * duckEnv + (1 - duckRel) * aMono;
        const float duck = 1.0f / (1.0f + 4.0f * duckEnv);   // los efectos bajan mientras cantás y suben en los huecos
        const float send = sendLp.process (sendHp.process (mono, 0), 0);

        // Delay ping-pong con lectura suave (sin clics al cambiar de tempo)
        const float dt = juce::jlimit (2.0f, (float) dSize - 2.0f, delayTimeSamples.getNextValue());
        float rpos = (float) delayPos - dt;
        if (rpos < 0) rpos += (float) dSize;
        const int r0 = (int) rpos; const float fr = rpos - (float) r0; const int r1 = (r0 + 1) % dSize;
        const float dl = delayBuf[0][(size_t) r0] * (1 - fr) + delayBuf[0][(size_t) r1] * fr;
        const float dr = delayBuf[1][(size_t) r0] * (1 - fr) + delayBuf[1][(size_t) r1] * fr;
        delayLp[0] = dlpCoef * delayLp[0] + (1 - dlpCoef) * dr;
        delayLp[1] = dlpCoef * delayLp[1] + (1 - dlpCoef) * dl;
        delayBuf[0][(size_t) delayPos] = send + fb * delayLp[0];
        delayBuf[1][(size_t) delayPos] = fb * delayLp[1];
        if (++delayPos >= dSize) delayPos = 0;

        // La reverb recibe la voz y también un poco del delay (se funden)
        float rl = 0.0f, rr = 0.0f;
        reverb.process (send + 0.3f * (dl + dr) * delayAmt, rl, rr);

        float fxL = duck * (delayRet * dl + revRet * rl);
        float fxR = duck * (delayRet * dr + revRet * rr);
        const float fxM = 0.5f * (fxL + fxR), fxS = 0.5f * (fxL - fxR) * fxSide;
        fxL = fxM + fxS; fxR = fxM - fxS;

        if (numOut > 1) { ch[0][i] = x[0] + fxL; ch[1][i] = x[1] + fxR; }
        else            { ch[0][i] = x[0] + fxM; }
    }
    grDisplay = maxGr;

    outGain.setTargetValue (juce::Decibels::decibelsToGain (p ("salida")));
    // Limitador final: nunca pasa de -1 dB (sin clips en FL)
    const float limRel = std::exp (-1.0f / (0.08f * (float) fs));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        float pk = 0.0f;
        for (int c = 0; c < numOut; ++c) { ch[c][i] *= g; pk = juce::jmax (pk, std::abs (ch[c][i])); }
        limEnv = pk > limEnv ? pk : limRel * limEnv + (1 - limRel) * pk;
        const float lg = limEnv > 0.89f ? 0.89f / limEnv : 1.0f;
        for (int c = 0; c < numOut; ++c) ch[c][i] *= lg;
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
