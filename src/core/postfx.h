// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Post-traitement de l'image rendue, en virgule flottante avant la reduction en 8 ou 10 bits (donc avec le supersampling, le flou de mouvement
// et la sortie 10 bits) : passe finale <nom>.post.glsl (iChannel0 = l'image), puis effets predefinis, puis LUT couleur .cube, puis fondu.
#pragma once
#include "channels.h"
#include "common.h"
#include "gl.h"
#include "hooks.h"
#include "passes.h"
#include "settings.h"

struct PostEffectInfo {
    const char* name;  // bloom | sharpen | chroma | contrast | saturation | hue | vignette | grain
    double def;        // quantite par defaut
    double lo, hi;     // bornes
    const char* labelKey;  // cle de langue du nom affiche
};
// Les effets dans l'ordre ou ils sont appliques.
const std::vector<PostEffectInfo>& postEffectInfos();

struct PostEffect {
    int index = 0;      // position dans postEffectInfos()
    double amount = 0;
};
// « bloom:0.4,grain,vignette:0.5 » -> effets. Faux + unknown (le premier nom inconnu ou la quantite invalide) sinon.
bool parsePostEffects(const std::string& text, std::vector<PostEffect>& out, std::wstring& unknown);

class PostChain {
public:
    PostChain() = default;
    PostChain(const PostChain&) = delete;
    PostChain& operator=(const PostChain&) = delete;
    ~PostChain() { release(); }

    // Prepare la chaine (programmes, textures flottantes w x h, LUT, canaux de la passe .post). Contexte OpenGL courant requis.
    // paramDecls : declarations des parametres (uniforms personnalises) pour la passe .post. Faux + err si un element est invalide.
    bool build(const Settings& s, const BufferPasses& bp, const std::wstring& shaderDir, const std::wstring& ffmpeg, const Hooks& H,
               std::wstring& err, int w, int h, const std::string& paramDecls);
    bool active() const { return active_; }
    // Cible du rendu de l'image quand la chaine est active (texture flottante w x h).
    GLuint inputFbo() const { return in_.fbo; }
    // Applique la chaine a l'image rendue dans inputFbo() et ecrit le resultat dans targetFbo (8 ou 16 bits), multiplie par fade.
    // setUniforms(prog, n) renseigne iTime, iFrame, iMouse, parametres... de la passe .post. bufferTex : tampons les plus recents (ou nullptr).
    // Laisse targetFbo lie, la zone de dessin w x h, aucun programme actif, l'unite de texture 0 active.
    void apply(GLuint targetFbo, long long n, double t, float fade, const std::function<void(GLuint prog, long long n)>& setUniforms,
               const GLuint* bufferTex);
    void release();

private:
    struct Target {
        GLuint tex = 0, fbo = 0;
        int w = 0, h = 0;
    };
    bool makeTarget(Target& t, int w, int h, std::wstring& err);
    void drawTo(GLuint fbo, int w, int h);

    bool active_ = false, custom_ = false, composite_ = false, alpha_ = false;
    int hdr_ = 0;          // 0 = SDR, 1 = HDR10 (PQ), 2 = HLG : transfert applique en fin de chaine (BT.2020)
    float hdrWhite_ = 203;  // cd/m2 d'un blanc de shader a 1.0
    int w_ = 0, h_ = 0;
    double amount_[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // dans l'ordre de postEffectInfos()
    Target in_, mid_;
    Target bloomA_[3], bloomB_[3];
    GLuint progCustom_ = 0, progComposite_ = 0, progBright_ = 0, progBlur_ = 0, progDown_ = 0;
    ChannelSet chans_;
    GLuint lut3_ = 0, lut1_ = 0;
    int lutMode_ = 0, lutSize_ = 0;
    float lutMin_[3] = {0, 0, 0}, lutMax_[3] = {1, 1, 1};
};
