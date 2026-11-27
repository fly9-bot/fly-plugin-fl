#pragma once
#include <juce_core/juce_core.h>
#include <array>

// Todo lo que el chat puede mover. Los nombres coinciden con los IDs de parámetros.
struct MixSettings
{
    static constexpr int count = 13;
    static const char* const ids[count];
    static const char* const labels[count];
    std::array<float, count> v {};

    float& operator[] (int i) { return v[(size_t) i]; }
    float operator[] (int i) const { return v[(size_t) i]; }
    static int indexOf (const char* id);
};

// Índices para escribir más claro
enum Mx { Corte, Graves, Barro, Presencia, Aire, Compresion, Saturacion, DeEsser,
          Delay, DelayNota, Reverb, Sala, Ancho };

struct StyleResult
{
    bool understood = false;
    MixSettings settings;
    juce::String reply;   // lo que contesta el chat
    juce::String styleName;
};

namespace StyleEngine
{
    // Entiende cosas como "mix type drake", "type cro con más reverb", "más oscuro", "sin delay".
    StyleResult interpret (const juce::String& message, const MixSettings& current);
    juce::StringArray quickStyles();     // los botones rápidos
    void clampAll (MixSettings&);
}
