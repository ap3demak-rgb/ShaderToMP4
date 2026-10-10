// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Sélecteur de fichiers et de dossiers intégré (aucune boîte de dialogue système n'est disponible sans bibliothèque supplémentaire).
#pragma once
#include "common.h"

namespace ui {

enum class BrowseMode { OpenFiles, OpenFile, PickFolder, SaveFile };

class FileBrowser {
public:
    // extensions : minuscules, avec le point (« .glsl ») ; vide = tous les fichiers. defaultName : nom proposé (SaveFile).
    void open(BrowseMode mode, const std::string& titleKey, const std::wstring& startDir, const std::vector<std::wstring>& extensions,
              const std::wstring& defaultName, std::function<void(const std::vector<std::wstring>&)> onDone);
    void draw();  // à appeler à chaque image ; gère sa propre fenêtre modale

private:
    void refresh();
    bool matches(const std::wstring& name) const;

    BrowseMode mode_ = BrowseMode::OpenFiles;
    std::string titleKey_;
    std::wstring dir_;
    std::vector<std::wstring> exts_;
    std::vector<std::wstring> dirs_, files_;
    std::vector<std::wstring> picked_;
    std::function<void(const std::vector<std::wstring>&)> onDone_;
    char name_[256] = "";
    bool openRequested_ = false;
    bool showHidden_ = false;
};

}  // namespace ui
