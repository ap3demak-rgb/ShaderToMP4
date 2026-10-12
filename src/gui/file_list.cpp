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
        const std::wstring shown = batchLabel(g.batches, f);
        it.pszText = const_cast<LPWSTR>(shown.c_str());
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

// Les shaders selectionnes montent, descendent ou passent en tete de file ; leur ordre relatif est conserve
void moveSelected(int mode) {
    if (g.running) return;
    const int n = (int)g.paths.size();
    std::vector<char> isSel((size_t)n, 0);
    bool any = false;
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < n) {
        isSel[(size_t)i] = 1;
        any = true;
    }
    if (!any) return;
    std::vector<int> order((size_t)n);
    for (int k = 0; k < n; k++) order[(size_t)k] = k;
    if (mode == 0) {
        std::stable_partition(order.begin(), order.end(), [&](int k) { return isSel[(size_t)k] != 0; });
    } else if (mode < 0) {
        for (int k = 1; k < n; k++)
            if (isSel[(size_t)order[(size_t)k]] && !isSel[(size_t)order[(size_t)k - 1]]) std::swap(order[(size_t)k], order[(size_t)k - 1]);
    } else {
        for (int k = n - 2; k >= 0; k--)
            if (isSel[(size_t)order[(size_t)k]] && !isSel[(size_t)order[(size_t)k + 1]]) std::swap(order[(size_t)k], order[(size_t)k + 1]);
    }
    std::vector<std::wstring> paths;
    std::vector<int> state;
    for (int k : order) {
        paths.push_back(g.paths[(size_t)k]);
        state.push_back(g.state[(size_t)k]);
    }
    g.paths = paths;
    g.state = state;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    for (int k = 0; k < n; k++) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = k;
        const std::wstring shown = batchLabel(g.batches, g.paths[(size_t)k]);
        it.pszText = const_cast<LPWSTR>(shown.c_str());
        SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        setItemText(k, 1, statusText(g.state[(size_t)k]));
    }
    for (int k = 0; k < n; k++) {  // la selection suit les shaders deplaces
        LVITEMW it{};
        it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        it.state = isSel[(size_t)order[(size_t)k]] ? (LVIS_SELECTED | LVIS_FOCUSED) : 0;
        SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)k, (LPARAM)&it);
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    saveQueueNow();
}

void setBatchOfSelected(int n) {
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < (int)g.paths.size()) {
        if (n > 0) g.batches[g.paths[(size_t)i]] = n;
        else g.batches.erase(g.paths[(size_t)i]);
        setItemText(i, 0, batchLabel(g.batches, g.paths[(size_t)i]));
    }
    saveBatches(g.exeDir, g.batches);
}

