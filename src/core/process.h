// Lancement d'un processus sans fenetre en recuperant sa sortie (sondes ffmpeg, assemblage, extraction).
#pragma once
#include "common.h"

// Execute cmd, envoie chaque ligne de sortie (stdout+stderr) a onLine (peut etre vide), renvoie le code de sortie (-1 : lancement impossible).
int runProcess(const std::wstring& cmd, const std::function<void(const std::wstring&)>& onLine);
