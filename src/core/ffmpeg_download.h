// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Presence et telechargement automatique de ffmpeg.exe (build "essentials" de gyan.dev, verifie par SHA-256).
#pragma once
#include "common.h"

bool ffmpegPresent(const std::wstring& exeDir);  // ffmpeg.exe a cote du programme ou dans le PATH

// Telecharge, verifie et installe ffmpeg.exe dans exeDir. progress(pourcentage, message) ; cancel : arret demande (facultatif).
bool downloadFfmpeg(const std::wstring& exeDir, const std::function<void(int, const std::wstring&)>& progress,
                    const std::atomic<bool>* cancel, std::wstring& err);
