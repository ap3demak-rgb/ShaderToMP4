// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Licence de l'edition Pro (shareware) : fichier signe ShaderToMP4.license pose a cote de l'executable, verifie hors ligne (Ed25519, cle publique dans
// le programme), jamais de reseau, d'horloge d'essai ni de saisie. Sans licence valide, une sortie qui utilise une fonction Pro porte un filigrane ;
// avec une licence valide, tout l'habillage d'essai (filigrane, bouton d'achat, mentions « sans licence ») disparait.
#pragma once
#include "common.h"

enum class ProLicenseState {
    None,        // pas de fichier de licence
    Valid,       // signature exacte, edition Pro, version majeure couverte, non expiree
    Expired,     // signature exacte mais date d'expiration passee
    Invalid,     // fichier falsifie, tronque, mal forme ou d'une autre edition
    OtherMajor,  // signature exacte mais licence d'une autre version majeure
};

struct ProLicense {
    ProLicenseState state = ProLicenseState::None;
    std::string holder, edition, id, expires;  // « expires » : AAAA-MM-JJ ou « never »
    int major = 0;
};

// Nom du fichier attendu a cote de l'executable.
const wchar_t* licenseFileName();

// Verification d'un texte de licence (fonction pure, utilisee par l'auto-test). pub : cle publique Ed25519 (32 octets) ; appMajor : version majeure du
// programme ; todayDays : jours depuis le 1970-01-01 (UTC), -1 = date du jour.
ProLicense checkLicenseText(const std::string& text, const uint8_t pub[32], int appMajor, long long todayDays = -1);
long long daysFromCivil(int y, int m, int d);  // jours depuis 1970-01-01

// Cle publique du programme (la cle secrete n'est jamais dans un depot : voir le generateur prive).
void licensePublicKey(uint8_t out[32]);

// Lit <dossier>/ShaderToMP4.license avec la cle du programme. Appele au demarrage (relu a chaque demarrage seulement) ; sans appel, la premiere
// lecture a lieu au premier acces. Retourne la licence courante.
ProLicense loadLicense(const std::wstring& dir, const uint8_t* pub = nullptr);  // pub : autre cle (auto-test), nullptr = cle du programme
void initLicense(const std::wstring& exeDir);
void ensureLicense(const std::wstring& exeDir);  // initLicense si rien n'a encore ete lu (rendu lance sans passer par le demarrage)
ProLicense currentLicense();  // etat courant (charge au besoin depuis le dossier passe a initLicense)
void setLicenseForTest(const ProLicense& li);  // auto-test seulement : impose l'etat courant (initLicense le remet)
bool isRegistered();           // licence valide : version enregistree, plus aucun habillage d'essai
bool showTrialUi();            // contraire : bouton d'achat, etat « version d'essai », filigrane possible

// Texte « version d'essai / enregistree / expiree... » de l'etat, pour la fenetre A propos et --license-info (langue de l'interface).
std::wstring licenseStatusText(const ProLicense& li);
std::wstring licenseInfoText(const ProLicense& li, const std::wstring& exeDir);  // texte complet de --license-info

// Achat : montants proposes (euros) et adresse PayPal.me ouverte sur clic de l'utilisateur ; un montant hors liste donne le premier.
const std::vector<int>& purchaseAmounts();
std::wstring purchaseUrl(int euros);

// --- Fonctions Pro --------------------------------------------------------------------------------------------------------------------------
// Un rendu commence par proBegin() ; chaque fonction Pro appelle proCheck("nom") a son point d'usage : vrai si la licence est valide, sinon la
// fonction est notee. Apres la preparation du rendu, watermarkRequired() dit si la sortie doit porter le filigrane (sans licence valide ET au moins
// une fonction Pro utilisee). Une sortie sans fonction Pro n'en porte jamais. Etat propre a chaque thread de rendu.
void proBegin();
bool proCheck(const char* feature);
std::vector<std::string> proUsed();
bool watermarkRequired();
