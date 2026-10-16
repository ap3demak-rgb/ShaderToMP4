// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Petites expressions numeriques d'un parametre qui suit la musique : `iBass * 0.5 + 0.2`, `max(iBeat, iOnset)`, `smoothstep(0.2, 0.8, iEnergy)`.
// Operateurs + - * / ^ et parentheses ; fonctions sin cos tan abs sqrt exp log floor ceil fract sign min max pow mod clamp mix smoothstep step ;
// constante pi ; variables : celles du rythme (iBeat, iBeatCount, iBPM, iBass, iMid, iTreble, iEnergy, iOnset) et iTime.
#pragma once
#include "common.h"

// Variables d'une expression, dans l'ordre de kExprVarNames
enum ExprVar { EV_BEAT, EV_BEATCOUNT, EV_BPM, EV_BASS, EV_MID, EV_TREBLE, EV_ENERGY, EV_ONSET, EV_TIME, EV_COUNT };
extern const char* const kExprVarNames[EV_COUNT];

struct ExprEnv {
    double v[EV_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
};

class Expr {
public:
    // Faux + message (position comprise) si le texte n'est pas une expression valide
    bool parse(const std::string& text, std::string& err);
    bool empty() const { return code_.empty(); }
    double eval(const ExprEnv& env) const;
    // Vrai si l'expression lit une variable du rythme (iBeat... iOnset) : elle demande alors l'analyse du son
    bool usesRhythm() const { return usesRhythm_; }

private:
    struct Op {
        int kind = 0;       // 0 constante, 1 variable, 2 operateur binaire, 3 negation, 4 fonction
        double value = 0;   // constante
        int index = 0;      // variable, operateur ou fonction
        int argc = 0;       // fonction
    };
    std::vector<Op> code_;
    bool usesRhythm_ = false;
    friend struct ExprParser;
};

// Vrai si `text` contient, comme mot entier, une variable du rythme (iBeat, iBeatCount, iBPM, iBass, iMid, iTreble, iEnergy, iOnset)
bool mentionsRhythmVar(const std::string& text);
