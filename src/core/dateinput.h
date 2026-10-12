// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iDate simule : date de depart fixe (`// date=2026-03-14T15:09:26`) ou reelle, qui avance avec le temps du rendu.
#pragma once
#include "common.h"
#include "platform/platform.h"

struct DateInput {
    platform::DateTime base;  // date et heure a iTime = 0
    double rate = 1.0;        // secondes de date par seconde de rendu (1 = temps reel, 0 = figee, `+rate=` dans le reglage)
};

// Date du reglage `date` : « AAAA-MM-JJ[THH:MM[:SS]][+rate=R] » ; texte vide ou invalide = date reelle du PC (cadence 1).
DateInput dateInputFrom(const std::string& text);

// iDate pour le temps t : annee, mois (0-11), jour, secondes depuis minuit ; passe minuit, le jour (mois, annee) suit.
void dateAt(const DateInput& d, double t, float out[4]);
