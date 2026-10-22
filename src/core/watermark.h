// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Filigrane d'essai : texte discret en bas a droite de la couleur d'une sortie qui utilise une fonction Pro sans licence valide (voir prolicense.h).
// Position, taille, texte et opacite sont fixes (non reglables) ; il ne touche que la couleur de l'image rendue, jamais les passes de donnees
// (profondeur, identifiants, mouvement). Dessine en virgule flottante-compatible : un simple melange sur la cible courante.
#pragma once
#include "common.h"

class Watermark {
public:
    Watermark() = default;
    Watermark(const Watermark&) = delete;
    Watermark& operator=(const Watermark&) = delete;
    ~Watermark() { release(); }

    // Prepare le texte (bitmap) et le programme. fullW x fullH : taille de l'image rendue ; outputH : hauteur de la sortie finale, qui fixe la taille du texte
    // (rapport conserve avec le sur-echantillonnage : ssFactor pixels rendus par pixel de sortie). Contexte OpenGL courant requis.
    bool build(int fullW, int fullH, int outputH, int ssFactor, std::wstring& err);
    bool active() const { return prog_ != 0; }
    // Melange le filigrane sur la cible liee. tileW x tileH : zone dessinee (la tuile), (offX, offY) : son origine dans l'image complete.
    // Laisse aucun programme actif, le melange coupe, l'unite de texture 0 active.
    void draw(int tileW, int tileH, int offX, int offY);
    void release();

    // Texte et bitmap RGBA (haut en bas) pour la taille donnee : exposes pour l'auto-test.
    static std::string textFor(int fullW, int pixelScale);
    static void bitmap(const std::string& text, int pixelScale, std::vector<uint8_t>& rgba, int& w, int& h);
    static int pixelScaleFor(int outputH, int ssFactor);

private:
    unsigned prog_ = 0, tex_ = 0;
    int fullW_ = 0, fullH_ = 0, bw_ = 0, bh_ = 0, ox_ = 0, oy_ = 0;
};
