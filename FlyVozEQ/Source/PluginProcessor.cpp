#include "PluginProcessor.h"
#include "PluginEditor.h"

// Curva "ideal" de una voz bien grabada (dB por banda de octava).
// Las voces tienen más energía en medios-graves y caen hacia los agudos.
static constexpr float idealVoice[FlyVozEQProcessor::numBands] = { -6, -1, 0, 0, -3, -7, -11, -16 };

// Carácter de cada botón (dB extra por banda).
static constexpr float modeCurves[FlyVozEQProcessor::numModes][FlyVozEQProcessor::numBands] = {
    {   0,  0,    0,    0,   0,   0,    0,   0 },  // Natural
    { 1.5f, 2.5f, 1,    0,  -1,  -2, -2.5f, -3 },  // Cálida
    {  -1, -1, -0.5f,   0,   1, 2.5f, 3.5f,  4 },  // Brillante
    { 2.5f, 1,   -2, -0.5f, 0.5f, 2, 1.5f,   0 },  // Podcast
    { -12, -4,    0,    2,   3,   1,   -6, -12 },  // Radio
};

// Máximo que el análisis puede subir en cada banda (los extremos más cuidados
// para no levantar ruido de fondo, rumble o soplido).
static constexpr float maxBoost[FlyVozEQProcessor::numBands] = { 3, 5, 6, 6, 6, 6, 5, 4 };
static constexpr float maxCut = 9.0f;

juce::StringArray FlyVozEQProcessor::modeNames()
{
    return { "Natural", juce::String::fromUTF8 ("Cálida"), "Brillante", "Podcast", "Radio" };
}

FlyVozEQProcessor::FlyVozEQProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& m : measuredDb)  m = 0.0f;
    for (auto& g : displayGain) g = 0.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout FlyVozEQProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "modo", 1 }, "Modo", modeNames(), 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "intensidad", 1 }, "Intensidad",
                                                   NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f,
                                                   AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "salida", 1 }, "Salida",
                                                   NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                   AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "hpf", 1 }, "Quitar graves", true));
    return l;
}

bool FlyVozEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void FlyVozEQProcessor::prepareToPlay (double sampleRate, int)
{
    currentFs = sampleRate;
    hpf.reset();
    hpf.setHighpass (currentFs, 80.0, 0.707);
    float g[numBands];
    computeTargetGains (g);
    for (int b = 0; b < numBands; ++b)
    {
        currentGain[(size_t) b] = g[b];
        peaks[(size_t) b].reset();
        peaks[(size_t) b].setPeak (currentFs, juce::jmin ((double) bandFreqs[b], currentFs * 0.45), bandQ, g[b]);
    }
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("salida")->load()));
    fifoIndex = 0;
}

void FlyVozEQProcessor::startAnalysis()
{
    // Se resetea acá; el hilo de audio arranca a acumular cuando ve analyzing = true.
    bandAccum.fill (0.0);
    voicedFrames = 0;
    fifoIndex = 0;
    framesNeeded = juce::jmax (1, (int) (8.0 * currentFs / fftSize)); // ~8 s de voz real
    progress = 0.0f;
    analyzing = true;
}

void FlyVozEQProcessor::computeTargetGains (float* out) const
{
    const int mode = juce::jlimit (0, numModes - 1, (int) apvts.getRawParameterValue ("modo")->load());
    const float amount = apvts.getRawParameterValue ("intensidad")->load() / 100.0f;

    float corr[numBands] {};
    if (hasAnalysis)
    {
        // Diferencia entre la voz ideal y la medida, alineada en los medios.
        float diff[numBands];
        float offset = 0.0f;
        for (int b = 0; b < numBands; ++b)
            diff[b] = idealVoice[b] - measuredDb[(size_t) b].load();
        for (int b = 1; b <= 5; ++b)
            offset += diff[b];
        offset /= 5.0f;
        for (int b = 0; b < numBands; ++b)
            corr[b] = juce::jlimit (-maxCut, maxBoost[b], (diff[b] - offset) * 0.85f);
    }

    for (int b = 0; b < numBands; ++b)
        out[b] = juce::jlimit (-15.0f, 12.0f, (corr[b] + modeCurves[mode][b]) * amount);
}

