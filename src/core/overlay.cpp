// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "overlay.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"

bool ffmpegHasDrawtext(const std::wstring& ffmpeg) {
    static std::mutex m;
    static std::map<std::wstring, bool> cache;
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(ffmpeg);
    if (it != cache.end()) return it->second;
    bool found = false;
    const int rc = runProcess(L"\"" + ffmpeg + L"\" -hide_banner -filters", [&](const std::wstring& line) {
        if (line.find(L" drawtext ") != std::wstring::npos) found = true;
    });
    if (rc == 0) cache[ffmpeg] = found;  // pas de cache si ffmpeg est absent : il peut arriver ensuite
    return found;
}

std::wstring escapeDrawtext(const std::wstring& text) {
    // niveau drawtext : « \ » protege le caractere suivant, « %{ » commence une expansion
    std::wstring t;
    for (wchar_t c : text) {
        if (c == L'\\') t += L"\\\\";
        else if (c == L'%') t += L"\\%";
        else if (c == L'"') t += L'\'';  // le guillemet double couperait la commande
        else if (c == L'\n' || c == L'\r') t += L' ';
        else t += c;
    }
    // niveau analyseur d'options (« cle=valeur:cle=valeur ») : entre apostrophes ; une apostrophe ferme, s'echappe, rouvre
    std::wstring r = L"'";
    for (wchar_t c : t) r += c == L'\'' ? std::wstring(L"'\\''") : std::wstring(1, c);
    return r + L"'";
}

// Le descripteur de filtre est lu deux fois par ffmpeg (graphe, puis options du filtre) : une valeur d'option deja prete pour le second
// niveau doit encore proteger \ ' , ; [ ] pour passer le premier (« les deux-points d'un chemin Windows » = \\: au final).
static std::wstring forGraph(const std::wstring& v) {
    std::wstring r;
    for (wchar_t c : v) {
        if (wcschr(L"\\',;[]", c)) r += L'\\';
        r += c;
    }
    return r;
}

// « (t+OFF) » : le temps de la video, meme pour une partie qui commence apres 0
static std::wstring videoTime(double offset) { return wfmt(L"(t+%.6f)", offset); }

bool overlayFilter(const Settings& s, double offset, int w, int h, const std::wstring& ffmpeg, std::wstring& filter, std::wstring& err) {
    filter.clear();
    if (s.title.empty() && s.credit.empty()) return true;
    const double dur = std::max(0.5, s.titleDur);
    const std::wstring font = platform::systemFontFile();
    if (!ffmpegHasDrawtext(ffmpeg)) {
        err = TX("error.overlay_ffmpeg");
        return false;
    }
    if (font.empty()) {
        err = TX("error.overlay_font");
        return false;
    }
    if (offset >= dur) return true;  // deja fini : rien a dessiner dans cette partie
    std::wstring fontPath;  // chemin vu par l'analyseur d'options : barres obliques, « : » protege
    for (wchar_t c : font) {
        if (c == L'\\') c = L'/';
        if (c == L':') fontPath += L'\\';
        fontPath += c;
    }
    const double fade = std::min(0.5, dur / 3.0);
    const std::wstring T = videoTime(offset);
    const std::wstring alpha = wfmt(L"if(lt(%ls,%g),%ls/%g,if(gt(%ls,%g),(%g-%ls)/%g,1))", T.c_str(), fade, T.c_str(), fade, T.c_str(), dur - fade,
                                    dur, T.c_str(), fade);
    const std::wstring enable = wfmt(L"lt(%ls,%g)", T.c_str(), dur);
    auto item = [&](const std::wstring& full, double sizeFrac, const std::wstring& pos) {
        const std::wstring text = full.substr(0, 150);  // une ligne : la commande formatee a une taille limitee
        int size = std::max(10, (int)std::lround(h * sizeFrac));
        // texte long : on reduit la police pour qu'il tienne dans 90 % de la largeur (largeur moyenne d'un caractere ~ 0.55 fois la police)
        const double estimated = 0.55 * size * (double)text.size();
        if (estimated > 0.9 * w) size = std::max(10, (int)(0.9 * w / (0.55 * (double)text.size())));
        return L"drawtext=fontfile=" + forGraph(fontPath) + L":text=" + forGraph(escapeDrawtext(text)) +
               wfmt(L":fontsize=%d:fontcolor=white:shadowx=%d:shadowy=%d:shadowcolor=black@0.6:", size, std::max(1, size / 18), std::max(1, size / 18)) +
               forGraph(pos) + L":alpha=" + forGraph(alpha) + L":enable=" + forGraph(enable) + L",";
    };
    if (!s.title.empty()) filter += item(s.title, 0.085, L"x=(w-text_w)/2:y=(h-text_h)/2");
    if (!s.credit.empty()) filter += item(s.credit, 0.032, wfmt(L"x=%d:y=h-text_h-%d", (int)std::lround(w * 0.03), (int)std::lround(h * 0.04)));
    return true;
}
