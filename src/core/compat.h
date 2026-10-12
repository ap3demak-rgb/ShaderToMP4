// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Compatibilite Shadertoy : conversions GLSL ES -> GLSL 3.30 et rapport des fonctions non prises en charge.
#pragma once
#include "common.h"
#include "settings.h"

// Adapte le code d'une passe : retire `#version` et `#extension` (les lignes sont videes, la numerotation est conservee).
// Les noms GLSL ES 1.00 (texture2D, textureCube...) sont traduits par des macros de l'enveloppe (gl.cpp).
// notes : une phrase par adaptation faite.
std::string adaptShaderCode(const std::string& code, std::vector<std::wstring>& notes);

// Rapport avant rendu sur l'ensemble du code (image, tampons, commun) : ce qui est ignore ou fige, au lieu d'un echec obscur.
void checkCompatibility(const std::string& allCode, const Settings& s, std::vector<std::wstring>& notes);

// Vrai si le code (hors commentaires) contient le mot entier `word` (iChannel0, iMouse...).
bool codeUsesWord(const std::string& code, const std::string& word);
