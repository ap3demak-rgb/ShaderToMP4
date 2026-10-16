// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Analyse d'un fichier son au temps exact d'une image : spectre et forme d'onde (canal `audio`), niveau et bandes (uniforms lies au son).
#pragma once
#include "common.h"
#include <complex>

// FFT (taille puissance de deux) sur place, partagee avec l'analyse du rythme
void fftInPlace(std::vector<std::complex<float>>& a);

class AudioAnalyzer {
public:
    // Decode tout le fichier en mono 44100 Hz, 16 bits (20 minutes au plus) par ffmpeg. Faux + err en cas d'echec.
    bool load(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err);
    bool loaded() const { return !pcm_.empty(); }
    // Les echantillons decodes (mono, 44100 Hz, 16 bits)
    const std::vector<int16_t>& samples() const { return pcm_; }

    // Ligne 0 (512 octets) : spectre d'une fenetre de 1024 echantillons se terminant au temps t (Blackman, -100 a -30 dB sur 0..255, comme
    // un analyseur web) ; ligne 1 (512 octets) : forme d'onde des 512 derniers echantillons (128 = silence).
    void rows(double t, uint8_t out[1024]) const;
    // Niveau (RMS des 1024 derniers echantillons, 1 = sinus a pleine echelle) et crete de trois bandes du spectre, tous dans [0, 1] :
    // graves 0 a 250 Hz, mediums 250 Hz a 2 kHz, aigus 2 a 10 kHz.
    struct Levels {
        float level = 0, bass = 0, mid = 0, treble = 0;
    };
    Levels levels(double t) const;

private:
    float sample(long long i) const { return i < 0 || i >= (long long)pcm_.size() ? 0.0f : (float)pcm_[(size_t)i]; }
    std::vector<int16_t> pcm_;
};