void FlyVozEQProcessor::analyzeFrame()
{
    // Solo cuenta frames con voz (descarta silencio y ruido de fondo).
    double sumSq = 0.0;
    for (auto s : fifo) sumSq += (double) s * s;
    const double rmsDb = 10.0 * std::log10 (sumSq / fftSize + 1e-20);
    if (rmsDb < -50.0)
        return;

    std::copy (fifo.begin(), fifo.end(), fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double binHz = currentFs / fftSize;
    double frameBands[numBands] {};
    double total = 0.0;
    for (int b = 0; b < numBands; ++b)
    {
        const int lo = juce::jmax (1, (int) std::floor (bandFreqs[b] / std::sqrt (2.0) / binHz));
        const int hi = juce::jmin (fftSize / 2 - 1, (int) std::ceil (bandFreqs[b] * std::sqrt (2.0) / binHz));
        for (int k = lo; k <= hi; ++k)
            frameBands[b] += (double) fftData[(size_t) k] * fftData[(size_t) k];
        total += frameBands[b];
    }
    if (total <= 0.0)
        return;

    // Normaliza cada frame para que hable fuerte o suave pese igual.
    for (int b = 0; b < numBands; ++b)
        bandAccum[(size_t) b] += frameBands[b] / total;

    ++voicedFrames;
    progress = juce::jmin (1.0f, (float) voicedFrames / (float) framesNeeded);

    if (voicedFrames >= framesNeeded)
    {
        for (int b = 0; b < numBands; ++b)
            measuredDb[(size_t) b] = (float) (10.0 * std::log10 (bandAccum[(size_t) b] / voicedFrames + 1e-12));
        hasAnalysis = true;
        analyzing = false;
    }
}

void FlyVozEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    for (int ch = numCh; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // Nivel de entrada (para el medidor) y análisis.
    float peak = 0.0f;
    const bool doAnalysis = analyzing.load();
    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            mono += buffer.getSample (ch, i);
        mono /= (float) juce::jmax (1, numCh);
        peak = juce::jmax (peak, std::abs (mono));

        if (doAnalysis)
        {
            fifo[(size_t) fifoIndex++] = mono;
            if (fifoIndex == fftSize)
            {
                fifoIndex = 0;
                analyzeFrame();
                if (! analyzing.load()) break;
            }
        }
    }
    inputLevelDb = juce::jmax (inputLevelDb.load() - 1.5f, juce::Decibels::gainToDecibels (peak, -100.0f));

    // Suaviza los cambios de EQ para que no haya clics.
    float target[numBands];
    computeTargetGains (target);
    for (int b = 0; b < numBands; ++b)
    {
        auto& g = currentGain[(size_t) b];
        const float prev = g;
        g += (target[b] - g) * 0.15f;
        if (std::abs (target[b] - g) < 0.01f) g = target[b];
        if (g != prev)
            peaks[(size_t) b].setPeak (currentFs, juce::jmin ((double) bandFreqs[b], currentFs * 0.45), bandQ, g);
        displayGain[(size_t) b] = g;
    }

    const bool wantHpf = apvts.getRawParameterValue ("hpf")->load() > 0.5f;
    if (wantHpf && ! hpfOn) hpf.reset();
    hpfOn = wantHpf;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("salida")->load()));

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    for (int i = 0; i < n; ++i)
    {
        const float og = outGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float x = data[ch][i];
            if (hpfOn) x = hpf.process (x, ch);
            for (auto& p : peaks) x = p.process (x, ch);
            data[ch][i] = x * og;
        }
    }
}

void FlyVozEQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree an ("ANALISIS");
    an.setProperty ("tiene", hasAnalysis.load(), nullptr);
    for (int b = 0; b < numBands; ++b)
        an.setProperty ("b" + juce::String (b), measuredDb[(size_t) b].load(), nullptr);
    state.removeChild (state.getChildWithName ("ANALISIS"), nullptr);
    state.appendChild (an, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FlyVozEQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType()))
            return;
        auto an = state.getChildWithName ("ANALISIS");
        if (an.isValid())
        {
            for (int b = 0; b < numBands; ++b)
                measuredDb[(size_t) b] = (float) an.getProperty ("b" + juce::String (b), 0.0f);
            hasAnalysis = (bool) an.getProperty ("tiene", false);
            state.removeChild (an, nullptr);
        }
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* FlyVozEQProcessor::createEditor() { return new FlyVozEQEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FlyVozEQProcessor(); }
