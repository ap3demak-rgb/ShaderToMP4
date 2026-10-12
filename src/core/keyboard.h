// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Clavier simule (canal `keyboard`) : touches pressees a des instants donnes (`// key=65:2.0-4.5`), aucune lecture du vrai clavier.
#pragma once
#include "common.h"

struct KeyPress {
    int code = 0;        // code de touche JavaScript comme Shadertoy (65 = A, 32 = espace, 37..40 = fleches)
    double from = 0, to = 0;  // touche enfoncee de `from` (inclus) a `to` (exclu), en secondes du shader (iTime)
};

// Ajoute les appuis de « touche:debut-fin ; touche:debut-fin ... » (touche : code numerique, lettre a-z ou nom : space, enter, left,
// up, right, down, shift, ctrl, alt, esc, tab, backspace). Faux si le texte est invalide (rien n'est ajoute).
bool parseKeyPresses(const std::string& text, std::vector<KeyPress>& out);
std::string formatKeyPresses(const std::vector<KeyPress>& keys);

// Texture de Shadertoy, 256 x 3 octets pour l'image de temps t (duree d'une image dt) :
// ligne 0 = touche enfoncee, ligne 1 = enfoncee pendant cette image (appui commence dans l'image), ligne 2 = bascule
// (inversee a chaque appui depuis le debut du shader).
constexpr int kKeyboardW = 256, kKeyboardH = 3;
void keyboardState(const std::vector<KeyPress>& keys, double t, double dt, uint8_t out[kKeyboardW * kKeyboardH]);
