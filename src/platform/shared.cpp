// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Couche plateforme : code commun à tous les systèmes.
#include "platform/platform.h"

namespace platform {

std::vector<std::wstring> splitCommand(const std::wstring& command) {
    std::vector<std::wstring> r;
    std::wstring cur;
    bool inQuote = false, have = false;
    for (wchar_t c : command) {
        if (c == L'"') {
            inQuote = !inQuote;
            have = true;
        } else if ((c == L' ' || c == L'\t') && !inQuote) {
            if (have) r.push_back(cur);
            cur.clear();
            have = false;
        } else {
            cur += c;
            have = true;
        }
    }
    if (have) r.push_back(cur);
    return r;
}

}  // namespace platform
