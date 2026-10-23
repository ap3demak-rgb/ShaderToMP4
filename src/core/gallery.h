// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Galerie d'exemples livree : examples/gallery/<categorie>/<nom>.glsl avec une vignette <nom>.bmp (128x72) et, dans l'en-tete du shader,
// deux lignes « // fr: Titre — explication » et « // en: Title — explanation » (les autres langues retombent sur l'anglais).
#pragma once
#include "common.h"

struct GalleryItem {
    std::wstring category;  // nom du dossier : maths, bruit, fractales, raymarching, 2d, post, boucles, son, vr, passes
    std::wstring name;      // nom du shader sans extension
    std::wstring path;      // chemin complet du .glsl
    std::wstring thumb;     // chemin de la vignette (vide si absente)
    std::wstring title[2], text[2];  // [0] = anglais, [1] = francais
};

// Dossier de la galerie (a cote du programme, sinon dans le dossier parent) ; vide si introuvable
std::wstring galleryDir(const std::wstring& exeDir);

// Tous les exemples, classes par categorie (ordre fixe de galleryCategories) puis par nom
std::vector<GalleryItem> listGallery(const std::wstring& exeDir);
const std::vector<std::wstring>& galleryCategories();

// Titre et explication dans la langue donnee (« fr » ou autre : anglais)
std::wstring galleryTitle(const GalleryItem& it, const std::string& lang);
std::wstring galleryText(const GalleryItem& it, const std::string& lang);
