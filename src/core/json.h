// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Lecteur JSON minimal (RFC 8259) pour les exports de shaders : objets, tableaux, chaines (UTF-8, \uXXXX), nombres, booleens, null.
#pragma once
#include "common.h"

struct JsonValue {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<JsonValue> items;                           // tableau
    std::vector<std::pair<std::string, JsonValue>> members;  // objet (ordre conserve)

    const JsonValue* get(const std::string& key) const;  // membre d'un objet, nullptr si absent
    std::string str(const std::string& key, const std::string& fallback = "") const;  // chaine d'un membre
    double num(const std::string& key, double fallback = 0) const;
};

// false + message (avec la position) si le texte n'est pas du JSON valide
bool parseJson(const std::string& text, JsonValue& out, std::string& error);