void showListMenu() {
    if (g.paths.empty()) return;
    const bool busy = g.running;
    HMENU m = CreatePopupMenu();
    const UINT en = busy ? MF_GRAYED : MF_ENABLED;
    AppendMenuW(m, MF_STRING | en, 1, TX("ui.menu.move_top").c_str());
    AppendMenuW(m, MF_STRING | en, 2, TX("ui.menu.move_up").c_str());
    AppendMenuW(m, MF_STRING | en, 3, TX("ui.menu.move_down").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | en, 4, TX("ui.menu.render_selection").c_str());
    AppendMenuW(m, MF_STRING, 8, TX("ui.menu.edit").c_str());
    AppendMenuW(m, MF_STRING | en, 5, TX("ui.menu.reset_state").c_str());
    AppendMenuW(m, MF_STRING | (en == MF_ENABLED && g.fileScope.on ? MF_ENABLED : MF_GRAYED), 7, TX("ui.menu.write_shader").c_str());
    {  // lots : numeroter les shaders selectionnes, puis rendre un lot
        HMENU sub = CreatePopupMenu();
        for (int n = 1; n <= kBatchCount; n++) AppendMenuW(sub, MF_STRING | en, (UINT_PTR)(20 + n), TXF("ui.menu.batch_set", n).c_str());
        AppendMenuW(sub, MF_STRING | en, 30, TX("ui.menu.batch_none").c_str());
        AppendMenuW(m, MF_POPUP | en, (UINT_PTR)sub, TX("ui.menu.batch").c_str());
        for (int n = 1; n <= kBatchCount; n++) {
            int count = 0;
            for (const auto& p : g.paths) count += batchOf(g.batches, p) == n;
            if (count > 0) AppendMenuW(m, MF_STRING | en, (UINT_PTR)(40 + n), TXF("ui.menu.batch_render", n, count).c_str());
        }
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | en, 6, TX("ui.btn.remove").c_str());
    POINT pt;
    GetCursorPos(&pt);
    const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) moveSelected(0);
    else if (cmd == 2) moveSelected(-1);
    else if (cmd == 3) moveSelected(1);
    else if (cmd == 4) startJobs(false, 0, 0, 0, false, true);
    else if (cmd == 5) {  // les shaders termines ou en echec redeviennent « en attente » : ils seront rendus au prochain Demarrer
        int i = -1;
        while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < (int)g.state.size()) {
            g.state[(size_t)i] = ST_PENDING;
            setItemText(i, 1, statusText(ST_PENDING));
        }
        updateButtons();
    } else if (cmd == 6) removeSelected();
    else if (cmd == 7) writeScopeIntoShader();
    else if (cmd == 8) showEditor(g.wnd);
    else if (cmd > 20 && cmd <= 20 + kBatchCount) setBatchOfSelected(cmd - 20);
    else if (cmd == 30) setBatchOfSelected(0);
    else if (cmd > 40 && cmd <= 40 + kBatchCount) startJobs(false, 0, 0, 0, false, false, cmd - 40);
}

void clearAll() {
    if (g.running) return;
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    g.paths.clear();
    g.state.clear();
    updateButtons();
}

// Image, son ou video deposes : menu iChannel0-3 (et bande-son pour un son), puis ecrit la ligne dans le shader selectionne
void dropMedia(const std::vector<std::wstring>& files) {
    if (g.running) return;
    const std::wstring shader = selectedShaderPath();
    if (shader.empty()) {
        appendLog(TX("log.drop_media_no_shader"));
        return;
    }
    for (const auto& file : files) {
        const DropKind kind = classifyDropped(file);
        const std::wstring name = file.substr(file.find_last_of(L"\\/") == std::wstring::npos ? 0 : file.find_last_of(L"\\/") + 1);
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TXF("ui.menu.drop_title", name.c_str()).c_str());
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        for (int i = 0; i < 4; i++) AppendMenuW(m, MF_STRING, (UINT_PTR)(1 + i), TXF("ui.menu.drop_channel", i).c_str());
        if (kind == DropKind::Sound) AppendMenuW(m, MF_STRING, 10, TX("ui.menu.drop_soundtrack").c_str());
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g.wnd);
        const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, g.wnd, nullptr);
        DestroyMenu(m);
        if (cmd == 0) continue;
        const Override ov = cmd == 10 ? soundtrackOverride(shader, file) : channelOverride(shader, file, kind, cmd - 1);
        std::wstring err;
        if (!writeSettingsIntoShader(shader, {ov}, err)) {
            appendLog(TXF("log.error", err.c_str()));
            continue;
        }
        appendLog(TXF("log.channel_written", W(ov.key).c_str(), W(ov.value).c_str(), shader.c_str()));
        updateForced();  // le shader impose maintenant ce reglage
        if (g.player && g.player->running()) startPlayer();
    }
}

void browseFiles() {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = g.wnd;
    const std::wstring filter = makeFilter({{TX("dialog.shaders") + L" (*.txt;*.glsl;*.json;*.montage)", L"*.txt;*.glsl;*.json;*.montage"}, {TX("dialog.all_files"), L"*.*"}});
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

