// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Contexte OpenGL 3.3 core sans serveur d'affichage (ni X11 ni Wayland) par EGL : ferme de rendu, conteneur, integration continue.
// libEGL est chargee a l'execution (dlopen) : le programme n'en depend pas. Linux seulement ; sous macOS available() reste faux.
#pragma once
#include "common.h"

namespace platform {
namespace egl {

// Charge libEGL et ouvre l'affichage (surfaceless Mesa, sinon l'affichage EGL par defaut) ; faux et message dans `err` si libEGL ou le
// pilote manquent. Sans effet si c'est deja fait.
bool open(std::wstring& err);
bool available();  // open() a reussi
// Un nouveau contexte OpenGL 3.3 core (chaque thread de rendu a le sien, comme avec GLFW) ; nullptr et message dans `err` en cas d'echec
void* createContext(std::wstring& err);
bool makeCurrent(void* context, std::wstring& err);  // le contexte devient courant dans le thread appelant
void release();                                      // plus de contexte courant dans le thread appelant
void destroyContext(void* context);
void* proc(const char* name);  // adresse d'une fonction OpenGL

}  // namespace egl
}  // namespace platform
