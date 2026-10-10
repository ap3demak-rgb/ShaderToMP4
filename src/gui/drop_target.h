// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Glisser-deposer de fichiers par OLE (IDropTarget) : permet d'illuminer la zone de depot pendant le survol.
#pragma once
#include "common.h"

bool registerDropTarget(HWND wnd);  // OleInitialize doit avoir ete appele
void revokeDropTarget(HWND wnd);
