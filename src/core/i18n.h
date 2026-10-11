// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Internationalisation : tous les textes visibles viennent de fichiers JSON (lang/en.json, lang/fr.json).
// Les deux fichiers sont embarqués dans l'exe ; un fichier lang/<code>.json placé à côté de l'exe les complète ou les
// remplace, ce qui permet d'ajouter une langue sans recompiler. Repli clé par clé : langue choisie, puis anglais.
#pragma once
#include "common.h"

namespace i18n {

// Charge la langue (code "en", "fr"... ; "" = langue du système). exeDir : dossier de l'application (pour lang/).
bool init(const std::wstring& exeDir, const std::string& code);
bool setLanguage(const std::string& code);  // change de langue à chaud ; false si le code est inconnu
std::string language();                     // code actuellement utilisé
std::string systemLanguage();               // langue du système si un fichier de langue existe (en, fr, lang/<code>.json), sinon "en"
std::vector<std::string> available();       // codes disponibles : en, fr + lang/*.json

// Texte pour une clé (ex. "ui.btn.start"). Clé absente partout : renvoie la clé entre crochets et la mémorise.
std::wstring tr(const char* key);
std::string tr8(const char* key);  // même texte en UTF-8 (sortie console, fichiers)
// Texte formaté, arguments à la printf ; positions numérotées possibles (%1$d, %2$ls) quand l'ordre change selon la langue.
std::wstring trf(const char* key, ...);

// Nombre pour l'affichage (séparateur décimal de la langue), format %g. Les fichiers .ini gardent toujours le point.
std::wstring num(double v);
std::wstring fixed(double v, int decimals);  // nombre à décimales fixes, séparateur de la langue

std::vector<std::string> missingKeys();                  // clés demandées mais introuvables
std::vector<std::string> keys(const std::string& code);  // toutes les clés d'une langue (contrôle de parité)

}  // namespace i18n

// Raccourcis : TX("cle") renvoie le texte ; TXF("cle", args...) le formate (printf)
#define TX(key) i18n::tr(key)
#define TXF(key, ...) i18n::trf(key, __VA_ARGS__)
