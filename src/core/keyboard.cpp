// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "keyboard.h"
#include "util.h"

static int keyCodeOf(const std::string& nameIn) {
    const std::string n = lower(trim(nameIn));
    if (n.empty()) return -1;
    static const struct { const char* name; int code; } kNames[] = {
        {"backspace", 8}, {"tab", 9}, {"enter", 13}, {"shift", 16}, {"ctrl", 17}, {"alt", 18}, {"esc", 27}, {"space", 32},
        {"left", 37}, {"up", 38}, {"right", 39}, {"down", 40}};
    for (const auto& k : kNames)
        if (n == k.name) return k.code;
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return n[0] - 'a' + 65;
    char* end = nullptr;
    const long v = strtol(n.c_str(), &end, 10);
    if (*end != 0 || v < 0 || v > 255) return -1;
    return (int)v;  // un nombre est un code de touche : la touche « 5 » est le code 53
}

bool parseKeyPresses(const std::string& text, std::vector<KeyPress>& out) {
    std::vector<KeyPress> add;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(';', pos);
        if (e == std::string::npos) e = text.size();
        const std::string part = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (part.empty()) continue;
        const size_t colon = part.find(':');
        if (colon == std::string::npos) return false;
        KeyPress k;
        k.code = keyCodeOf(part.substr(0, colon));
        if (k.code < 0) return false;
        const std::string range = trim(part.substr(colon + 1));
        const size_t dash = range.find('-');
        if (dash == std::string::npos) return false;
        char* end = nullptr;
        const std::string a = trim(range.substr(0, dash)), b = trim(range.substr(dash + 1));
        k.from = strtod(a.c_str(), &end);
        if (a.empty() || *end != 0) return false;
        k.to = strtod(b.c_str(), &end);
        if (b.empty() || *end != 0 || k.from < 0 || k.to <= k.from) return false;
        add.push_back(k);
    }
    if (add.empty()) return false;
    out.insert(out.end(), add.begin(), add.end());
    return true;
}

std::string formatKeyPresses(const std::vector<KeyPress>& keys) {
    std::string r;
    char b[64];
    for (size_t i = 0; i < keys.size(); i++) {
        snprintf(b, sizeof(b), "%s%d:%g-%g", i ? " ; " : "", keys[i].code, keys[i].from, keys[i].to);
        r += b;
    }
    return r;
}

void keyboardState(const std::vector<KeyPress>& keys, double t, double dt, uint8_t out[kKeyboardW * kKeyboardH]) {
    memset(out, 0, kKeyboardW * kKeyboardH);
    // appuis par touche, tries et fusionnes quand ils se recouvrent : un appui par intervalle fusionne
    std::vector<KeyPress> sorted = keys;
    std::sort(sorted.begin(), sorted.end(), [](const KeyPress& a, const KeyPress& b) { return a.code != b.code ? a.code < b.code : a.from < b.from; });
    std::vector<KeyPress> merged;
    for (const KeyPress& k : sorted) {
        if (!merged.empty() && merged.back().code == k.code && k.from <= merged.back().to) merged.back().to = std::max(merged.back().to, k.to);
        else merged.push_back(k);
    }
    for (const KeyPress& k : merged) {
        if (k.code < 0 || k.code >= kKeyboardW) continue;
        if (t >= k.from && t < k.to) out[k.code] = 255;
        if (k.from <= t && t - k.from < dt * 0.999) out[kKeyboardW + k.code] = 255;
        if (k.from <= t) out[2 * kKeyboardW + k.code] ^= 255;
    }
}
