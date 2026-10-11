// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Passes multiples Shadertoy : Buffer A a D (textures flottantes persistantes d'une image a l'autre) et Common.
#pragma once
#include "channels.h"
#include "common.h"
#include "gl.h"
#include "hooks.h"
#include "settings.h"

// Les passes sont des fichiers a cote du shader : <nom>.bufferA.glsl ... <nom>.bufferD.glsl et <nom>.common.glsl.
// Chaque passe lit ses canaux par ses propres commentaires (// iChannel0=bufferA, image, noise...). Ordre : A, B, C, D, puis Image.
// Un tampon lu apres son execution dans l'image courante donne l'image courante ; lu avant (ou par lui-meme), l'image precedente.
class BufferPasses {
public:
    // Cherche les fichiers des passes. read : lit et normalise un fichier de shader.
    void discover(const std::wstring& shaderPath, const std::function<bool(const std::wstring&, std::string&)>& read, const Hooks& H);
    bool any() const { return used_[0] || used_[1] || used_[2] || used_[3]; }
    // Code de l'image : le code commun place devant le code du shader (numeros de ligne du shader conserves).
    std::string withCommon(const std::string& code) const;
    const std::wstring& commonPath() const { return commonPath_; }
    int commonLines() const;
    // Tout le code des passes (commun + tampons), pour le rapport de compatibilite.
    std::string allCode() const;

    // Compile les passes, charge leurs canaux, cree les cibles RGBA32F (rw x rh) remises a zero. false + err en cas d'echec.
    bool build(int rw, int rh, const std::wstring& shaderDir, const std::wstring& ffmpeg, const Hooks& H, std::wstring& err);
    // Textures les plus recentes des quatre tampons (0 si absent), pour lier les canaux de l'image.
    void latest(GLuint out[4]) const;
    // Execute les tampons pour l'image n. setUniforms(prog, n) renseigne iTime, iFrame, iMouse, iDate... de chaque programme.
    // L'appelant restaure sa cible, son viewport et son programme ensuite.
    void step(long long n, int rw, int rh, const std::function<void(GLuint prog, long long n)>& setUniforms);
    void release();
    // Remet tous les tampons a zero (retour au debut d'une boucle, ou en arriere dans l'apercu).
    void reset();

private:
    struct Buf {
        std::string code;
        std::wstring path;  // fichier de la passe
        Settings st;       // canaux lus dans les commentaires de la passe
        GLuint prog = 0;
        ChannelSet chans;
        GLuint tex[2] = {0, 0}, fbo[2] = {0, 0};
        int read = 0;      // indice de la texture terminee la plus recente
    };
    std::string common_;
    std::wstring commonPath_;
    Buf bufs_[4];
    bool used_[4] = {false, false, false, false};
};
