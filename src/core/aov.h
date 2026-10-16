// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Passes de sortie nommees (AOV) : ce qu'un shader ecrit en plus de la couleur, lu dans ses tampons Buffer A a D et range en canaux d'un
// fichier OpenEXR multicouche. Syntaxe, dans le shader (commentaires) ou dans le reglage « aov » separee par des points-virgules :
//   // aov depth=bufferB.r      profondeur dans le canal rouge du tampon B     -> canal « depth.Z »
//   // aov normal=bufferC.rgb   normale dans les canaux rgb du tampon C        -> « normal.X », « normal.Y », « normal.Z »
//   // aov id=bufferD.r         identifiant d'objet                            -> « id.R »
//   // aov alpha                ecrit l'alpha de l'image principale (canal A)
//   // aov motion=bufferC.rg    vecteurs de mouvement calcules par le shader (pixels par image)
//   // aov motion               vecteurs de mouvement ESTIMES entre deux images consecutives (estimation, pas une mesure)
#pragma once
#include "common.h"
#include "settings.h"

struct AovSpec {
    enum class Kind { Buffer, Alpha, MotionEstimate };
    Kind kind = Kind::Buffer;
    std::string name;                     // « depth », « normal »...
    int buffer = -1;                      // 0 a 3 : tampon A a D
    std::string swizzle;                  // « r », « rgb », « xy »... (au plus quatre composantes)
    std::vector<std::string> exrNames;    // nom de chaque canal ecrit dans le fichier
    std::vector<int> components;          // composante 0 a 3 lue dans le tampon pour chaque canal
};

// Lit les specifications du reglage. Faux et message si l'une est incorrecte.
bool parseAovs(const Settings& s, std::vector<AovSpec>& out, std::wstring& err);

// Vecteurs de mouvement estimes entre deux images (luminance, largeur x hauteur, de haut en bas) : correspondance de blocs sur une image reduite
// de moitie, vecteurs en pixels de l'image pleine, du pixel de `prev` vers sa position dans `cur`. vx et vy ont largeur x hauteur valeurs.
void estimateMotion(const std::vector<float>& prev, const std::vector<float>& cur, int width, int height, std::vector<float>& vx, std::vector<float>& vy);
