// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Formats de sortie : codecs, conteneurs, disponibilite dans ffmpeg, estimation de taille, nom du fichier de sortie.
#pragma once
#include "common.h"
#include "settings.h"

struct FormatInfo {
    const char* id;       // valeur de config.ini
    const wchar_t* label; // nom technique affiche dans les listes (non traduit)
};

// Codecs : h264, h265, vp9, av1, prores_*, dnxhr_*. Conteneurs : mp4, mov, mkv, webm, gif, webp, png, tiff, exr.
int codecCount();
const FormatInfo& codecAt(int i);
int codecIndex(const std::string& id);      // 0 si inconnu
int containerCount();
const FormatInfo& containerAt(int i);
int containerIndex(const std::string& id);  // 0 si inconnu
std::string normalizeCodec(const std::string& v);      // alias (hevc, x265, prores, dnxhr...) -> identifiant du tableau
std::string normalizeContainer(const std::string& v);

// Nature de la sortie : video (mp4, mov, mkv, webm), image animee (gif, webp) ou sequence d'images (png, tiff, exr).
enum class OutKind { Video, Gif, Webp, Sequence };
OutKind outKind(const std::string& container);
bool isProRes(const std::string& codec);
bool isDnxhr(const std::string& codec);
// WebM n'accepte que VP9/AV1 ; MOV refuse VP9/AV1 ; MP4 refuse ProRes/DNxHR. Sans objet pour les images (codec ignore).
bool codecContainerOk(const std::string& codec, const std::string& container);

// Taille de la sortie apres la mise a l'echelle `scale` (« 1920x1080 », « 2 » ou « 2x » = facteur) : dimensions paires, 16 a 16384.
// Sans reglage, la taille du rendu. Faux si le reglage est invalide.
bool outputSize(const Settings& s, int& w, int& h);
// Transparence possible pour ce couple : ProRes 4444 (MOV, MKV), VP9 (WebM, MKV), WebP et sequences PNG / TIFF / EXR.
bool alphaSupported(const std::string& codec, const std::string& container);
// HDR10 / HLG possible : H.265, AV1 ou VP9 dans un conteneur video qui les accepte.
bool hdrSupported(const std::string& codec, const std::string& container);

// Chemin de ffmpeg : a cote du programme, sinon dans le PATH, sinon le nom seul.
std::wstring findFfmpeg(const std::wstring& exeDir);
// Encodeurs logiciels demandes par ce couple codec / conteneur (le premier present est utilise).
std::vector<std::string> requiredEncoders(const std::string& codec, const std::string& container);
// false si ffmpeg est present mais ne contient aucun des encodeurs requis (vrai si ffmpeg est introuvable : inconnu).
bool formatAvailable(const std::wstring& ffmpeg, const std::string& codec, const std::string& container);
bool codecAvailable(const std::wstring& ffmpeg, const std::string& codec);
bool containerAvailable(const std::wstring& ffmpeg, const std::string& container);
void forgetFfmpegCapabilities();  // apres un telechargement de ffmpeg

// Taille estimee du fichier (octets) pour une duree donnee (secondes), son compris si `audio`.
double estimateSizeBytes(const Settings& s, double seconds, bool audio);
// Duree reellement rendue (boucle parfaite comprise), en secondes.
double renderedSeconds(const Settings& s);
// Texte « environ 85 Mo » de la taille estimee ; ajoute un avertissement si le volume de `dir` (vide : ignore) manque de place.
std::wstring estimateText(const Settings& s, const std::wstring& dir);

// Fichier de sortie. Modele `name` ({shader} {w} {h} {fps} {date} {codec}), regle `exists` (overwrite | number | skip).
// forWrite : choisit le nom a ecrire (numerote si besoin) ; sinon renvoie le dernier fichier existant (ou le nom de base).
// Pour une sequence d'images : la premiere image du dossier `<nom>_frames`. skip : mis a true si le fichier existe et exists=skip.
std::wstring outputFile(const std::wstring& shaderPath, const Settings& s, bool forWrite, bool* skip = nullptr);
// Pour une sequence : dossier et modele de nom (`<dossier>/<nom>_%06d.<ext>`) deduits de la premiere image.
std::wstring sequenceDir(const std::wstring& firstFrame);
std::wstring sequencePattern(const std::wstring& firstFrame);
