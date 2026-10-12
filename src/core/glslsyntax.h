// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Coloration syntaxique GLSL (editeur de shader) : decoupe un texte en plages de meme nature.
#pragma once
#include "common.h"

enum class SyntaxKind { Plain, Comment, Directive, Keyword, Type, Function, Number, Preprocessor, Uniform };

struct SyntaxSpan {
    int start = 0, length = 0;  // en unites du texte analyse (wchar_t)
    SyntaxKind kind = SyntaxKind::Plain;
};

// Plages colorees, dans l'ordre ; ce qui n'est dans aucune plage est du texte ordinaire.
// Commentaires // et /* */, directives du programme (// cle=valeur, // uniform ...), mots-cles, types, fonctions GLSL,
// uniforms de Shadertoy (iTime, iResolution, iChannel0...), nombres, lignes #.
std::vector<SyntaxSpan> highlightGlsl(const std::wstring& text);
