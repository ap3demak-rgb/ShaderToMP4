// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Calcul reparti, sans serveur : les parties d'une video (rendu par parties, `chunk`) sont rendues en meme temps par plusieurs contextes OpenGL de ce poste
// (`parallel=N`) et/ou par plusieurs ordinateurs qui partagent un dossier (`farm=dossier`). Aucun reseau n'est ouvert : seul le dossier choisi est lu et
// ecrit. Une partie est reservee par un fichier de verrou a creation atomique ; un verrou qui n'est plus renouvele (ordinateur eteint) est repris ; une partie
// terminee n'est jamais refaite ; l'assemblage est fait par le premier ordinateur qui voit toutes les parties.
#pragma once
#include "common.h"
#include "hooks.h"

struct LockKeeper;

struct PartsPlan {
    long long nChunks = 0, chunkFrames = 0, totalFrames = 0;
    std::wstring partsDir;     // parties terminees : <partsDir>/NNNNN<ext> ; les parties en cours y sont ecrites sous un nom temporaire
    std::wstring ext;          // « .mkv »
    int workers = 1;           // contextes de rendu de ce processus (le thread appelant en est un)
    std::wstring farmDir;      // dossier partage de la ferme ; vide = pas de ferme
    double lockTimeout = 120;  // secondes sans renouvellement apres lesquelles un verrou est repris
    // Ce que tous les ordinateurs doivent avoir en commun pour que les parties se joignent sans nouvel encodage (ferme) : « signature », « version »,
    // « ffmpeg », « encoder », « format », « frames », « chunk » ; les autres cles sont gardees telles quelles
    std::vector<std::pair<std::string, std::string>> jobInfo;
};

struct PartsStats {
    int partsDone = 0;         // parties rendues par ce processus
    int resumed = 0;           // parties deja presentes au depart
    int retired = 0;           // contextes de rendu retires apres une panne
    int redistributed = 0;     // parties lentes confiees aussi a un contexte libre
    bool assembler = true;     // ce processus assemble la video (toujours vrai hors ferme)
    bool alreadyAssembled = false;  // ferme : le travail a deja ete assemble (rien a faire)
    std::wstring assembledBy;  // ferme : l'ordinateur qui a assemble, quand ce n'est pas celui-ci
    std::shared_ptr<LockKeeper> keeper;  // ferme : garde le verrou d'assemblage vivant jusqu'a farmAssembled / farmAbandon
};

// Rend la partie `chunk` (images [n0, n1)) dans le fichier `tmp` avec le contexte OpenGL du thread appelant
using RenderPartFn = std::function<Result(int worker, long long chunk, long long n0, long long n1, const std::wstring& tmp, const Hooks& H, std::wstring& err)>;

// Rend toutes les parties manquantes. Ok : toutes les parties existent dans partsDir (et, en ferme, ce processus sait s'il assemble). Annule : l'annulation de H.
Result runParts(const PartsPlan& plan, const Hooks& H, const RenderPartFn& render, std::wstring& err, PartsStats& stats);

// Ferme : a appeler par l'ordinateur qui assemble, une fois la video ecrite (ou abandonnee : output vide). Ecrit assembled.txt et farm-report.json.
void farmAssembled(const PartsPlan& plan, PartsStats& stats, const std::wstring& output, const std::function<void(const std::wstring&)>& log);
void farmAbandon(const PartsPlan& plan, PartsStats& stats);

// Etat d'un dossier de ferme : parties faites, en cours, libres, ordinateur de chacune, temps restant estime. Faux si le dossier ne contient pas de travail.
bool farmStatus(const std::wstring& farmDir, double lockTimeout, std::vector<std::wstring>& lines);
// Nom de cet ordinateur dans la ferme (nom de machine et numero de processus)
std::wstring farmMachineId();
