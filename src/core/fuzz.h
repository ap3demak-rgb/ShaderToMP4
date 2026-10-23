// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fuzzing des lecteurs de fichiers et de textes ecrits dans le programme (JSON, .montage, .ini, .cube, EXR, TIFF flottant, fiche .beats.json,
// jobs/*.ini, queue.txt, expressions, courbes de temps, effets...) : des entrees abimees de maniere reproductible (graine fixe) sont donnees a chaque
// lecteur ; aucune ne doit faire planter le programme, boucler sans fin, ni lever d'exception. Le mode `--fuzz=<cible|all> [--fuzz-count=N]
// [--fuzz-seed=N]` tourne dans son propre processus (un plantage est un code de sortie, pas la fin de l'auto-test) ; l'entree en cours est ecrite dans
// fuzz_last_<cible>.* du dossier de travail avant chaque appel : apres un plantage, c'est le cas fautif. L'edition Pro ajoute ses lecteurs (MIDI, CSV,
// glTF, OBJ, PLY) par registerFuzzTarget.
#pragma once
#include "common.h"

struct FuzzEnv {
    std::wstring dir;   // dossier de travail (ecriture libre) ; se termine par un separateur
    std::wstring path;  // fichier qui contient deja l'entree si la cible a une extension, sinon vide
};

struct FuzzTarget {
    std::string name;
    std::wstring ext;                 // extension du fichier d'entree (« .cube ») ; vide : lecteur de texte en memoire
    std::vector<std::string> seeds;   // entrees valides de depart (en plus des fichiers tests/fuzz/<cible>/*)
    std::function<std::vector<std::string>(const FuzzEnv& env)> makeSeeds;  // entrees valides fabriquees par le programme (EXR, TIFF...), facultatif
    std::function<void(const std::string& input, const FuzzEnv& env)> run;
};

void registerFuzzTarget(FuzzTarget t);
std::vector<std::string> fuzzTargetNames();

// Entree abimee de facon reproductible : bits retournes, octets remplaces, morceaux inseres / supprimes / dupliques / repetes, mots piegeux
std::string fuzzMutate(const std::vector<std::string>& seeds, uint64_t& rng);

// Codes de sortie : 0 tout va bien, 1 cible inconnue, 97 une entree a pris plus de 8 s, 98 exception levee par un lecteur
int runFuzzCli(const std::wstring& exeDir, const std::string& target, int count, unsigned seed, const std::function<void(const std::string&, bool)>& write);
