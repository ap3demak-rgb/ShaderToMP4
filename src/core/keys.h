// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Raccourcis clavier configurables (keys.ini a cote du programme) et recherche de la palette de commandes (Ctrl+K). Les actions sont decrites par
// la fenetre (identifiant, raccourci par defaut) ; ce module lit le fichier, refuse les conflits et filtre les noms d'actions.
#pragma once
#include "common.h"

struct Shortcut {
    bool ctrl = false, shift = false, alt = false;
    int vk = 0;  // code de touche virtuelle Windows (lettres et chiffres en majuscule ASCII)
    bool empty() const { return vk == 0; }
    bool operator==(const Shortcut& o) const { return ctrl == o.ctrl && shift == o.shift && alt == o.alt && vk == o.vk; }
};

struct KeyAction {
    std::string id;            // « project_save »
    std::string defaultKey;    // « Ctrl+S », "" = sans raccourci
};

struct KeyMap {
    std::vector<std::pair<std::string, Shortcut>> binding;  // une entree par action, dans l'ordre de la table
    const Shortcut* find(const std::string& actionId) const;
};

// « Ctrl+Maj+K », « F5 », « Echap »... faux si le texte n'est pas un raccourci ; une lettre ou un chiffre exige Ctrl ou Alt
bool parseShortcut(const std::string& text, Shortcut& out);
std::string shortcutText(const Shortcut& s);  // « Ctrl+Shift+K » (noms anglais, ceux qu'on ecrit dans keys.ini)

// Table par defaut, puis keys.ini (lignes « action=raccourci », « ; commentaire », « action= » pour retirer le raccourci). Un raccourci deja pris par une autre
// action, inconnu ou mal ecrit est refuse : l'ancien reste et la raison est ajoutee a `problems` (« line N: ... », en anglais technique).
KeyMap defaultKeys(const std::vector<KeyAction>& actions);
KeyMap parseKeys(const std::string& text, const std::vector<KeyAction>& actions, std::vector<std::string>& problems);
KeyMap loadKeys(const std::wstring& exeDir, const std::vector<KeyAction>& actions, std::vector<std::string>& problems);
// Les raccourcis qui se disputent la meme combinaison (vide si aucun conflit) : ids des actions concernees
std::vector<std::pair<std::string, std::string>> keyConflicts(const KeyMap& map);
// Modele de keys.ini : une ligne par action avec son raccourci actuel
std::string keysTemplate(const KeyMap& map);

// Palette : indices des noms qui contiennent tous les mots de la recherche (sans tenir compte de la casse ni des accents), les noms qui commencent par la
// recherche d'abord, puis par ordre de la liste
std::vector<int> filterNames(const std::vector<std::wstring>& names, const std::wstring& query);
// Minuscules sans accents (a, e, i, o, u, c, n pour les lettres latines accentuees)
std::wstring foldText(const std::wstring& s);
