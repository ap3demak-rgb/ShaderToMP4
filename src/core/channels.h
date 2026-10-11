// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Canaux d'entree Shadertoy : iChannel0 a iChannel3 lies a des textures 2D (image, bruit ou grille generes).
#pragma once
#include "common.h"
#include "gl.h"
#include "hooks.h"
#include "settings.h"

struct ChannelSet {
    GLuint tex[4] = {0, 0, 0, 0};
    int w[4] = {0, 0, 0, 0}, h[4] = {0, 0, 0, 0};
    bool any() const { return tex[0] || tex[1] || tex[2] || tex[3]; }
};

// Charge les canaux demandes par `s` (chemins relatifs au dossier du shader ; « noise » et « grid » sont generes).
// Les images sont lues par ffmpeg (PNG, JPG, HDR, EXR... tout ce qu'il decode). Contexte OpenGL courant requis.
bool loadChannels(const Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, ChannelSet& out, const Hooks& H,
                  std::wstring& err);
// Lie les textures aux unites 1 a 4 et renseigne iChannel0-3 et iChannelResolution du programme (actif).
void bindChannels(const ChannelSet& cs, GLuint prog);
void freeChannels(ChannelSet& cs);
