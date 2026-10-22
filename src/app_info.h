// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Metadonnees uniques du projet : utilisees par le code (fenetre A propos, --version, titre), par app.rc
// (proprietes de l'exe) et par les scripts. Rester compatible avec le compilateur de ressources (macros de chaines seulement).
#pragma once

#define APP_NAME_STR "ShaderToMP4"
#define APP_VER_MAJOR 2
#define APP_VER_MINOR 1
#define APP_VER_PATCH 1
#define APP_VER_STR "2.1.1"
#define APP_COPYRIGHT_YEAR "2026"
#define APP_COPYRIGHT_OWNER "Patrick JAILLET"
#define APP_LICENSE_ID "GPL-3.0-or-later"
#define APP_CONTACT_EMAIL "sandefjord.development@proton.me"
#define APP_WEBSITE_URL "https://ap3demak-rgb.github.io/ShaderToMP4"
// Achat de l'edition Pro : page PayPal.me de l'auteur ; le montant choisi (5, 10 ou 15 euros) est ajoute sous la forme /5EUR (voir core/prolicense.h)
#define APP_PURCHASE_URL "https://www.paypal.me/sandefjorddevel"

#ifndef RC_INVOKED
#define APP_WIDEN2(x) L##x
#define APP_WIDEN(x) APP_WIDEN2(x)
#define APP_NAME_WSTR APP_WIDEN(APP_NAME_STR)
#define APP_VER_WSTR APP_WIDEN(APP_VER_STR)
#define APP_COPYRIGHT_YEAR_W APP_WIDEN(APP_COPYRIGHT_YEAR)
#define APP_COPYRIGHT_OWNER_WSTR APP_WIDEN(APP_COPYRIGHT_OWNER)
#define APP_LICENSE_ID_WSTR APP_WIDEN(APP_LICENSE_ID)
#define APP_CONTACT_EMAIL_WSTR APP_WIDEN(APP_CONTACT_EMAIL)
#define APP_WEBSITE_URL_WSTR APP_WIDEN(APP_WEBSITE_URL)
#define APP_PURCHASE_URL_WSTR APP_WIDEN(APP_PURCHASE_URL)
#endif
