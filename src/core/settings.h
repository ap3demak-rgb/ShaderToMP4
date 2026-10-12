// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Reglages de la video : config.ini (a cote du .exe), commentaires `// cle=valeur` des shaders, fenetre.
#pragma once
#include "common.h"
#include "keyboard.h"
#include "mouse.h"

struct Settings {
    int width = 1920, height = 1080;
    double fps = 60.0, duration = 90.0, start = 0.0;
    int crf = 12;
    std::string preset = "slow";
    std::string codec = "h264";  // h264 | h265 | vp9 | av1 | prores_* | dnxhr_* (voir formats.h)
    std::string container = "mp4";  // mp4 | mov | mkv | webm | gif | webp | png | tiff | exr
    int depth = 8;                  // bits par composante : 8 ou 10 (rendu GL en 16 bits)
    std::wstring name;              // modele du nom de sortie ("" = {shader}) : {shader} {w} {h} {fps} {date} {codec}
    std::string exists = "overwrite";  // fichier de sortie deja present : overwrite | number | skip
    int ss = 1;                     // supersampling 1..4
    std::wstring outdir;            // dossier de sortie ("" = a cote du shader)
    int bitrate = 0;                // kb/s ; 0 = qualite constante (CRF)
    std::string color = "bt709";    // bt709 | bt709full | srgb | bt601
    int blur = 1;                   // flou de mouvement : sous-images par image (1 = aucun)
    double shutter = 0.5;           // ouverture de l obturateur, en fraction de la duree d une image
    double loop = 0;                // periode du shader (s) : la duree devient un multiple entier ; 0 = libre
    std::wstring audio;             // fichier son a mixer ("" = aucun)
    double fadein = 0, fadeout = 0; // fondus entree / sortie (s), image et son
    std::string encoder = "cpu";    // cpu | auto | nvenc | amf | qsv (encodage materiel)
    int pbo = 3;                    // profondeur de la file de lecture GPU (2..8)
    std::vector<std::string> variants;  // variantes du meme shader : "nom:cle=valeur|cle=valeur" (rendues apres le rendu de base)
    std::string endaction = "none"; // a la fin d'un lot reussi : none | folder | sleep (config.ini seulement)
    std::string gpu = "default";    // default | high | low : carte graphique sur un PC a deux GPU (Linux ; config.ini ou --gpu=)
    bool metadata = true;           // titre / commentaire ecrits dans le conteneur
    std::wstring channel[4];        // iChannel0-3 : chemin d'image, "noise" ou "grid" ("" = aucun)
    std::string chFilter[4];        // nearest | linear | mipmap ("" : mipmap pour une image, linear pour un tampon)
    std::string chWrap[4];          // clamp | repeat ("" : repeat pour une image, clamp pour un tampon)
    bool chVflip[4] = {true, true, true, true};                          // comme Shadertoy : haut de l'image en v = 1
    std::vector<MouseKey> mouseKeys;  // iMouse simule : images cles (vide = 0) ; positions en fractions de la taille (0,0 = bas gauche)
    double bufScale[4] = {0, 0, 0, 0};  // taille du tampon A-D : fraction de la taille du rendu (0 = taille du rendu)
    int bufW[4] = {0, 0, 0, 0}, bufH[4] = {0, 0, 0, 0};  // ou taille fixe en pixels (0 = taille du rendu)
    std::vector<KeyPress> keys;    // canal `keyboard` : touches pressees a des instants donnes
    std::string date;              // iDate fixe "AAAA-MM-JJ[THH:MM[:SS]]" ("" = date reelle)
    std::string vr = "auto";        // auto | off | mono | stereo : shader mainVR rendu en 360 equirectangulaire (stereo = haut / bas)
    double vrIpd = 0.064;           // ecart des yeux du rendu stereo, dans l'unite du shader
    int warmup = 0;                // images simulees avant la premiere (tampons chauffes, iFrame decale de N) ; 0 = aucune
    double chunk = 30;             // rendu par parties de N secondes, reprenables (0 = jamais)
};

bool sameSettings(const Settings& a, const Settings& b);
void parseLines(Settings& s, const std::string& text, bool onlySlashComments);  // ini (false) ou commentaires // (true)
void writeIni(const std::wstring& path, const Settings& s);
// Reglage nomme (ligne de commande : --width=1280) : isSettingKey dit si la cle (minuscules) existe, applySetting l'applique.
bool isSettingKey(const std::string& key);
// Reglages ecrits en commentaires `// cle=valeur` dans un shader (ils priment sur la fenetre), sous la forme « fps=30, width=3840 ».
// "" si le shader n'en a pas ou ne peut pas etre lu.
std::wstring shaderOverridesOf(const std::wstring& shaderPath);
void applySetting(Settings& s, const std::string& key, const std::string& value);
