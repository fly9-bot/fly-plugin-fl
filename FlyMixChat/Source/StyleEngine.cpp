#include "StyleEngine.h"

const char* const MixSettings::ids[count] = {
    "corte", "graves", "barro", "presencia", "aire", "compresion", "saturacion",
    "deesser", "delay", "delaynota", "reverb", "sala", "ancho" };

const char* const MixSettings::labels[count] = {
    "CORTE", "GRAVES", "BARRO", "PRESENCIA", "AIRE", "COMPRES.", "SATURAC.",
    "DE-ESSER", "DELAY", "NOTA DELAY", "REVERB", "SALA", "ANCHO" };

int MixSettings::indexOf (const char* id)
{
    for (int i = 0; i < count; ++i)
        if (juce::String (ids[i]) == id) return i;
    return -1;
}

namespace
{
    struct Range { float lo, hi; };
    constexpr Range ranges[MixSettings::count] = {
        { 20, 200 }, { -6, 6 }, { -8, 2 }, { -6, 8 }, { -8, 8 }, { 0, 100 }, { 0, 100 },
        { 0, 100 }, { 0, 100 }, { 0, 3 }, { 0, 100 }, { 0, 100 }, { 0, 100 } };

    struct Style
    {
        const char* name;
        std::initializer_list<const char*> keys;
        // corte graves barro presencia aire comp sat deess delay nota reverb sala ancho
        std::array<float, MixSettings::count> s;
        const char* desc;
    };

    // Nota delay: 0 = 1/4, 1 = 1/8 con puntillo, 2 = 1/8, 3 = 1/16
    const Style styles[] = {
        { "Drake", { "drake", "drizzy", "ovo" },
          { 90, 1.5f, -2.5f, 1.5f, 1.5f, 55, 15, 50, 12, 0, 22, 75, 20 },
          "voz cálida y al frente, agudos suaves, compresión pareja y una reverb grande pero oscura atrás" },
        { "Cro", { "cro", "cr0" },
          { 110, 0, -2, 2, 4, 60, 20, 55, 25, 1, 35, 85, 45 },
          "voz aireada y etérea, mucho brillo arriba, delay con puntillo y reverb larga y abierta" },
        { "Travis Scott", { "travis", "travis scott", "la flame" },
          { 100, 1, -3, 1, 2, 70, 45, 45, 30, 2, 40, 90, 50 },
          "voz saturada y psicodélica, delay en corcheas y reverb enorme bien ancha" },
        { "Bad Bunny", { "bad bunny", "benito", "badbo" },
          { 100, 0.5f, -2, 3, 3, 70, 20, 55, 15, 2, 18, 60, 25 },
          "voz brillante, bien adelante y comprimida, con espacio justo" },
        { "Duki", { "duki", "duko" },
          { 110, 0, -2.5f, 3.5f, 2.5f, 75, 35, 55, 18, 2, 15, 55, 30 },
          "voz agresiva y presente, compresión fuerte y algo de suciedad" },
        { "Trueno", { "trueno" },
          { 100, 0.5f, -2, 3, 2, 70, 25, 50, 8, 3, 8, 40, 10 },
          "rap seco y en la cara, mucha articulación y casi nada de efecto" },
        { "Bizarrap session", { "bizarrap", "biza", "session", "bzrp" },
          { 100, 0.5f, -2.5f, 2.5f, 3, 70, 18, 55, 14, 2, 16, 60, 25 },
          "voz moderna y limpia, brillante, con delay y reverb cortitos" },
        { "The Weeknd", { "weeknd", "the weeknd", "abel" },
          { 90, 1, -2, 2, 3, 55, 15, 50, 22, 0, 40, 80, 40 },
          "voz suave y ochentosa, reverb grande y delay en negras" },
        { "Emilia / Tini (pop)", { "emilia", "tini", "maria becerra", "pop" },
          { 120, 0, -2, 3, 4, 65, 12, 60, 14, 1, 25, 65, 30 },
          "voz pop brillante y pulida, con brillo y espacio" },
        { "Lo-fi", { "lofi", "lo-fi", "lo fi" },
          { 180, 2, 0, -2, -6, 60, 55, 20, 20, 0, 25, 60, 20 },
          "voz opaca y vieja, sin agudos, saturada como un casete" },
        { "Trap oscuro", { "trap", "drill" },
          { 100, 1, -2.5f, 2, 1, 70, 30, 50, 20, 2, 22, 70, 30 },
          "voz de trap oscura, saturada, con delay en corcheas" },
        { "RKT / cumbia", { "rkt", "cumbia", "l-gante", "lgante", "turreo" },
          { 120, 0, -2, 4, 2, 80, 35, 45, 10, 3, 10, 45, 15 },
          "voz al frente, cruda y comprimida para que corte en el boliche" },
    };

    struct Modifier
    {
        std::initializer_list<const char*> keys;
        std::initializer_list<std::pair<int, float>> deltas; // (parámetro, cambio)
        const char* up;   // texto cuando suma
        const char* down; // texto cuando resta
        bool canRemove;   // "sin X" lo pone en 0
    };

