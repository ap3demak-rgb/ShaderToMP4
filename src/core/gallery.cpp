// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "gallery.h"
#include "platform/platform.h"
#include "util.h"
#include <algorithm>

namespace {

const wchar_t kDash[] = L" — ";  // tiret cadratin entre le titre et l'explication

// « // fr: Titre — explication » : renvoie vrai si la ligne commence par la marque
bool takeLine(const std::string& line, const char* mark, std::wstring& title, std::wstring& text) {
    const std::string prefix = std::string("// ") + mark + ": ";
    if (line.compare(0, prefix.size(), prefix) != 0) return false;
    const std::wstring body = W(line.substr(prefix.size()));
    const size_t cut = body.find(kDash);
    if (cut == std::wstring::npos) {
        title = body;
        text.clear();
    } else {
        title = body.substr(0, cut);
        text = body.substr(cut + wcslen(kDash));
    }
    while (!text.empty() && (text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    return true;
}

bool endsWith(const std::wstring& s, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

}  // namespace

const std::vector<std::wstring>& galleryCategories() {
    static const std::vector<std::wstring> c = {L"maths", L"bruit", L"fractales", L"raymarching", L"2d", L"post", L"boucles", L"son", L"vr", L"passes"};
    return c;
}

std::wstring galleryDir(const std::wstring& exeDir) {
    for (const wchar_t* rel : {L"examples/gallery", L"../examples/gallery"}) {
        const std::wstring d = exeDir + rel;
        if (platform::dirExists(d)) return d;
    }
    return L"";
}

std::vector<GalleryItem> listGallery(const std::wstring& exeDir) {
    std::vector<GalleryItem> all;
    const std::wstring root = galleryDir(exeDir);
    if (root.empty()) return all;
    for (const std::wstring& cat : galleryCategories()) {
        std::vector<platform::DirEntry> files;
        const std::wstring dir = root + L"/" + cat;
        if (!platform::listDir(dir, files)) continue;
        std::vector<std::wstring> names;
        for (const auto& f : files)
            if (!f.isDir && endsWith(f.name, L".glsl") && f.name.find(L'.') == f.name.size() - 5) names.push_back(f.name.substr(0, f.name.size() - 5));  // les passes (x.bufferA.glsl) n'ont pas leur ligne
        std::sort(names.begin(), names.end());
        for (const std::wstring& n : names) {
            GalleryItem it;
            it.category = cat;
            it.name = n;
            it.path = dir + L"/" + n + L".glsl";
            const std::wstring bmp = dir + L"/" + n + L".bmp";
            if (platform::fileExists(bmp)) it.thumb = bmp;
            std::string src;
            if (readFile(it.path, src)) {
                size_t pos = 0;
                for (int guard = 0; guard < 12 && pos < src.size(); guard++) {  // l'en-tete : les premieres lignes seulement
                    size_t eol = src.find('\n', pos);
                    if (eol == std::string::npos) eol = src.size();
                    const std::string line = src.substr(pos, eol - pos);
                    takeLine(line, "en", it.title[0], it.text[0]);
                    takeLine(line, "fr", it.title[1], it.text[1]);
                    pos = eol + 1;
                }
            }
            if (it.title[0].empty()) it.title[0] = n;
            if (it.title[1].empty()) it.title[1] = it.title[0];
            if (it.text[1].empty()) it.text[1] = it.text[0];
            all.push_back(std::move(it));
        }
    }
    return all;
}

std::wstring galleryTitle(const GalleryItem& it, const std::string& lang) { return it.title[lang == "fr" ? 1 : 0]; }
std::wstring galleryText(const GalleryItem& it, const std::string& lang) { return it.text[lang == "fr" ? 1 : 0]; }
