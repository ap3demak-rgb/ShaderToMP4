// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Comparer deux shaders (ou deux versions du même), garder des images de référence pour ses propres shaders et estimer le temps de rendu.
// Tout se fait sur de petites images rendues par une sonde ; seule l'estimation du temps lance de vrais rendus courts.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

// --- Comparaison de deux shaders ---------------------------------------------------------------------------------------------------------
struct CompareOptions {
    int samples = 12;          // instants compares, repartis sur la duree
    double tolerance = 1.0;    // difference maximale admise (0 a 255) : au-dela, les shaders sont « differents »
    std::string video;         // "" aucune, sbs (cote a cote), curtain (rideau qui balaie), diff (difference amplifiee)
    double videoSeconds = 6;   // duree de la video de comparaison
};
struct CompareResult {
    int frames = 0;
    double meanAbs = 0, maxAbs = 0;  // ecart moyen et maximal (0 a 255) sur toutes les images
    double psnr = 0;                 // dB ; 0 quand les images sont identiques
    double worstTime = 0;            // instant de l'ecart maximal
    bool identical = false, different = false;  // identique au bit pres ; ecart maximal au-dessus de la tolerance
    std::wstring diffFile, videoFile;
};
bool compareShaders(const std::wstring& pathA, const std::wstring& pathB, const std::wstring& exeDir, const Settings& s, const CompareOptions& opt, const Hooks& H,
                    CompareResult& res, std::wstring& err);
// Deux images RGBA (premiere ligne en bas) de meme taille : ecart moyen, maximal et erreur quadratique moyenne sur les composantes rouge, verte et bleue
void diffRgba(const uint8_t* a, const uint8_t* b, int w, int h, double& meanAbs, int& maxAbs, double& mse);
// Phrase localisee du resultat
std::wstring describeCompare(const CompareResult& r);

// Version du fichier avant / apres edition : une image de chaque au meme instant, pour le rideau de l'editeur. Les deux textes sont ecrits dans des
// fichiers temporaires a cote du shader (<nom>~avant.glsl, <nom>~apres.glsl) puis supprimes. Seule la passe Image est comparee.
bool compareVersions(const std::wstring& shaderPath, const std::string& before, const std::string& after, const std::wstring& exeDir, const Settings& s,
                     double t, std::vector<uint8_t>& bgraBefore, std::vector<uint8_t>& bgraAfter, int& w, int& h, std::wstring& err);

// --- Images de reference ------------------------------------------------------------------------------------------------------------------------
struct GoldenResult {
    int frames = 0;
    double worstMean = 0;    // plus grand ecart moyen (0 a 255) a la reference
    int worstIndex = 0;
    double worstTime = 0;
    bool ok = true;
    std::wstring dir;
};
// Enregistre `count` images (192 pixels au plus) dans <dossier du shader ou de sortie>/golden/<nom>/ ; verifie ensuite qu'elles n'ont pas change
// (ecart moyen par image <= tolerance, sur 255). Les images dependent du GPU et du pilote : la tolerance les absorbe.
bool goldenRecord(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, int count, const Hooks& H, GoldenResult& res, std::wstring& err);
bool goldenVerify(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, double tolerance, const Hooks& H, GoldenResult& res, std::wstring& err);

// --- Estimation du temps de rendu ------------------------------------------------------------------------------------------------------------
struct Estimate {
    long long frames = 0;
    double videoSeconds = 0;
    double fpsMin = 0, fpsMean = 0, fpsMax = 0;      // images par seconde mesurees (rendu et encodage) sur trois instants
    double secLow = 0, secMid = 0, secHigh = 0;      // duree estimee du fichier (avec ses variantes)
    bool slow = false;                                // trop lent pour les reglages demandes
    std::vector<std::wstring> suggestions;            // lignes localisees : quoi baisser et ce que cela donnerait
};
// Mesure quelques secondes de vrai rendu (trois instants repartis, encodeur compris) puis extrapole. Contexte OpenGL courant requis.
bool estimateRender(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const Hooks& H, Estimate& est, std::wstring& err);
// Bornes, alerte de lenteur et propositions a partir de vitesses mesurees (images par seconde) ; est.frames et est.videoSeconds sont deja remplis
void estimateFromSpeeds(const Settings& s, const double* fps, int n, Estimate& est);
std::wstring formatDuration(double seconds);   // « 45 s », « 3 min 20 s », « 1 h 04 min »
std::wstring describeEstimate(const Estimate& e);  // lignes localisees (estimation puis alerte et propositions)
