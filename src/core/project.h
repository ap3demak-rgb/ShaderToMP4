// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fichier projet (.s2mproj) : la liste des shaders, les reglages generaux, ceux de chaque shader, les lots, les passes et les medias, dans un fichier JSON
// ecrit par le programme. Les chemins situes dans le dossier du projet y sont relatifs : le projet se deplace avec son dossier.
#pragma once
#include "common.h"
#include "settings.h"

constexpr int kProjectVersion = 1;
extern const wchar_t* const kProjectExt;  // L".s2mproj"

struct ProjectShader {
    std::wstring path;                  // chemin complet (relatif dans le fichier quand le shader est dans le dossier du projet)
    int batch = 0;                      // numero de lot, 0 = aucun
    int priority = 0;                   // -1 basse, 0 normale, 1 haute : ordre de rendu
    bool ownSettings = false;           // reglages propres a ce shader
    std::vector<Override> own;          // ... et leurs valeurs
    std::vector<std::wstring> passes;   // fichiers des passes qui existent (Buffer A-D, Common, ...), chemins complets
    std::vector<std::wstring> media;    // images, sons et videos des canaux iChannel0-3, chemins complets
};

struct Project {
    int version = kProjectVersion;
    std::wstring name;
    std::vector<Override> general;      // reglages generaux : tous ceux des fenetres + les reglages des fonctions Pro
    std::vector<ProjectShader> shaders;
};

// Reglages generaux d'un Settings sous forme de liste, et l'operation inverse (partant de `base`)
std::vector<Override> projectGeneral(const Settings& s);
Settings projectSettings(const std::vector<Override>& general, const Settings& base);

// Passes et medias d'un shader (fichiers qui existent ou sont cites : un fichier cite et absent figure dans `media`)
void projectFiles(const std::wstring& shaderPath, std::vector<std::wstring>& passes, std::vector<std::wstring>& media);

// Un projet pour la liste de shaders donnee : reglages generaux, reglages propres (jobs/) et lots lus dans le dossier de l'application
Project captureProject(const std::wstring& exeDir, const std::wstring& name, const Settings& general, const std::vector<std::wstring>& shaders,
                       const std::map<std::wstring, int>& batches, const std::map<std::wstring, int>& priorities = {});

// JSON du projet ; `dir` = dossier du fichier (les chemins qu'il contient y deviennent relatifs)
std::string projectToJson(const Project& p, const std::wstring& dir);
// Lit un projet (versions anciennes migrees) ; faux + message sinon. Les chemins relatifs sont resolus depuis `dir`.
bool projectFromJson(const std::string& text, const std::wstring& dir, Project& out, std::string& err);

bool saveProject(const std::wstring& path, const Project& p, std::wstring& err);
bool loadProject(const std::wstring& path, Project& out, std::wstring& err);

// Chemins du projet (shaders, passes, medias) qui n'existent plus
std::vector<std::wstring> projectMissing(const Project& p);

// Rend le projet actif : ecrit les reglages propres (jobs/) et les lots (jobs/batches.txt) de ses shaders. Renvoie les reglages generaux.
Settings applyProject(const std::wstring& exeDir, const Project& p, const Settings& base, std::map<std::wstring, int>& batches, std::map<std::wstring, int>* priorities = nullptr);

// Projets recents (jobs/recent_projects.txt, dix au plus, le dernier ouvert en tete, ceux qui n'existent plus sont ignores)
std::vector<std::wstring> recentProjects(const std::wstring& exeDir);
void addRecentProject(const std::wstring& exeDir, const std::wstring& path);
