// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Les onglets d'un shader Shadertoy : Image (le fichier du shader) et ses passes, des fichiers a cote de lui
// (<nom>.bufferA.glsl ... <nom>.common.glsl, <nom>.cubemapA.glsl, <nom>.sound.glsl, <nom>.post.glsl).
#pragma once
#include "common.h"

struct PassTab {
    std::string id;      // image, bufferA..bufferD, common, cubemapA, sound, post
    std::wstring label;  // nom de l'onglet (noms techniques, non traduits)
    std::wstring path;   // fichier ; pour une passe absente, celui qui serait cree
    bool exists = false;
};

// Neuf onglets dans l'ordre Image, Buffer A-D, Common, Cubemap A, Sound, Post. Une passe existante garde l'extension trouvee
// (.glsl ou .txt, comme le moteur la cherche) ; une passe absente est proposee en .glsl.
std::vector<PassTab> passTabs(const std::wstring& shaderPath);

// Squelette d'une nouvelle passe (code minimal qui compile)
std::string passTemplate(const std::string& id);

// Cree le fichier de la passe avec son squelette (UTF-8, sans BOM). Faux et message dans `err` s'il existe deja ou ne peut pas etre ecrit.
bool createPassFile(const PassTab& tab, std::wstring& err);
