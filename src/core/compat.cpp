// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "compat.h"
#include "i18n.h"
#include "util.h"

namespace {

// Code sans commentaires (// et /* */), meme longueur de lignes : les mots cites en commentaire ne comptent pas
std::string stripComments(const std::string& c) {
    std::string out = c;
    for (size_t i = 0; i < out.size();) {
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*') {
            out[i] = out[i + 1] = ' ';
            i += 2;
            while (i < out.size() && !(out[i] == '*' && i + 1 < out.size() && out[i + 1] == '/')) {
                if (out[i] != '\n') out[i] = ' ';
                i++;
            }
            if (i < out.size()) out[i++] = ' ';
            if (i < out.size()) out[i++] = ' ';
        } else {
            i++;
        }
    }
    return out;
}

bool isWordChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

bool usesWord(const std::string& code, const std::string& word) {
    for (size_t p = code.find(word); p != std::string::npos; p = code.find(word, p + 1)) {
        const bool before = p == 0 || !isWordChar(code[p - 1]);
        const bool after = p + word.size() >= code.size() || !isWordChar(code[p + word.size()]);
        if (before && after) return true;
    }
    return false;
}

}  // namespace

bool codeUsesWord(const std::string& code, const std::string& word) { return usesWord(stripComments(code), word); }

std::string adaptShaderCode(const std::string& code, std::vector<std::wstring>& notes) {
    std::string out;
    out.reserve(code.size());
    size_t pos = 0;
    bool version = false, extension = false;
    while (pos <= code.size()) {
        size_t e = code.find('\n', pos);
        if (e == std::string::npos) e = code.size();
        const std::string line = code.substr(pos, e - pos);
        const std::string t = trim(line);
        if (t.compare(0, 8, "#version") == 0) {
            version = true;
        } else if (t.compare(0, 10, "#extension") == 0) {
            extension = true;
        } else {
            out += line;
        }
        if (e < code.size()) out += '\n';
        pos = e + 1;
    }
    if (version) notes.push_back(TX("log.compat_version"));
    if (extension) notes.push_back(TX("log.compat_extension"));
    return out;
}

void checkCompatibility(const std::string& allCode, const Settings& s, std::vector<std::wstring>& notes) {
    const std::string c = stripComments(allCode);
    if (usesWord(c, "mainSound")) notes.push_back(TX("log.compat_sound"));
    bool hasCube = false, hasVolume = false, hasTimed = false;
    for (int i = 0; i < 4; i++) {
        const std::wstring& ch = s.channel[i];
        hasTimed = hasTimed || ch == L"audio" || ch.compare(0, 6, L"audio:") == 0 || ch.compare(0, 6, L"video:") == 0;
        if (ch == L"keyboard" && s.keys.empty()) notes.push_back(TXF("log.compat_keyboard", i));
        hasCube = hasCube || s.channel[i].compare(0, 4, L"cube") == 0;
        hasVolume = hasVolume || s.channel[i].compare(0, 7, L"volume:") == 0;
    }
    if (usesWord(c, "samplerCube") && !hasCube) notes.push_back(TX("log.compat_cubemap"));
    if (usesWord(c, "sampler3D") && !hasVolume) notes.push_back(TX("log.compat_volume"));
    if (usesWord(c, "iMouse") && s.mouseKeys.empty()) notes.push_back(TX("log.compat_mouse"));
    if (usesWord(c, "iDate") && s.date.empty()) notes.push_back(TX("log.compat_date"));
    if (usesWord(c, "iChannelTime") && !hasTimed) notes.push_back(TX("log.compat_channel_time"));
}
