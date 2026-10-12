// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "channeldrop.h"
#include "util.h"

namespace {

std::wstring extensionOf(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const size_t cut = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (cut != std::wstring::npos && dot < cut)) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

bool oneOf(const std::wstring& e, std::initializer_list<const wchar_t*> list) {
    for (const wchar_t* x : list)
        if (e == x) return true;
    return false;
}

std::wstring normalized(std::wstring p) {
    for (auto& c : p)
        if (c == L'\\') c = L'/';
    return p;
}

// Chemin relatif au dossier du shader si le fichier s'y trouve (comparaison sans casse), sinon le chemin complet
std::string pathFor(const std::wstring& shaderPath, const std::wstring& file) {
    const std::wstring sp = normalized(shaderPath), fp = normalized(file);
    const size_t cut = sp.find_last_of(L'/');
    const std::wstring dir = cut == std::wstring::npos ? L"" : sp.substr(0, cut + 1);
    if (!dir.empty() && fp.size() > dir.size()) {
        bool same = true;
        for (size_t i = 0; i < dir.size() && same; i++) same = towlower(fp[i]) == towlower(dir[i]);
        if (same) return U8(fp.substr(dir.size()));
    }
    return U8(fp);
}

}  // namespace

DropKind classifyDropped(const std::wstring& path) {
    const std::wstring e = extensionOf(path);
    if (oneOf(e, {L"glsl", L"txt", L"json", L"montage"})) return DropKind::Shader;
    if (oneOf(e, {L"png", L"jpg", L"jpeg", L"bmp", L"tga", L"hdr", L"exr", L"gif", L"webp", L"tif", L"tiff"})) return DropKind::Image;
    if (oneOf(e, {L"wav", L"mp3", L"flac", L"ogg", L"m4a", L"aac", L"opus"})) return DropKind::Sound;
    if (oneOf(e, {L"mp4", L"mov", L"mkv", L"webm", L"avi", L"m4v"})) return DropKind::Video;
    return DropKind::Other;
}

Override channelOverride(const std::wstring& shaderPath, const std::wstring& file, DropKind kind, int index) {
    const std::string prefix = kind == DropKind::Sound ? "audio:" : kind == DropKind::Video ? "video:" : "";
    return {"iChannel" + std::to_string(std::max(0, std::min(3, index))), prefix + pathFor(shaderPath, file)};
}

Override soundtrackOverride(const std::wstring&, const std::wstring& file) { return {"audio", U8(file)}; }
