// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Verification des mises a jour, UNIQUEMENT sur demande (bouton de la fenetre « A propos » ou --check-update) : jamais en arriere-plan, jamais au
// demarrage. Une requete GET lit la description de la derniere version publiee sur GitHub (le programme n'envoie rien d'autre que cette requete :
// son nom et sa version dans l'en-tete User-Agent) ; le telechargement est controle par la somme SHA-256 publiee avec la version, et rien n'est
// installe : le fichier verifie est range dans le dossier updates/ a cote du programme et l'utilisateur le lance lui-meme.
#pragma once
#include "common.h"

struct UpdateInfo {
    bool ok = false;
    std::wstring err;
    std::string current, latest;  // « 2.1.1 », « 2.2.0 »
    bool newer = false;
    std::wstring assetName, assetUrl;  // installeur (sinon archive) de la derniere version
    std::wstring sumsUrl;              // SHA256SUMS.txt de la version
    std::string sha256;                // somme attendue de l'asset, vide si elle n'est pas publiee
    std::wstring pageUrl;              // page de la version
};

// Adresse de l'API des versions du depot officiel
const wchar_t* defaultUpdateUrl();

// -1, 0, 1 : comparaison de deux numeros de version « 2.1.1 » (un « v » initial est ignore)
int compareVersions(const std::string& a, const std::string& b);

// Lit la reponse JSON de l'API (tag_name, html_url, assets[].name / browser_download_url) ; faux + out.err si elle n'a pas cette forme
bool parseRelease(const std::string& json, const std::string& current, UpdateInfo& out);

// Ligne « <somme>  <nom> » de SHA256SUMS.txt pour l'asset ; vide si absente
std::string findChecksum(const std::string& sumsText, const std::wstring& assetName);

// Une adresse n'est suivie que si elle est en https (ou en http vers la boucle locale, pour les essais avec un serveur local)
bool updateUrlAllowed(const std::wstring& url);

// Requete(s) reseau : description de la version, puis somme de l'asset
UpdateInfo checkForUpdate(const std::wstring& apiUrl);

// Telecharge l'asset dans `dir` (cree au besoin), controle sa somme SHA-256 et ne garde le fichier que s'il correspond ; progress(octets, total) rend faux pour arreter
bool downloadUpdate(const UpdateInfo& info, const std::wstring& dir, const std::function<bool(unsigned long long, unsigned long long)>& progress,
                    std::wstring& savedPath, std::wstring& err);
