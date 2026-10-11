// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Import hors ligne d'un shader Shadertoy : le fichier JSON exporte depuis la page du shader (aucune cle d'API, aucun reseau).
// Chaque passe devient un fichier a cote du fichier principal (voir passes.h) et les entrees deviennent des commentaires
// `// iChannelN=...` ; ce que ShaderToMP4 ne sait pas rejouer (medias de Shadertoy, webcam, clavier) est signale.
#pragma once
#include "common.h"

struct ShadertoyImport {
    std::wstring mainFile;                 // <nom>.glsl (onglet Image)
    std::vector<std::wstring> files;       // tous les fichiers ecrits
    std::vector<std::wstring> notes;       // remarques pour le journal (medias remplaces, licence...)
    std::wstring title, author, id;
};

// Ecrit les fichiers dans outDir (cree si besoin). false + err si le JSON n'est pas un export de shader.
bool importShadertoyJson(const std::wstring& jsonPath, const std::wstring& outDir, ShadertoyImport& result, std::wstring& err);

// Chemin a rendre pour un fichier donne a l'interface ou a la ligne de commande : le fichier lui-meme, ou, pour un `.json`,
// le shader importe dans <exeDir>/shadertoy/<nom>/. Vide + err en cas d'echec. log recoit les remarques de l'import.
std::wstring resolveShaderInput(const std::wstring& exeDir, const std::wstring& path, const std::function<void(const std::wstring&)>& log,
                                std::wstring& err);
