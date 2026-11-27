# FLY Plugins para FL Studio

Este paquete tiene dos plugins VST3, los dos en código fuente (C++ con JUCE).

- **FlyVozEQ**: tocás *ANALIZAR VOZ*, cantás o hablás 8 segundos y el plugin ecualiza la voz solo. Después elegís el estilo con un botón (Natural, Cálida, Brillante, Podcast o Radio) y con *Intensidad* manejás cuánto se aplica.
- **FlyMixChat**: escribís "mix type drake", "type cro con más reverb", "más oscuro" o "sin delay", y el plugin arma la cadena de voz completa: filtro de graves, EQ, de-esser, compresor, saturación, ancho estéreo, delay sincronizado al tempo y reverb. Todas las perillas se pueden retocar a mano.

## Opción A: compilar en tu PC (Windows)

1. Instalá **Visual Studio 2022 Community** y marcá "Desarrollo para el escritorio con C++" (eso ya trae CMake).
2. Instalá **Git** (git-scm.com).
3. Abrí la *Developer PowerShell for VS 2022*, entrá a la carpeta del plugin y ejecutá:
   ```
   cmake -B build -A x64
   cmake --build build --config Release --target FlyVozEQ_VST3
   ```
   (Para el otro plugin, cambiá el target a `FlyMixChat_VST3`.) La primera vez baja JUCE solo y tarda unos minutos.
4. Copiá la carpeta `build\FlyVozEQ_artefacts\Release\VST3\FLY VozEQ.vst3` a
   `C:\Program Files\Common Files\VST3\`
5. En FL Studio andá a *Options → Manage plugins → Find plugins*. Después lo cargás en un slot del Mixer.

## Opción B: que GitHub lo compile por vos (sin instalar nada)

El repo trae `.github/workflows/build.yml`: en cada cambio, en la pestaña **Actions**, a los ~10 minutos aparece el archivo **FLY-…-VST3-Windows** listo para descargar.

## Uso rápido

- **VozEQ**: ponelo primero en la cadena de la voz. Analizá con la voz real de la canción (no con silencio). El análisis queda guardado en el proyecto.
- **MixChat**: ponelo después del VozEQ (o del autotune). Podés escribir frases como:
  "type drake", "hacelo tipo bad bunny pero sin reverb", "más al frente", "menos eses", "más ancha", "más sucio", "lofi".
- Si la voz suena muy fuerte o muy baja, ajustá la perilla **SALIDA**.
