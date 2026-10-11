// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Reglages de la video : config.ini (a cote du .exe), commentaires `// cle=valeur` des shaders, fenetre.
#pragma once
#include "common.h"

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
    std::string endaction = "none"; // a la fin d'un lot reussi : none | folder | sleep (config.ini seulement)
    bool metadata = true;           // titre / commentaire ecrits dans le conteneur
    std::wstring channel[4];        // iChannel0-3 : chemin d'image, "noise" ou "grid" ("" = aucun)
    std::string chFilter[4];        // nearest | linear | mipmap ("" : mipmap pour une image, linear pour un tampon)
    std::string chWrap[4];          // clamp | repeat ("" : repeat pour une image, clamp pour un tampon)
    bool chVflip[4] = {true, true, true, true};                          // comme Shadertoy : haut de l'image en v = 1
    bool mouseSet = false;          // iMouse fixe : position en fraction de la taille (0,0 = bas gauche)
    double mouseX = 0, mouseY = 0;
    bool mouseDown = false;         // bouton considere comme enfonce
    std::string date;               // iDate fixe "AAAA-MM-JJ[THH:MM[:SS]]" ("" = date reelle)
    double chunk = 30;              // rendu par parties de N secondes, reprenables (0 = jamais)
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
