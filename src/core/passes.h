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
    bool any() const { return used_[0] || used_[1] || used_[2] || used_[3] || cubeUsed_; }
    // Declarations GLSL des parametres (uniforms personnalises) ajoutees a chaque programme compile par build().
    void setPreamble(const std::string& preamble) { preamble_ = preamble; }
    // Code de l'image : le code commun place devant le code du shader (numeros de ligne du shader conserves).
    std::string withCommon(const std::string& code) const;
    const std::wstring& commonPath() const { return commonPath_; }
    // Passe Sound (<nom>.sound.glsl) : rendue a part par soundpass.h, pas a chaque image.
    bool hasSound() const { return !sound_.empty(); }
    const std::string& soundCode() const { return sound_; }
    const std::wstring& soundPath() const { return soundPath_; }
    // Passe finale <nom>.post.glsl (mainImage appliquee a l'image rendue, iChannel0 = l'image) : lue par postfx.h
    bool hasPost() const { return !post_.empty(); }
    const std::string& postCode() const { return post_; }
    const std::wstring& postPath() const { return postPath_; }
    const Settings& postSettings() const { return postSt_; }  // canaux iChannel1-3 de la passe
    int commonLines() const;
    // Tout le code des passes (commun + tampons), pour le rapport de compatibilite.
    std::string allCode() const;
    // Rapport de compatibilite sur les passes : canaux declares mais jamais lus (image comprise) et passes que ni l'image ni une
    // passe utile ne lit. imageSettings / imageCode : l'image principale.
    void report(const Settings& imageSettings, const std::string& imageCode, std::vector<std::wstring>& notes) const;

    // Compile les passes, charge leurs canaux, cree les cibles RGBA32F (rw x rh) remises a zero. false + err en cas d'echec.
    // s : taille propre de chaque tampon (bufferA.scale / .size), sinon la taille du rendu.
    bool build(int rw, int rh, const std::wstring& shaderDir, const std::wstring& ffmpeg, const Hooks& H, std::wstring& err,
               double start = 0, double fps = 60, const Settings* s = nullptr);
    // Taille du tampon i (0 a 3) apres build : les canaux qui le lisent (iChannelResolution) doivent la connaitre.
    void bufferSize(int i, int& w, int& h) const { w = sw_[i]; h = sh_[i]; }
    // Corrige la taille des canaux de cs qui lisent un tampon (loadChannels les a crees a la taille du rendu).
    void fixChannelSizes(ChannelSet& cs) const;
    // Textures les plus recentes des quatre tampons (0 si absent), pour lier les canaux de l'image.
    void latest(GLuint out[5]) const;  // [0..3] tampons A-D, [4] cubemap de la passe Cubemap A
    // Execute les tampons pour l'image n. setUniforms(prog, n) renseigne iTime, iFrame, iMouse, iDate... de chaque programme.
    // L'appelant restaure sa cible, son viewport et son programme ensuite.
    void step(long long n, double t, int rw, int rh, const std::function<void(GLuint prog, long long n)>& setUniforms);
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
    struct Cube {  // passe « Cubemap A » : mainCubemap rendue sur les six faces, a chaque image
        std::string code;
        std::wstring path;
        Settings st;
        GLuint prog = 0;
        ChannelSet chans;
        GLuint tex[2] = {0, 0}, fbo[2] = {0, 0};
        int read = 0;
    };
    Cube cube_;
    bool cubeUsed_ = false;
    std::string preamble_;
    std::string post_;
    std::wstring postPath_;
    Settings postSt_;
    std::string sound_;
    std::wstring soundPath_;
    std::string common_;
    std::wstring commonPath_;
    Buf bufs_[4];
    bool used_[4] = {false, false, false, false};
    int sw_[4] = {0, 0, 0, 0}, sh_[4] = {0, 0, 0, 0};  // taille de chaque tampon
};
