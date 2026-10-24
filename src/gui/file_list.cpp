// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "core/batches.h"
#include "ui_theme.h"

int indexOfId(int id) {
    for (size_t i = 0; i < g.ids.size(); i++)
        if (g.ids[i] == id) return (int)i;
    return -1;
}

std::wstring itemLabel(const std::wstring& path) {
    const auto it = g.priority.find(path);
    const std::wstring mark = it == g.priority.end() || it->second == 0 ? L"" : it->second > 0 ? L"\u25B2 " : L"\u25BC ";
    return mark + batchLabel(g.batches, path);
}

void addFiles(const std::vector<std::wstring>& files) {
    if (g.running) return;
    for (const auto& f0 : files) {
        if (f0.size() > wcslen(kProjectExt) && _wcsicmp(f0.c_str() + f0.size() - wcslen(kProjectExt), kProjectExt) == 0) {  // un fichier projet depose : on l'ouvre
            openProjectFile(f0);
            continue;
        }
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
        g.ids.push_back(g.nextId++);
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = (int)g.paths.size() - 1;
        const std::wstring shown = itemLabel(f);
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
        g.ids.erase(g.ids.begin() + *it);
    }
    updateButtons();
}

// Les shaders selectionnes montent, descendent ou passent en tete de file ; leur ordre relatif est conserve
void moveSelected(int mode) {
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
    std::vector<int> state, ids;
    for (int k : order) {
        paths.push_back(g.paths[(size_t)k]);
        state.push_back(g.state[(size_t)k]);
        ids.push_back(g.ids[(size_t)k]);
    }
    g.paths = paths;
    g.state = state;
    g.ids = ids;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    for (int k = 0; k < n; k++) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = k;
        const std::wstring shown = itemLabel(g.paths[(size_t)k]);
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
    rebuildPlan();  // rendu en cours : le travail suivant suit le nouvel ordre
}

void setBatchOfSelected(int n) {
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < (int)g.paths.size()) {
        if (n > 0) g.batches[g.paths[(size_t)i]] = n;
        else g.batches.erase(g.paths[(size_t)i]);
        setItemText(i, 0, itemLabel(g.paths[(size_t)i]));
    }
    saveBatches(g.exeDir, g.batches);
}

void showListMenu() {
    if (g.paths.empty()) return;
    const bool busy = g.running;
    HMENU m = CreatePopupMenu();
    const UINT en = busy ? MF_GRAYED : MF_ENABLED;
    AppendMenuW(m, MF_STRING, 1, TX("ui.menu.move_top").c_str());
    AppendMenuW(m, MF_STRING, 2, TX("ui.menu.move_up").c_str());
    AppendMenuW(m, MF_STRING, 3, TX("ui.menu.move_down").c_str());
    AppendMenuW(m, MF_STRING, 9, TX("ui.menu.job_hold").c_str());
    {  // priorite du travail : les plus hautes passent avant les autres
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, 51, TX("ui.menu.prio_high").c_str());
        AppendMenuW(sub, MF_STRING, 52, TX("ui.menu.prio_normal").c_str());
        AppendMenuW(sub, MF_STRING, 53, TX("ui.menu.prio_low").c_str());
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, TX("ui.menu.priority").c_str());
    }
    AppendMenuW(m, MF_STRING, 10, TX("ui.menu.job_log").c_str());
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
    else if (cmd == 9) toggleHoldSelected();
    else if (cmd == 10) openJobLog();
    else if (cmd >= 51 && cmd <= 53) setPriorityOfSelected(cmd == 51 ? 1 : cmd == 52 ? 0 : -1);
    else if (cmd > 20 && cmd <= 20 + kBatchCount) setBatchOfSelected(cmd - 20);
    else if (cmd == 30) setBatchOfSelected(0);
    else if (cmd > 40 && cmd <= 40 + kBatchCount) startJobs(false, 0, 0, 0, false, false, cmd - 40);
}

void clearAll() {
    if (g.running) return;
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    g.paths.clear();
    g.state.clear();
    g.ids.clear();
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


// Suspendre ou reprendre : un travail en attente est mis de cote (« Suspendu ») ou remis dans la file ; celui qui tourne passe par le bouton Pause
void toggleHoldSelected() {
    int i = -1;
    bool pausedRunning = false;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < (int)g.state.size()) {
        int& st = g.state[(size_t)i];
        if (st == ST_PENDING) st = ST_HELD;
        else if (st == ST_HELD) st = ST_PENDING;
        else if (st == ST_RUNNING && !pausedRunning) {
            pausedRunning = true;
            PostMessageW(g.wnd, WM_COMMAND, ID_PAUSE, 0);
            continue;
        } else {
            continue;
        }
        setItemText(i, 1, statusText(st));
    }
    InvalidateRect(g.list, nullptr, FALSE);
    saveQueueNow();
    rebuildPlan();
    updateButtons();
}

void setPriorityOfSelected(int p) {
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < (int)g.paths.size()) {
        if (p == 0) g.priority.erase(g.paths[(size_t)i]);
        else g.priority[g.paths[(size_t)i]] = p;
        setItemText(i, 0, itemLabel(g.paths[(size_t)i]));
    }
    rebuildPlan();
}

