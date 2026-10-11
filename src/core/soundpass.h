// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Passe « Sound » de Shadertoy : `vec2 mainSound(int samp, float time)` rendue par le GPU, 512x512 echantillons stereo par bloc,
// puis ecrite en WAV 16 bits (44,1 kHz) que ffmpeg ajoute a la video comme piste audio.
#pragma once
#include "common.h"
#include "hooks.h"

constexpr int kSoundRate = 44100;

enum class SoundResult { Ok, Failed, Cancelled };

// code : passe complete (Common devant). path / lines / commonPath servent a situer les erreurs « fichier:ligne ».
// Rend `seconds` secondes a partir de l'instant `start` du shader (comme la video) dans wavPath. Contexte OpenGL requis.
SoundResult renderSoundWav(const std::string& code, const std::wstring& path, int lines, const std::wstring& commonPath, double start,
                           double seconds, const std::wstring& wavPath, const Hooks& H, std::wstring& err);
