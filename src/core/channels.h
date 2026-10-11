// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Canaux d'entree Shadertoy : iChannel0 a iChannel3 lies a des textures (2D, cubemap, volume), a l'audio d'un fichier
// (spectre et forme d'onde), a une video, ou a un tampon d'une passe multiple.
#pragma once
#include "common.h"
#include "gl.h"
#include "hooks.h"
#include "settings.h"

// Canal qui change a chaque image (audio, video) : met a jour sa texture pour le temps t / l'image n.
class DynamicChannel {
public:
    virtual ~DynamicChannel() {}
    virtual void update(double t, long long frameIndex) = 0;
};

struct ChannelSet {
    GLuint tex[4] = {0, 0, 0, 0};
    GLenum target[4] = {GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D};  // 2D, cubemap ou 3D
    int w[4] = {0, 0, 0, 0}, h[4] = {0, 0, 0, 0}, d[4] = {1, 1, 1, 1};
    int buffer[4] = {-1, -1, -1, -1};  // canal lu dans un tampon (0 a 3 = Buffer A a D), sinon -1
    std::string filter[4], wrap[4];    // reglages appliques a la lecture d'un tampon
    std::shared_ptr<DynamicChannel> dyn[4];  // audio / video
    bool any() const { return tex[0] || tex[1] || tex[2] || tex[3]; }
    bool anyBuffer() const { return buffer[0] >= 0 || buffer[1] >= 0 || buffer[2] >= 0 || buffer[3] >= 0; }
};

// Type GLSL du canal selon sa description : "sampler2D" (image, bruit, grille, audio, video, tampon), "samplerCube" (cube:...),
// "sampler3D" (volume:...). Le programme doit declarer iChannelN avec ce type avant d'etre compile.
std::string channelSamplerType(const std::wstring& spec);
void channelTypes(const Settings& s, std::string out[4]);

// Charge les canaux demandes par `s` (chemins relatifs au dossier du shader). Descriptions :
//   fichier image | noise | grid | bufferA..D | cube:fichier_%d.png (six faces) | cube:croix.png | cube:sky |
//   volume:fichier.png@N (N tranches cote a cote) | volume:noise | audio | audio:fichier | video:fichier
// Les images et sons sont lus par ffmpeg. `start` et `fps` calent l'audio et la video sur le temps de rendu.
// Contexte OpenGL courant requis. bufW x bufH : taille des tampons des passes multiples.
bool loadChannels(const Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, ChannelSet& out, const Hooks& H,
                  std::wstring& err, int bufW = 0, int bufH = 0, double start = 0, double fps = 60);
// Lie les textures aux unites 1 a 4 et renseigne iChannel0-3 et iChannelResolution du programme (actif).
// bufferTex : textures a jour des quatre tampons (peut changer a chaque image).
void bindChannels(const ChannelSet& cs, GLuint prog, const GLuint* bufferTex = nullptr);
// Met a jour les canaux audio et video pour le temps t (image n).
void updateChannels(ChannelSet& cs, double t, long long frameIndex);
void freeChannels(ChannelSet& cs);
