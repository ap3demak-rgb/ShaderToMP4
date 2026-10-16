// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Publication, poids et traçabilité : cibles de publication (limites des plates-formes), taille cible d'un fichier, licence et crédit d'un shader,
// source embarquée dans la vidéo (vidéo reproductible), cache des fichiers maîtres, vérification d'intégrité, aperçu animé court.
// Les limites des plates-formes sont des données tenues à la main (`presets/targets.ini` les complète) : elles ne viennent d'aucun service en ligne.
#pragma once
#include "common.h"
#include "settings.h"

// --- Cibles de publication --------------------------------------------------------------------------------------------------------------
struct Target {
    std::string id;           // « tiktok », « discord-8mb »...
    std::wstring label;
    int width = 0, height = 0;   // 0 = pas de contrainte
    double fps = 0;
    std::string codec, container;
    double maxSeconds = 0;       // duree maximale (0 = aucune)
    double maxMB = 0;            // poids maximal en Mo de 1 000 000 octets (0 = aucun)
};
// Cibles integrees puis celles de <programme>/presets/targets.ini (une section [id] par cible : label, width, height, fps, codec, container,
// maxduration, maxsize) ; une cible du fichier remplace l'integree de meme id.
std::vector<Target> publishTargets(const std::wstring& exeDir);
bool parseTargets(const std::string& text, std::vector<Target>& out);
const Target* findTarget(const std::vector<Target>& all, const std::string& id);
// Applique les valeurs de la cible a `s` (taille, cadence, codec, conteneur, poids cible) sauf pour les cles de `skip` (imposees par le shader).
void applyTarget(Settings& s, const Target& t, const std::vector<std::string>& skip);
// Avertissements localises : duree au-dela de la limite, taille ou poids differents de la cible apres les reglages du shader
std::vector<std::wstring> checkTarget(const Target& t, const Settings& s, double seconds, int outW, int outH);

// Recadrage centre « 9:16 » : taille conservee dans une image de w x h (dimensions paires) ; faux si le texte n'est pas « L:H »
bool cropSize(const std::string& spec, int w, int h, int& cw, int& ch);

// --- Taille cible ------------------------------------------------------------------------------------------------------------------------------
// « 25 », « 25MB », « 800KB », « 1.5GB » -> Mo (1 Mo = 1 000 000 octets) ; faux si illisible
bool parseSizeMB(const std::string& text, double& mb);
struct BitratePlan {
    int videoKbps = 0, audioKbps = 0;
    double seconds = 0;
};
// Debit video qui tient dans `mb` Mo pour `seconds` secondes avec un son de `audioKbps` (marge de 4 % pour le conteneur) ; faux si le debit
// serait inferieur a 20 kb/s
bool planBitrate(double mb, double seconds, int audioKbps, BitratePlan& out);
// Debit corrige apres un essai qui a donne `actualBytes` octets pour un but de `targetBytes`
int correctedBitrate(int kbps, uint64_t actualBytes, uint64_t targetBytes);

// --- Licence et crédit -----------------------------------------------------------------------------------------------------------------------
struct LicenseInfo {
    bool found = false;           // une licence ou un auteur a ete reconnu
    std::string id;               // « CC-BY-NC-SA-3.0 », « MIT », « CC0 »...
    bool nonCommercial = false;   // l'usage commercial est exclu
    bool assumed = false;         // licence par defaut de Shadertoy, non ecrite dans le code
    std::wstring author, title;
};
// Lit l'en-tete du shader : « // Shadertoy: titre - auteur - adresse » (import) et les licences connues ecrites en commentaire
LicenseInfo detectLicense(const std::string& code);
std::wstring licenseText(const LicenseInfo& l);  // « CC BY-NC-SA 3.0 » ; defaut : l'identifiant

// --- Source embarquée (vidéo reproductible) ---------------------------------------------------------------------------------------------------
struct SourceBundle {
    std::string appVersion, shaderName;
    std::vector<std::pair<std::string, std::string>> files;  // nom de fichier -> contenu (shader, passes)
    std::string settingsIni;                                 // reglages utilises, au format de config.ini
};
std::string base64Encode(const std::string& bytes);
bool base64Decode(const std::string& text, std::string& bytes);
std::string bundleToJson(const SourceBundle& b);
bool bundleFromJson(const std::string& json, SourceBundle& b);
// Fichier de metadonnees ffmpeg (-f ffmetadata) portant le paquet dans la balise `shadertomp4` ; vrai si ecrit
bool writeBundleMetadata(const std::wstring& path, const SourceBundle& b, const std::vector<std::pair<std::string, std::string>>& extra = {});
// Lit le paquet d'un fichier video par ffmpeg (-f ffmetadata). Faux et message si la video n'en contient pas.
bool readBundleFromVideo(const std::wstring& ffmpeg, const std::wstring& video, SourceBundle& b, std::wstring& err);
// Le shader (et ses passes) en un paquet : les fichiers a cote du shader dont le nom commence par le sien (<nom>.glsl, .bufferA.glsl...)
SourceBundle makeBundle(const std::wstring& shaderPath, const std::string& settingsIni);

// --- Cache des maîtres ----------------------------------------------------------------------------------------------------------------------
std::wstring cacheDir(const std::wstring& exeDir);  // <programme>/cache
uint64_t cacheSize(const std::wstring& exeDir);     // octets des maitres
uint64_t cachePurge(const std::wstring& exeDir);    // supprime tout, rend les octets liberes
// Garde le cache sous maxBytes en supprimant les maitres les moins recemment utilises (jamais `keep`)
void cacheTrim(const std::wstring& exeDir, uint64_t maxBytes, const std::wstring& keep);
void cacheTouch(const std::wstring& master, const std::wstring& info);  // ecrit / met a jour la fiche <maitre>.txt (derniere utilisation)

// --- Vérification d'intégrité ------------------------------------------------------------------------------------------------------------------
struct VerifyResult {
    double seconds = 0, fps = 0;
    long long frames = 0;
    std::string sha256;
    bool hadReport = false;
    std::vector<std::wstring> problems;   // vide = tout concorde
    std::vector<std::wstring> checks;     // ce qui a ete controle, ligne par ligne
};
// Relit la video (duree, cadence, nombre d'images par decodage complet, somme SHA-256) et la compare au rapport <video sans extension>.report.json
// s'il existe (images, cadence, duree, taille, somme).
bool verifyVideo(const std::wstring& ffmpeg, const std::wstring& video, VerifyResult& res, std::wstring& err);

// --- Aperçu animé ------------------------------------------------------------------------------------------------------------------------------------
// GIF ou WebP de 3 secondes, 480 pixels de large, tire du milieu de la video par ffmpeg (aucun nouveau calcul GPU) ; faux si ffmpeg echoue
bool makeAnimPreview(const std::wstring& ffmpeg, const std::wstring& video, const std::wstring& dest, bool webp, double videoSeconds);
// Controle d'un fichier pour l'interface : lignes de compte rendu (ce qui a ete controle, puis les ecarts) ; `ok` est faux s'il y a un ecart ;
// faux et message dans `err` si la video ne peut pas etre relue
bool verifyLines(const std::wstring& ffmpeg, const std::wstring& video, std::vector<std::wstring>& lines, bool& ok, std::wstring& err);
