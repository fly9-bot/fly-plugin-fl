#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

// Reverb de estudio: difusores + red de 8 líneas (FDN) con colas que se oscurecen.
// Sin memoria dinámica en process(): seguro para el hilo de audio.
class FlyReverb
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static constexpr double lineMs[N] = { 31.7, 37.9, 41.3, 47.1, 53.9, 59.3, 67.7, 73.9 };
        for (int i = 0; i < N; ++i)
        {
            len[i] = std::max (8, (int) (lineMs[i] * 0.001 * fs));
            lines[i].assign ((size_t) len[i] + 1, 0.0f);
            pos[i] = 0;
            lp[i] = 0.0f;
        }
        static constexpr double apMs[NA] = { 4.7, 6.9, 9.3, 12.1 };
        for (int i = 0; i < NA; ++i)
        {
            apLen[i] = std::max (4, (int) (apMs[i] * 0.001 * fs));
            ap[i].assign ((size_t) apLen[i], 0.0f);
            apPos[i] = 0;
        }
        pre.assign ((size_t) (0.12 * fs) + 2, 0.0f);
        prePos = 0;
        setParams (2.0f, 6000.0f, 30.0f);
    }

    void setParams (float decaySeconds, float dampHz, float predelayMs)
    {
        for (int i = 0; i < N; ++i)
            gain[i] = (float) std::pow (10.0, -3.0 * len[i] / (fs * std::max (0.2f, decaySeconds)));
        dampCoef = (float) std::exp (-2.0 * 3.14159265358979 * dampHz / fs);
        preSamples = std::clamp ((int) (predelayMs * 0.001 * fs), 1, (int) pre.size() - 1);
    }

    // Entrada mono, salida estéreo (solo señal mojada).
    inline void process (float in, float& outL, float& outR) noexcept
    {
        // Pre-delay: separa la voz de la cola, así no se empasta
        pre[(size_t) prePos] = in;
        int rp = prePos - preSamples; if (rp < 0) rp += (int) pre.size();
        float x = pre[(size_t) rp];
        if (++prePos >= (int) pre.size()) prePos = 0;

        // Difusión (densidad, evita el sonido metálico)
        for (int i = 0; i < NA; ++i)
        {
            float& d = ap[i][(size_t) apPos[i]];
            const float v = x + 0.62f * d;
            const float y = d - 0.62f * v;
            d = v;
            if (++apPos[i] >= apLen[i]) apPos[i] = 0;
            x = y;
        }

        // Red de retardos con matriz Householder
        std::array<float, N> o;
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            o[(size_t) i] = lines[i][(size_t) pos[i]];
            sum += o[(size_t) i];
        }
        const float h = sum * (2.0f / N);
        for (int i = 0; i < N; ++i)
        {
            float fb = (o[(size_t) i] - h) * gain[i];
            lp[i] = dampCoef * lp[i] + (1.0f - dampCoef) * fb;   // cada rebote más oscuro
            lines[i][(size_t) pos[i]] = lp[i] + x * 0.35f;
            if (++pos[i] >= len[i] + 1) pos[i] = 0;
        }
        outL = 0.5f * (o[0] - o[2] + o[4] - o[6] + o[1]);
        outR = 0.5f * (o[1] - o[3] + o[5] - o[7] + o[2]);
    }

private:
    static constexpr int N = 8, NA = 4;
    double fs = 48000.0;
    std::array<std::vector<float>, N> lines;
    std::array<int, N> len {}, pos {};
    std::array<float, N> gain {}, lp {};
    std::array<std::vector<float>, NA> ap;
    std::array<int, NA> apLen {}, apPos {};
    std::vector<float> pre;
    int prePos = 0, preSamples = 1;
    float dampCoef = 0.5f;
};
