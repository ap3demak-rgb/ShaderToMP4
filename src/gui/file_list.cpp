// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/i18n.h"
#include "core/util.h"

void addFiles(const std::vector<std::wstring>& files) {
    if (g.running) return;
    for (const auto& f0 : files) {
        std::wstring ie;  // un .json exporte de Shadertoy est importe, puis le shader obtenu est ajoute
        const std::wstring f = resolveShaderInput(g.exeDir, f0, [](const std::wstring& m) { appendLog(m); }, ie);
        if (f.empty()) {
            appendLog(TXF("log.error", ie.c_str()));
            continue;
        }
        bool dup = false;
        for (const auto& p : g.paths)
            if (_wcsicmp(p.c_str(), f.c_str()) == 0) { dup = true; break; }
        if (dup || !fileExists(f)) continue;
        g.paths.push_back(f);
        g.state.push_back(ST_PENDING);
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = (int)g.paths.size() - 1;
        it.pszText = const_cast<LPWSTR>(f.c_str());
        SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        setItemText(it.iItem, 1, statusText(ST_PENDING));
    }
    updateButtons();
}

void removeSelected() {
    if (g.running) return;
    std::vector<int> sel;
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1) sel.push_back(i);
    for (auto it = sel.rbegin(); it != sel.rend(); ++it) {
        SendMessageW(g.list, LVM_DELETEITEM, (WPARAM)*it, 0);
        g.paths.erase(g.paths.begin() + *it);
        g.state.erase(g.state.begin() + *it);
    }
    updateButtons();
}

void clearAll() {
    if (g.running) return;
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    g.paths.clear();
    g.state.clear();
    updateButtons();
}

void browseFiles() {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = g.wnd;
    const std::wstring filter = makeFilter({{TX("dialog.shaders") + L" (*.txt;*.glsl;*.json)", L"*.txt;*.glsl;*.json"}, {TX("dialog.all_files"), L"*.*"}});
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf.data();
    o.nMaxFile = (DWORD)buf.size();
    o.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&o)) return;
    // Un fichier : chemin complet. Plusieurs : dossier\0nom1\0nom2\0\0
    std::wstring first = buf.data();
    const wchar_t* next = buf.data() + first.size() + 1;
    std::vector<std::wstring> files;
    if (*next == 0) {
        files.push_back(first);
    } else {
        std::wstring dir = first;
        if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
        while (*next) {
            files.push_back(dir + next);
            next += wcslen(next) + 1;
        }
    }
    addFiles(files);
}

