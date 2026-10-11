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
    int buffer[4] = {-1, -1, -1, -1};  // canal lu dans un tampon (0 a 3 = Buffer A a D), sinon -1
    std::string filter[4], wrap[4];    // reglages appliques a la lecture d'un tampon
    bool any() const { return tex[0] || tex[1] || tex[2] || tex[3]; }
    bool anyBuffer() const { return buffer[0] >= 0 || buffer[1] >= 0 || buffer[2] >= 0 || buffer[3] >= 0; }
};

// Charge les canaux demandes par `s` (chemins relatifs au dossier du shader ; « noise » et « grid » sont generes).
// Les images sont lues par ffmpeg (PNG, JPG, HDR, EXR... tout ce qu'il decode). Contexte OpenGL courant requis.
// « bufferA » a « bufferD » designent les tampons des passes multiples (de taille bufW x bufH).
bool loadChannels(const Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, ChannelSet& out, const Hooks& H,
                  std::wstring& err, int bufW = 0, int bufH = 0);
// Lie les textures aux unites 1 a 4 et renseigne iChannel0-3 et iChannelResolution du programme (actif).
// bufferTex : textures a jour des quatre tampons (peut changer a chaque image).
void bindChannels(const ChannelSet& cs, GLuint prog, const GLuint* bufferTex = nullptr);
void freeChannels(ChannelSet& cs);
