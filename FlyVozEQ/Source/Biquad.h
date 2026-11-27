#pragma once
#include <cmath>
#include <complex>

// Filtro biquad (recetas RBJ). Sin memoria dinámica: seguro para el hilo de audio.
static constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    void setPeak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double c = std::cos (w);
        const double a0 = 1.0 + alpha / A;
        b0 = (float) ((1.0 + alpha * A) / a0);
        b1 = (float) ((-2.0 * c) / a0);
        b2 = (float) ((1.0 - alpha * A) / a0);
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - alpha / A) / a0);
    }

    void setHighpass (double fs, double f, double q)
    {
        const double w = 2.0 * kPi * f / fs;
        const double alpha = std::sin (w) / (2.0 * q);
        const double c = std::cos (w);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 + c) / 2.0) / a0);
        b1 = (float) ((-(1.0 + c)) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    inline float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    // Respuesta en dB a una frecuencia (para dibujar la curva).
    double magnitudeDb (double f, double fs) const
    {
        const double w = 2.0 * kPi * f / fs;
        const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
        const auto num = (double) b0 + (double) b1 * z1c + (double) b2 * z2c;
        const auto den = 1.0 + (double) a1 * z1c + (double) a2 * z2c;
        return 20.0 * std::log10 (std::abs (num / den) + 1e-12);
    }
};
