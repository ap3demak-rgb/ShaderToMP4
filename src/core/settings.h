// Reglages de la video : config.ini (a cote du .exe), commentaires `// cle=valeur` des shaders, fenetre.
#pragma once
#include "common.h"

struct Settings {
    int width = 1920, height = 1080;
    double fps = 60.0, duration = 90.0, start = 0.0;
    int crf = 12;
    std::string preset = "slow";
    std::string codec = "h264";  // h264 | h265
    std::string container = "mp4";  // mp4 | mov | mkv
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
    double chunk = 30;              // rendu par parties de N secondes, reprenables (0 = jamais)
    std::string gpu = "auto";       // auto | high | low : preference Windows pour les PC a deux GPU
};

bool sameSettings(const Settings& a, const Settings& b);
void parseLines(Settings& s, const std::string& text, bool onlySlashComments);  // ini (false) ou commentaires // (true)
void writeIni(const std::wstring& path, const Settings& s);
