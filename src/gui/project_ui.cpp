// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Projets (.s2mproj) : nouveau, ouvrir, enregistrer, enregistrer sous, projets recents, et le menu du bouton Projet qui rassemble aussi l'historique des
// rendus, les outils d'apercu, la palette de commandes et les raccourcis. Le format et sa lecture sont dans core/project.cpp.
#include "app.h"
#include "core/i18n.h"
#include "core/util.h"
#include "ui_controls.h"
#include "../../res/resource.h"
#include <commdlg.h>

static std::wstring projectFilter() { return makeFilter({{TX("dialog.project") + L" (*.s2mproj)", L"*.s2mproj"}, {TX("dialog.all_files"), L"*.*"}}); }

void updateTitle() {
    std::wstring t = APP_NAME_WSTR L" " APP_VER_WSTR;
    if (!g.projectName.empty()) t += L" - " + g.projectName;
    SetWindowTextW(g.wnd, t.c_str());
}

static std::wstring nameOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/");
    std::wstring n = cut == std::wstring::npos ? path : path.substr(cut + 1);
    const size_t dot = n.find_last_of(L'.');
    return dot == std::wstring::npos ? n : n.substr(0, dot);
}

// La liste de shaders est videe sans toucher a la selection de reglages propres : ceux-ci sont deja enregistres dans jobs/
static void emptyList() {
    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    g.paths.clear();
    g.state.clear();
    g.ids.clear();
    g.batches.clear();
    g.priority.clear();
    saveBatches(g.exeDir, g.batches);
}

// Les reglages de la fenetre sont lus (pour garder ce qui vient d'etre saisi), puis l'etat « reglages propres » est quitte sans rien effacer
static void leaveScope() {
    Settings cur = g.base;
    std::wstring e;
    if (uiToSettings(cur, e)) g.base = cur;
    flushFileScope();
    g.base = generalSettings();
    g.fileScope = App::FileScope();
}

void newProject() {
    if (g.running) return;
    stopPlayer();
    leaveScope();
    emptyList();
    g.projectPath.clear();
    g.projectName.clear();
    updateTitle();
    updateButtons();
    saveQueueNow();
    appendLog(TX("log.project_new"));
}

void openProjectFile(const std::wstring& path) {
    if (g.running) {
        appendLog(TX("log.project_busy"));
        return;
    }
    Project p;
    std::wstring err;
    if (!loadProject(path, p, err)) {
        MessageBoxW(g.wnd, err.c_str(), TX("dialog.project_title").c_str(), MB_ICONWARNING);
        return;
    }
    stopPlayer();
    leaveScope();
    emptyList();
    std::map<std::wstring, int> prio;
    const Settings s = applyProject(g.exeDir, p, Settings(), g.batches, &prio);  // reglages propres (jobs/) et lots du projet, reglages generaux renvoyes
    g.priority = prio;
    g.base = s;
    settingsToUI(g.base);
    saveSettings();
    std::vector<std::wstring> shaders;
    for (const ProjectShader& sh : p.shaders) shaders.push_back(sh.path);
    addFiles(shaders);
    g.fileScope.shown = L"";
    syncFileScope();
    g.projectPath = path;
    g.projectName = p.name;
    addRecentProject(g.exeDir, path);
    updateTitle();
    updateForced();
    updateEstimate();
    updateButtons();
    followSelection();
    saveQueueNow();
    appendLog(TXF("log.project_opened", p.name.c_str(), (int)p.shaders.size()));
    const std::vector<std::wstring> missing = projectMissing(p);
    for (size_t i = 0; i < missing.size() && i < 12; i++) appendLog(TXF("log.project_missing", missing[i].c_str()));
    if (missing.size() > 12) appendLog(TXF("log.project_missing_more", (int)missing.size() - 12));
}

void openProjectDialog() {
    if (g.running) return;
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = g.wnd;
    const std::wstring filter = projectFilter();
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf.data();
    o.nMaxFile = (DWORD)buf.size();
    o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&o)) openProjectFile(buf.data());
}

