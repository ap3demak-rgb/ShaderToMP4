// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include "common.h"

// Donne a un controle son nom accessible (cle de langue, relue a chaque demande : suit le changement de langue).
// langKey doit rester valide (litteral).
void setAccessibleName(HWND control, const char* langKey);