    const Modifier modifiers[] = {
        { { "reverb", "reverberacion", "espacio", "mojado", "eco largo" }, { { Reverb, 12 }, { Sala, 8 } }, "más reverb", "menos reverb", true },
        { { "delay", "eco", "repeticion" }, { { Delay, 12 } }, "más delay", "menos delay", true },
        { { "oscuro", "oscura", "dark" }, { { Aire, -2.5f }, { Presencia, -1 } }, "más oscura", "menos oscura", false },
        { { "brillante", "brillo", "aire", "aireada", "aireado" }, { { Aire, 2.5f }, { Presencia, 0.8f } }, "más brillo", "menos brillo", false },
        { { "graves", "cuerpo", "gruesa", "grueso" }, { { Graves, 2 }, { Corte, -15 } }, "más cuerpo", "menos graves", false },
        { { "al frente", "adelante", "compresion", "comprimida", "comprimido", "pegada" }, { { Compresion, 15 }, { Presencia, 0.8f } }, "más al frente", "menos compresión", true },
        { { "sucia", "sucio", "saturada", "saturado", "saturacion", "distorsion", "agresiva", "agresivo" }, { { Saturacion, 20 } }, "más saturación", "menos saturación", true },
        { { "suave", "natural", "limpia", "limpio" }, { { Compresion, -12 }, { Saturacion, -12 } }, "más suave", "menos suave", false },
        { { "ancha", "ancho", "abierta", "abierto", "stereo", "estereo" }, { { Ancho, 25 } }, "más ancha", "más angosta", true },
        { { "eses", "sibilancia", "ss", "de-esser", "deesser" }, { { DeEsser, -20 } }, "con más eses", "con menos eses", false },
        { { "barro", "embarrada", "nasal", "cajosa" }, { { Barro, 2 } }, "con más barro", "con menos barro", false },
        { { "presencia", "clara", "definida" }, { { Presencia, 2 } }, "más presencia", "menos presencia", false },
    };

    juce::String normalise (juce::String s)
    {
        s = s.toLowerCase();
        s = s.replaceCharacters (juce::String::fromUTF8 ("áéíóúüñ"), "aeiouun");
        return s.replaceCharacters (",.!?¿¡;:", "        ");
    }

    // Busca la palabra (o frase) y devuelve dónde arranca, o -1.
    int findKey (const juce::String& text, const juce::String& key)
    {
        const juce::String padded = " " + text + " ";
        const int i = padded.indexOf (" " + key + " ");
        return i;
    }

    // Mira la palabra anterior: "menos"/"poco" = -1, "sin" = quitar, otro = +1
    int signBefore (const juce::String& text, int pos, bool& remove)
    {
        auto before = juce::StringArray::fromTokens (text.substring (0, juce::jmax (0, pos)), " ", "");
        before.removeEmptyStrings();
        remove = false;
        for (int k = before.size() - 1; k >= juce::jmax (0, before.size() - 2); --k)
        {
            const auto& w = before[k];
            if (w == "sin" || w == "saca" || w == "sacale" || w == "quita" || w == "quitale") { remove = true; return -1; }
            if (w == "menos" || w == "poco" || w == "baja" || w == "bajale") return -1;
            if (w == "mas" || w == "sube" || w == "subile" || w == "muy" || w == "bien") return 1;
        }
        return 1;
    }
}

void StyleEngine::clampAll (MixSettings& m)
{
    for (int i = 0; i < MixSettings::count; ++i)
        m[i] = juce::jlimit (ranges[i].lo, ranges[i].hi, m[i]);
    m[DelayNota] = std::round (m[DelayNota]);
}

juce::StringArray StyleEngine::quickStyles()
{
    return { "Drake", "Cro", "Travis", "Bad Bunny", "Duki", "Trueno", "Weeknd", "Lo-fi" };
}

StyleResult StyleEngine::interpret (const juce::String& message, const MixSettings& current)
{
    StyleResult r;
    r.settings = current;
    const auto text = normalise (message);

    // 1) ¿Nombró un estilo/artista?
    const Style* found = nullptr;
    for (auto& st : styles)
    {
        for (auto* k : st.keys)
            if (findKey (text, k) >= 0) { found = &st; break; }
        if (found) break;
    }
    if (found)
    {
        for (int i = 0; i < MixSettings::count; ++i)
            r.settings[i] = found->s[(size_t) i];
        r.styleName = found->name;
    }

    // 2) Ajustes extra: "más reverb", "menos oscuro", "sin delay"...
    juce::StringArray notes;
    for (auto& m : modifiers)
    {
        for (auto* k : m.keys)
        {
            const int pos = findKey (text, k);
            if (pos < 0) continue;
            bool remove = false;
            const int sign = signBefore (text, pos, remove);
            if (remove && m.canRemove)
            {
                for (auto& d : m.deltas) if (d.first != Sala && d.first != Presencia) r.settings[d.first] = ranges[d.first].lo;
                notes.add (juce::String::fromUTF8 ("sin ") + k);
            }
            else
            {
                for (auto& d : m.deltas) r.settings[d.first] += d.second * (float) sign;
                notes.add (juce::String::fromUTF8 (sign > 0 ? m.up : m.down));
            }
            break;
        }
    }

    // 3) Arma la respuesta
    clampAll (r.settings);
    r.understood = found != nullptr || ! notes.isEmpty();
    if (found)
    {
        r.reply = juce::String::fromUTF8 ("Listo, mix tipo ") + found->name + ": " + juce::String::fromUTF8 (found->desc);
        if (! notes.isEmpty()) r.reply += juce::String::fromUTF8 (". Además: ") + notes.joinIntoString (", ");
        r.reply += ".";
    }
    else if (! notes.isEmpty())
    {
        r.reply = juce::String::fromUTF8 ("Ajustado: ") + notes.joinIntoString (", ") + ".";
    }
    else
    {
        r.reply = juce::String::fromUTF8 ("No reconocí ese estilo. Probá con: type drake, type cro, travis, bad bunny, duki, trueno, "
                                          "bizarrap, weeknd, pop, lo-fi, trap o rkt. También podés pedir \"más reverb\", "
                                          "\"más oscuro\", \"sin delay\", \"más al frente\"...");
    }
    return r;
}
