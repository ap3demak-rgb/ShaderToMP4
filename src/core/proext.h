// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Points d'accroche de l'edition Pro : le moteur public ne connait que ces interfaces ; les fonctions Pro (scenes 3D, passes de geometrie...) vivent dans le
// depot prive, sont compilees avec ce depot et s'enregistrent au demarrage par registerProExtensions(). L'edition libre n'enregistre rien : aucun de ces
// points d'accroche n'est alors appele.
#pragma once
#include "common.h"
#include "gl.h"
#include "hooks.h"
#include "settings.h"

// Demande de scene 3D pour un rendu : le shader image, ses reglages, la taille du rendu.
struct SceneRequest {
    std::wstring shaderPath;  // shader image (fragments)
    std::wstring exeDir;
    const Settings* settings = nullptr;
    const std::string* code = nullptr;  // code du shader image apres adaptation (commentaires // compris)
    int width = 0, height = 0;          // taille du rendu (iResolution du programme principal)
    bool preview = false;               // image d'apercu ou lecteur temps reel
    const Hooks* hooks = nullptr;       // journal
};

// Scene 3D : maillage dessine par un etage sommets a la place du triangle plein ecran, devant le meme fragment shader que le mode image.
// Chaque methode est appelee dans le thread de rendu, contexte OpenGL courant.
class ProScene {
public:
    virtual ~ProScene() {}
    virtual std::string version() const { return "330"; }  // « 330 » ou « 430 » (passes de calcul)
    virtual std::string vertexSource() = 0;                // etage sommets complet (#version comprise) du programme principal
    virtual std::wstring vertexPath() const = 0;           // fichier de l'etage sommets (« » = etage par defaut), pour localiser les erreurs
    virtual int vertexLines() const = 0;                   // nombre de lignes de ce fichier
    // Apres la compilation du programme principal : tampons du maillage, cibles, passes. w x h : taille du rendu.
    virtual bool prepare(GLuint prog, int w, int h, std::wstring& err) = 0;
    // Dessine l'image (programme principal actif, uniform de temps deja regles) au lieu du triangle plein ecran : la cible liee, la fenetre, les
    // parametres de melange (accumulation) sont ceux de l'appelant et lui sont rendus intacts. t : temps du shader ; weight, premul : poids de fondu
    // et mode d'alpha que l'appelant a regles dans le programme (st_weight, st_premul).
    virtual void draw(double t, float weight, bool premul) = 0;
    // Tout ce qui change le resultat (maillage, camera, passes) : entre dans la signature des parties reprenables.
    virtual std::string signature() const = 0;
};

struct ProExt {
    // Cree la scene d'un rendu. Renvoie nullptr sans erreur si le shader n'en demande pas ; nullptr avec err renseigne si la demande est invalide.
    std::function<std::unique_ptr<ProScene>(const SceneRequest&, std::wstring& err)> makeScene;
    // Cas d'auto-test de l'edition Pro (contexte OpenGL courant) : ajoute au rapport, compte les cas et les echecs.
    std::function<void(const std::wstring& exeDir, const std::wstring& testsDir, std::wstring& report, int& total, int& fails)> selfTest;
};

void registerProExt(const ProExt& ext);
const ProExt* proExt();  // nullptr dans l'edition libre

// Appelee au demarrage par les points d'entree : enregistre les extensions si l'edition Pro les fournit (registerProExtensions), sinon ne fait rien.
void initProExtensions();
