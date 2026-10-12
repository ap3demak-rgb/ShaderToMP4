// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iMouse simule : trajectoire par images cles (`// mouse=0:0.2,0.2 ; 5:0.8,0.6,down`), deterministe.
#pragma once
#include "common.h"

struct MouseKey {
    double t = 0;       // temps du shader (iTime), en secondes
    double x = 0, y = 0;  // fractions de la taille, origine en bas a gauche
    bool down = false;  // bouton enfonce a partir de cette cle
};

// Analyse « x,y[,down] » (forme simple, une cle a t = 0) ou « t:x,y[,down] ; t:x,y[,down] ... ». Faux si le texte est invalide.
bool parseMouseKeys(const std::string& text, std::vector<MouseKey>& out);
std::string formatMouseKeys(const std::vector<MouseKey>& keys);

// Valeur de iMouse pour l'image de temps t (duree d'une image dt) dans un rendu de w x h pixels.
// xy = position (interpolation lineaire entre deux cles) ; zw = position du dernier clic, z > 0 tant que le bouton est
// enfonce, w > 0 seulement sur l'image ou le clic a commence (comme Shadertoy), negatifs sinon.
void mouseAt(const std::vector<MouseKey>& keys, double t, double dt, int w, int h, float out[4]);
