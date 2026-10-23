// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Explication des erreurs de compilation GLSL les plus courantes : une phrase d'aide et une correction proposee, affichees sous la
// ligne d'erreur du journal. Le code du shader n'est jamais modifie.
#pragma once
#include "common.h"

struct ErrorHelp {
    std::string id;     // regle reconnue (undeclared, channel, texture, type, overload, ctor, syntax, main, redefined, assign, swizzle, divzero)
    std::wstring help;  // ce qui se passe
    std::wstring fix;   // ce qu'on peut essayer
};

// Reconnait la ligne d'un journal de compilateur (NVIDIA, Mesa, AMD) ; faux si aucune regle ne s'applique
bool explainGlslError(const std::wstring& driverLine, ErrorHelp& out);
