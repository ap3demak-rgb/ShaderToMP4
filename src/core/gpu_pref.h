// Preference GPU de Windows pour ce programme (PC a deux cartes graphiques).
#pragma once
#include "common.h"

// gpu = "high" | "low" | "auto". Renvoie true si la preference vient de changer : elle n'est prise en compte
// qu'au lancement suivant (l'appelant relance alors le programme). "auto" ne touche pas aux reglages de Windows.
bool applyGpuPreference(const std::string& gpu, const std::wstring& exe);