bool saveProjectNow(bool saveAs) {
    std::wstring path = g.projectPath;
    if (saveAs || path.empty()) {
        std::vector<wchar_t> buf(32768, 0);
        if (!g.projectName.empty()) wcsncpy_s(buf.data(), buf.size(), g.projectName.c_str(), _TRUNCATE);
        OPENFILENAMEW o{};
        o.lStructSize = sizeof(o);
        o.hwndOwner = g.wnd;
        const std::wstring filter = projectFilter();
        o.lpstrFilter = filter.c_str();
        o.lpstrFile = buf.data();
        o.nMaxFile = (DWORD)buf.size();
        o.lpstrDefExt = L"s2mproj";
        o.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&o)) return false;
        path = buf.data();
    }
    Settings cur = g.base;
    std::wstring e;
    if (!uiToSettings(cur, e)) {  // une valeur invalide : rien n'est ecrit
        MessageBoxW(g.wnd, e.c_str(), TX("dialog.invalid_settings").c_str(), MB_ICONWARNING);
        return false;
    }
    g.base = cur;
    saveSettings();
    flushFileScope();  // les reglages propres du shader courant sont lus sur le disque
    const std::wstring name = nameOf(path);
    const Project p = captureProject(g.exeDir, name, generalSettings(), g.paths, g.batches, g.priority);
    if (!saveProject(path, p, e)) {
        MessageBoxW(g.wnd, e.c_str(), TX("dialog.project_title").c_str(), MB_ICONWARNING);
        return false;
    }
    g.projectPath = path;
    g.projectName = name;
    addRecentProject(g.exeDir, path);
    updateTitle();
    appendLog(TXF("log.project_saved", path.c_str(), (int)p.shaders.size()));
    return true;
}

// Texte d'une ligne de menu : nom de l'action, tabulation, raccourci
static std::wstring menuText(const char* actionId) {
    const std::wstring key = actionShortcutText(actionId);
    return actionName(actionId) + (key.empty() ? L"" : L"\t" + key);
}

void showProjectMenu() {
    const UINT idle = g.running ? MF_GRAYED : MF_ENABLED;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | idle, ID_PROJ_NEW, menuText("project_new").c_str());
    AppendMenuW(m, MF_STRING | idle, ID_PROJ_OPEN, menuText("project_open").c_str());
    const std::vector<std::wstring> recent = recentProjects(g.exeDir);
    {
        HMENU sub = CreatePopupMenu();
        for (size_t i = 0; i < recent.size(); i++) AppendMenuW(sub, MF_STRING, (UINT_PTR)(kRecentProjectBase + (int)i), recent[i].c_str());
        if (recent.empty()) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, TX("ui.menu.no_recent").c_str());
        AppendMenuW(m, MF_POPUP | idle, (UINT_PTR)sub, TX("ui.menu.recent_projects").c_str());
    }
    AppendMenuW(m, MF_STRING, ID_PROJ_SAVE, menuText("project_save").c_str());
    AppendMenuW(m, MF_STRING, ID_PROJ_SAVEAS, menuText("project_saveas").c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_HISTORY, menuText("history").c_str());
    {  // outils de l'apercu
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_ZOOMFIT, menuText("zoom_fit").c_str());
        AppendMenuW(sub, MF_STRING, ID_ZOOM100, menuText("zoom_100").c_str());
        AppendMenuW(sub, MF_STRING | (g.frozen.on ? MF_CHECKED : 0), ID_FREEZE, menuText("freeze").c_str());
        AppendMenuW(sub, MF_STRING, ID_SCOPES, menuText("scopes").c_str());
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, TX("ui.menu.preview_tools").c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_PALETTE, menuText("palette").c_str());
    AppendMenuW(m, MF_STRING, ID_KEYS_EDIT, actionName("keys_edit").c_str());
    AppendMenuW(m, MF_STRING, ID_KEYS_RELOAD, actionName("keys_reload").c_str());
    RECT r;
    GetWindowRect(g.bProject, &r);
    SetForegroundWindow(g.wnd);
    const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd >= kRecentProjectBase && cmd < kRecentProjectBase + (int)recent.size()) openProjectFile(recent[(size_t)(cmd - kRecentProjectBase)]);
    else if (cmd != 0) PostMessageW(g.wnd, WM_COMMAND, (WPARAM)cmd, 0);
}
