// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Encodeurs ffmpeg (CPU ou materiel) et espaces colorimetriques.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

struct ColorSpec {
    const wchar_t* range;   // tv | pc
    const wchar_t* matrix;  // matrice RGB -> YUV
    const wchar_t* tags;    // etiquettes ffmpeg (espace, primaires, transfert)
};
ColorSpec colorSpec(const std::string& c);
// Espace colorimetrique de la sortie : celui du reglage `color`, ou BT.2020 avec transfert PQ (HDR10) ou HLG quand `hdr` est actif.
ColorSpec colorSpecFor(const Settings& s);
// Filtres ffmpeg de la sortie, chacun termine par une virgule (vides si inutiles) :
//  - mise a l'echelle `scale` avec le filtre `scalefilter` (de w x h, taille du rendu apres supersampling, vers ow x oh) ;
//  - interpolation d'images `interp` (minterpolate, 8 bits) ;
//  - normalisation du son `loudnorm` : mesure du son (EBU R128) puis gain FIXE (le volume ne monte ni ne descend en cours de route) ; sans mesure
//    exploitable, aucune normalisation (jamais le filtre loudnorm en une passe, dont le gain varie).
std::wstring outScaleFilter(const Settings& s, int w, int h, int ow, int oh);
std::wstring interpFilter(const Settings& s);

// Mesure du son (premiere passe de la normalisation).
struct LoudnormMeasure {
    bool ok = false;       // mesure lue et exploitable (son non silencieux)
    double inputI = 0;     // volume integre mesure (LUFS)
    double inputTP = 0;    // crete vraie mesuree (dBTP)
};
// Filtre de mesure a placer en fin de chaine, sans virgule finale (la commande ecrit la mesure dans son journal, a lire avec parseLoudnormMeasure).
std::wstring loudnormMeasureFilter(const Settings& s);
// Lit la mesure dans la sortie de ffmpeg (bloc JSON de loudnorm) ; m.ok vrai si elle est exploitable.
bool parseLoudnormMeasure(const std::string& text, LoudnormMeasure& m);
// Lance la commande de mesure (voir loudnormMeasureFilter) et lit son resultat.
// raw : sortie complete de ffmpeg (pour le journal en cas d'echec).
bool measureLoudnorm(const std::wstring& cmd, LoudnormMeasure& m, std::string* raw = nullptr);
// Gain fixe (dB) qui amene la mesure a la cible `loudnorm`. La crete vraie reste sous -1,5 dBTP : si le gain voulu la depasse, un limiteur de crete
// rattrape au plus 3 dB, au-dela le gain est reduit (le volume cible n'est alors pas atteint). limited : le limiteur est utilise.
double loudnormGainDb(const Settings& s, const LoudnormMeasure& m, bool& limited);
// Filtre de normalisation, termine par une virgule (vide si `loudnorm` est 0). Avec une mesure exploitable : gain fixe (+ limiteur de crete si
// besoin) ; sans mesure, vide.
std::wstring loudnormFilter(const Settings& s, const LoudnormMeasure* m = nullptr);

// Encodeurs materiels connus : "nvenc", "amf", "qsv" (Windows, Linux), "vaapi" (Linux), "videotoolbox" (macOS).
// Choisit l'encodeur reellement utilisable : "cpu" ou l'un d'eux (sonde ffmpeg, repli sur le CPU).
std::string resolveEncoder(const Settings& s, const std::wstring& ffmpeg, const Hooks& H);
// Arguments ffmpeg (-c:v ..., qualite, preset) pour l'encodeur resolu.
std::wstring encoderArgs(const Settings& s, const std::string& enc);
// Nom affiche d'un codec (H.264, VP9, ProRes HQ...).
const wchar_t* codecLabel(const std::string& codec);
// Codec audio du conteneur (-c:a ...) et options propres au conteneur (-movflags ...).
std::wstring audioCodecArgs(const std::string& container);
std::wstring containerArgs(const std::string& container);
// Fin de la chaine de filtres (format envoye a l'encodeur) et option -pix_fmt de sortie.
std::wstring encoderFormat(const std::string& enc);
std::wstring encoderPixFmt(const std::string& enc);
// Format de pixel envoye a l'encodeur selon le codec et la profondeur (yuv420p, yuv420p10le, yuv422p10le...), "" pour VAAPI.
std::wstring outPixFmt(const Settings& s, const std::string& enc);
std::wstring outFormat(const Settings& s, const std::string& enc);       // fin de la chaine de filtres
std::wstring outPixFmtArg(const Settings& s, const std::string& enc);    // option -pix_fmt
// Filtres et options ffmpeg pour une image animee ou une sequence (GIF, WebP, PNG, TIFF, EXR) ; deep : entree 16 bits.
// alphaMode : ffmpeg connait l'option alpha_mode de setparams (ffmpeg recent) ; sans elle, la couleur premultipliee serait redressee a l'ecriture
std::wstring imageFilterTail(const Settings& s, bool alphaModeFilter = true);
std::wstring imageEncoderArgs(const Settings& s);
