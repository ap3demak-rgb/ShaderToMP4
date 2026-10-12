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
//  - normalisation du son `loudnorm` (EBU R128, une passe).
std::wstring outScaleFilter(const Settings& s, int w, int h, int ow, int oh);
std::wstring interpFilter(const Settings& s);
std::wstring loudnormFilter(const Settings& s);

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
std::wstring imageFilterTail(const Settings& s);
std::wstring imageEncoderArgs(const Settings& s);
