// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fichier image, son ou video depose sur la fenetre : quel canal Shadertoy ecrire dans les commentaires du shader (`// iChannel0=...`).
#pragma once
#include "common.h"
#include "settings.h"

enum class DropKind { Shader, Image, Sound, Video, Other };

// D'apres l'extension : shaders (.glsl .txt .json .montage), images, sons, videos
DropKind classifyDropped(const std::wstring& path);

// Reglage iChannel<index> (0 a 3) pour ce fichier : « chemin » pour une image, « audio:chemin » pour un son, « video:chemin » pour une video.
// Le chemin est relatif au dossier du shader (separateur /) quand le fichier s'y trouve, sinon absolu.
Override channelOverride(const std::wstring& shaderPath, const std::wstring& file, DropKind kind, int index);
// Reglage `audio` (bande-son de la video) pour un fichier son
Override soundtrackOverride(const std::wstring& shaderPath, const std::wstring& file);
