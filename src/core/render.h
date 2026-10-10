// Moteur de rendu : shader Shadertoy -> images -> ffmpeg (video), image PNG ou apercu. Independant de l'interface.
#pragma once
#include "common.h"
#include "hooks.h"
#include "settings.h"

// Convertit shaderPath selon les reglages `base` (completes par les commentaires // du shader).
// Un contexte OpenGL doit etre courant dans le thread appelant. exeDir : dossier contenant ffmpeg.exe.
Result convertFile(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& base, const Hooks& H,
                   std::wstring& err);
