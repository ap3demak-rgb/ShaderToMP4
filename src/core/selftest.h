// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Auto-test : ShaderToMP4.exe --selftest [--update] [--quiet]
#pragma once
#include "common.h"

// Rend chaque tests\*.glsl en 64x64 a t = 1 s et le compare a tests\<nom>.ref. Renvoie 0 si tout passe.
// Le dossier tests est cherche a cote du .exe, puis dans le dossier parent (exe dans bin\).
int runSelfTest(const std::wstring& exeDir, bool update, bool quiet);
