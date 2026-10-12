// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Texte incruste au debut de la video par le filtre drawtext de ffmpeg : titre au centre, credit en bas a gauche (par exemple l'auteur d'un
// shader importe de Shadertoy dont la licence demande l'attribution). Apparition et disparition en fondu.
#pragma once
#include "common.h"
#include "settings.h"

// Vrai si ce ffmpeg contient le filtre drawtext (cache).
bool ffmpegHasDrawtext(const std::wstring& ffmpeg);

// Filtre a placer dans la chaine -vf apres la mise a l'echelle, terminé par une virgule ; vide s'il n'y a rien a afficher.
// offset : instant (secondes de la video) de la premiere image de ce rendu (une partie reprenable commence apres 0).
// w, h : taille de la sortie. Faux + err si le texte est demande mais que ffmpeg n'a pas drawtext ou qu'aucune police n'est trouvee.
bool overlayFilter(const Settings& s, double offset, int w, int h, const std::wstring& ffmpeg, std::wstring& filter, std::wstring& err);

// Valeur de l'option `text` de drawtext, entre apostrophes pour l'analyseur d'options (\ et % proteges pour drawtext, apostrophes
// internes echappees, saut de ligne remplace par un espace).
std::wstring escapeDrawtext(const std::wstring& text);
