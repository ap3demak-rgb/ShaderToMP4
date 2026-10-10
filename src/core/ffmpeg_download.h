// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Presence et telechargement automatique de ffmpeg (builds statiques : gyan.dev sous Windows, BtbN sous Linux, verifies par SHA-256).
#pragma once
#include "common.h"

bool ffmpegPresent(const std::wstring& exeDir);  // ffmpeg a cote du programme ou dans le PATH
bool ffmpegDownloadSupported();                  // faux sous macOS (pas de source avec empreinte verifiable)

// Telecharge, verifie et installe ffmpeg.exe dans exeDir. progress(pourcentage, message) ; cancel : arret demande (facultatif).
bool downloadFfmpeg(const std::wstring& exeDir, const std::function<void(int, const std::wstring&)>& progress,
                    const std::atomic<bool>* cancel, std::wstring& err);
