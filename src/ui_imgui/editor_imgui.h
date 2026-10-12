// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Éditeur de shader de l'interface Linux / macOS : un onglet par fichier du shader (Image et ses passes), numéros de ligne, erreurs de
// compilation marquées. Pas de coloration syntaxique ici (le champ de texte de Dear ImGui ne colore pas).
#pragma once
#include "common.h"
#include "ui_imgui/model.h"

namespace ui {

void openEditor(Model& m);  // sur le shader sélectionné
bool editorOpen();
void drawEditor(Model& m);  // à appeler à chaque image, après les autres fenêtres
void editorNoteLog(const std::wstring& text);  // erreurs « fichier:ligne: message » du journal

}  // namespace ui