// Journal du travail selectionne : le fichier ecrit pendant son dernier rendu s'ouvre dans l'editeur de texte du systeme
void openJobLog() {
    const std::wstring shader = selectedShaderPath();
    if (shader.empty()) return;
    const std::wstring log = jobLogPath(shader);
    if (!fileExists(log)) {
        appendLog(TXF("log.job_log_none", shader.c_str()));
        return;
    }
    ShellExecuteW(g.wnd, L"open", L"notepad.exe", (L"\"" + log + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

// Glisser les travaux selectionnes : un trait montre l'endroit ou ils seront deposes
static bool gDrag = false;
static int gDragTarget = -1;
static bool gDragAfter = false;

bool listDragging() { return gDrag; }

static void showInsertMark(int item, bool after) {
    LVINSERTMARK im{};
    im.cbSize = sizeof(im);
    im.dwFlags = after ? LVIM_AFTER : 0;
    im.iItem = item;
    SendMessageW(g.list, LVM_SETINSERTMARK, 0, (LPARAM)&im);
}

void listDragBegin(int item) {
    if (item < 0 || item >= (int)g.paths.size()) return;
    LVITEMW st{};
    st.stateMask = LVIS_SELECTED;
    if (!(SendMessageW(g.list, LVM_GETITEMSTATE, (WPARAM)item, LVIS_SELECTED) & LVIS_SELECTED)) {  // on glisse un travail non selectionne : il devient la selection
        st.state = 0;
        SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
        st.state = LVIS_SELECTED;
        SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)item, (LPARAM)&st);
    }
    SendMessageW(g.list, LVM_SETINSERTMARKCOLOR, 0, theme::cref(theme::ACCENT_A));
    gDrag = true;
    gDragTarget = -1;
    SetCapture(g.wnd);
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32645)));
}

void listDragMove(POINT pt) {  // pt : coordonnees client de la fenetre principale
    if (!gDrag) return;
    POINT lp = pt;
    MapWindowPoints(g.wnd, g.list, &lp, 1);
    RECT lr;
    GetClientRect(g.list, &lr);
    if (lp.y < 12) SendMessageW(g.list, LVM_SCROLL, 0, -MulDiv(16, (int)g.dpi, 96));  // le bord de la liste fait defiler
    else if (lp.y > lr.bottom - 12) SendMessageW(g.list, LVM_SCROLL, 0, MulDiv(16, (int)g.dpi, 96));
    LVHITTESTINFO ht{};
    ht.pt = {std::min<LONG>(std::max<LONG>(lp.x, 2), lr.right - 2), lp.y};
    SendMessageW(g.list, LVM_HITTEST, 0, (LPARAM)&ht);
    if (ht.iItem < 0) {  // sous le dernier : apres lui ; au-dessus du premier : avant lui
        gDragTarget = lp.y > 0 ? (int)g.paths.size() - 1 : 0;
        gDragAfter = lp.y > 0;
    } else {
        RECT ir{};
        ir.left = LVIR_BOUNDS;
        SendMessageW(g.list, LVM_GETITEMRECT, (WPARAM)ht.iItem, (LPARAM)&ir);
        gDragTarget = ht.iItem;
        gDragAfter = lp.y > (ir.top + ir.bottom) / 2;
    }
    showInsertMark(gDragTarget, gDragAfter);
}

void listDragEnd(bool apply) {
    if (!gDrag) return;
    gDrag = false;
    ReleaseCapture();
    showInsertMark(-1, false);
    const int target = gDragTarget;
    const bool after = gDragAfter;
    gDragTarget = -1;
    if (!apply || target < 0 || target >= (int)g.paths.size()) return;
    const int n = (int)g.paths.size();
    std::vector<char> isSel((size_t)n, 0);
    int i = -1;
    while ((i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) != -1 && i < n) isSel[(size_t)i] = 1;
    // Les travaux selectionnes (ordre conserve) se placent avant ou apres la cible ; si la cible est elle-meme selectionnee, rien ne bouge
    if (isSel[(size_t)target]) return;
    std::vector<int> moved, rest;
    for (int k = 0; k < n; k++) (isSel[(size_t)k] ? moved : rest).push_back(k);
    std::vector<int> order;
    for (int k : rest) {
        if (k == target && !after) order.insert(order.end(), moved.begin(), moved.end());
        order.push_back(k);
        if (k == target && after) order.insert(order.end(), moved.begin(), moved.end());
    }
    std::vector<std::wstring> paths;
    std::vector<int> state, ids;
    for (int k : order) {
        paths.push_back(g.paths[(size_t)k]);
        state.push_back(g.state[(size_t)k]);
        ids.push_back(g.ids[(size_t)k]);
    }
    g.paths = paths;
    g.state = state;
    g.ids = ids;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    for (int k = 0; k < n; k++) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = k;
        const std::wstring shown = itemLabel(g.paths[(size_t)k]);
        it.pszText = const_cast<LPWSTR>(shown.c_str());
        SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        setItemText(k, 1, statusText(g.state[(size_t)k]));
    }
    for (int k = 0; k < n; k++) {
        LVITEMW it{};
        it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        it.state = isSel[(size_t)order[(size_t)k]] ? (LVIS_SELECTED | LVIS_FOCUSED) : 0;
        SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)k, (LPARAM)&it);
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    saveQueueNow();
    rebuildPlan();
}
