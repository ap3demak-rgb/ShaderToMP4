// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// ShaderToMP4 : Shadertoy (onglet Image) -> vidéo parfaite via ffmpeg.exe.
// Point d'entrée : ligne de commande, langue, fenêtre principale. Le moteur est dans src/core, l'interface dans src/gui.
#include "gui/app.h"
#include "core/cli.h"
#include "core/ffmpeg_download.h"
#include "core/i18n.h"
#include "core/prolicense.h"
#include "core/proext.h"
#include "core/selftest.h"
#include "core/util.h"
#include "gui/ui_controls.h"
#include "gui/ui_render.h"
#include "gui/ui_theme.h"

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif

// Demande le GPU puissant sur les PC portables hybrides (NVIDIA / AMD)
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

// Écrit du texte UTF-8 sur la console du programme appelant (le .exe est une application fenêtrée sans console propre).
void writeConsole(const std::string& utf8) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {  // lancé depuis un terminal sans redirection : on s'y rattache
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
        out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!out || out == INVALID_HANDLE_VALUE) return;
    }
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(out, &mode)) {  // vraie console : texte large
        std::wstring w = W(utf8);
        WriteConsoleW(out, w.c_str(), (DWORD)w.size(), &written, nullptr);
    } else {  // redirection vers un fichier ou un tube : UTF-8 tel quel
        WriteFile(out, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    }
}

std::string versionText() {
    std::string t;
    t += std::string(APP_NAME_STR) + " " + APP_VER_STR + "\n";
    t += std::string("Copyright (C) ") + APP_COPYRIGHT_YEAR + " " + APP_COPYRIGHT_OWNER + "\n";
    t += i18n::tr8("about.license") + "\n";
    t += i18n::tr8("about.contact") + ": " + APP_CONTACT_EMAIL + "\n";
    t += i18n::tr8("about.website") + ": " + APP_WEBSITE_URL + "\n";
    return t;
}

std::string helpText() {
    std::string t = versionText() + "\n" + i18n::tr8("cli.usage") + "\n\n";
    for (const char* k : {"cli.opt.files", "cli.opt.selftest", "cli.opt.update", "cli.opt.render", "cli.opt.quiet", "cli.opt.get_ffmpeg",
                          "cli.opt.version", "cli.opt.help"})
        t += "  " + i18n::tr8(k) + "\n";
    return t + renderHelpText();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    wchar_t exePath[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    g.exeDir = exePath;
    g.exeDir.erase(g.exeDir.find_last_of(L"\\/") + 1);

    // Langue : celle mémorisée dans window.ini, sinon celle du système ; l'anglais embarqué sert de repli
    if (!i18n::init(g.exeDir, readSavedLanguage())) {
        // Erreur d'amorçage : les textes eux-mêmes sont inaccessibles, ce message est donc le seul qui ne vient pas d'un fichier de langue.
        MessageBoxW(nullptr, L"The built-in language data is missing or damaged.", APP_NAME_WSTR, MB_ICONERROR);  // i18n-bootstrap
        return 1;
    }

    initLicense(g.exeDir);  // fichier de licence a cote du programme, relu a chaque demarrage
    initProExtensions();    // fonctions de l'edition Pro (aucune dans l'edition libre)

    // Ligne de commande
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> files;
    bool selftest = false, update = false, quiet = false, getFfmpeg = false, version = false, help = false, render = false,
         diagnostic = false, benchmark = false, licenseInfo = false;
    for (int i = 1; argv && i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--selftest") selftest = true;
        else if (a == L"--render") render = true;
        else if (a.compare(0, 6, L"--dpi=") == 0) theme::dpiOverride = (UINT)std::max(96, std::min(384, _wtoi(a.c_str() + 6)));  // verification de l'echelle
        else if (a == L"--diagnostic") diagnostic = true;
        else if (a == L"--benchmark") benchmark = true;
        else if (a == L"--license-info") licenseInfo = true;
        else if (a == L"--update") update = true;
        else if (a == L"--quiet") quiet = true;
        else if (a == L"--get-ffmpeg") getFfmpeg = true;
        else if (a == L"--version") version = true;
        else if (a == L"--help" || a == L"-h" || a == L"/?") help = true;
        else files.push_back(a);
    }
    if (argv) LocalFree(argv);

    // Modes sans fenêtre
    if (version || help) {
        writeConsole(help ? helpText() : versionText());
        return 0;
    }
    if (licenseInfo) return runLicenseInfoCli(g.exeDir, [](const std::string& s, bool) { writeConsole(s); });
    if (diagnostic) return runDiagnosticCli(g.exeDir, [](const std::string& s, bool) { writeConsole(s); });
    if (benchmark) return runBenchmarkCli(g.exeDir, files, [](const std::string& s, bool) { writeConsole(s); });
    if (render)  // ShaderToMP4 --render [options] fichiers : sans fenetre, texte sur la console de l'appelant
        return runRenderCli(g.exeDir, files, [](const std::string& s, bool) { writeConsole(s); });
    std::string iniText;
    const std::wstring ini = g.exeDir + L"config.ini";
    if (selftest) return runSelfTest(g.exeDir, update, quiet);
    if (getFfmpeg) {  // téléchargement sans fenêtre : ShaderToMP4.exe --get-ffmpeg [--quiet]
        std::wstring e;
        bool ok = downloadFfmpeg(g.exeDir, [](int, const std::wstring&) {}, nullptr, e);
        if (!quiet) MessageBoxW(nullptr, ok ? TX("dl.installed").c_str() : e.c_str(), APP_NAME_WSTR, ok ? MB_ICONINFORMATION : MB_ICONWARNING);
        return ok ? 0 : 1;
    }

    OleInitialize(nullptr);  // glisser-déposer OLE et barre des tâches (ITaskbarList3)
    if (!ui::init() || !ui::registerControls(hi)) {
        MessageBoxW(nullptr, TX("error.direct2d").c_str(), APP_NAME_WSTR, MB_ICONERROR);  // i18n-bootstrap
        return 1;
    }
    // config.ini (créé automatiquement au premier lancement)
    if (!fileExists(ini)) writeIni(ini, g.base);
    if (readFile(ini, iniText)) parseLines(g.base, iniText, false);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = CreateSolidBrush(theme::cref(theme::BG));  // pas d'éclair blanc au démarrage
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"ShaderToMP4Main";
    RegisterClassExW(&wc);

    // Taille initiale : 1000 x 900 dp, sans dépasser la zone de travail
    const UINT sysDpi = theme::dpiOverride ? theme::dpiOverride : GetDpiForSystem();
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    RECT want{0, 0, MulDiv(1000, (int)sysDpi, 96), MulDiv(900, (int)sysDpi, 96)};
    AdjustWindowRectExForDpi(&want, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    int ww = std::min((int)(want.right - want.left), (int)(wa.right - wa.left));
    int wh = std::min((int)(want.bottom - want.top), (int)(wa.bottom - wa.top));
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, APP_NAME_WSTR L" " APP_VER_WSTR, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, nullptr, nullptr, hi, nullptr);
    if (!wnd) return 1;
    restoreWindowPlacement(wnd, show);
    UpdateWindow(wnd);

    // Raccourcis clavier
    ACCEL acc[] = {
        {FVIRTKEY | FCONTROL, 'O', ID_ADD},
        {FVIRTKEY | FCONTROL, 'P', ID_PREVBTN},
        {FVIRTKEY | FCONTROL, 'E', ID_EDITOR},
        {FVIRTKEY, VK_F1, ID_ABOUT},
        {FVIRTKEY, VK_F5, ID_START},
        {FVIRTKEY, VK_F6, ID_PAUSE},
        {FVIRTKEY, VK_ESCAPE, ID_CANCEL},
    };
    std::vector<ACCEL> accAll(acc, acc + sizeof(acc) / sizeof(acc[0]));
    if (const ProExt* px = proExt(); px && px->tools.undoRedo) {  // annuler / retablir (edition Pro)
        accAll.push_back({FVIRTKEY | FCONTROL, 'Z', ID_UNDO});
        accAll.push_back({FVIRTKEY | FCONTROL, 'Y', ID_REDO});
        accAll.push_back({FVIRTKEY | FCONTROL | FSHIFT, 'Z', ID_REDO});
    }
    HACCEL haccel = CreateAcceleratorTableW(accAll.data(), (int)accAll.size());

    // Cartes graphiques détectées (affichées s'il y en a plusieurs)
    {
        std::wstring names;
        int count = 0;
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof(dd);
        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++) {
            if (names.find(dd.DeviceString) == std::wstring::npos) {
                if (!names.empty()) names += L" ; ";
                names += dd.DeviceString;
                count++;
            }
        }
        if (count > 1) appendLog(TXF("log.graphics_cards", names.c_str()));
    }
    appendLog(APP_NAME_WSTR L" " APP_VER_WSTR);
    // Fichiers glissés sur le .exe : ajoutés puis rendu lancé directement
    if (!files.empty()) {
        addFiles(files);
        startJobs();
    } else {
        const std::vector<std::wstring> restored = loadQueue(g.exeDir);  // shaders restes a rendre lors de la derniere session
        if (!restored.empty()) {
            addFiles(restored);
            appendLog(TXF("log.queue_restored", (int)restored.size()));
        }
        if (!ffmpegPresent(g.exeDir)) promptDownload();
    }

    toolsChanged();  // etat de depart de l'annulation (edition Pro)
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        const bool inMain = GetAncestor(m.hwnd, GA_ROOT) == wnd;  // l'éditeur et les autres fenêtres ont leurs propres touches
        if (inMain && TranslateAcceleratorW(wnd, haccel, &m)) continue;
        if (inMain && IsDialogMessageW(wnd, &m)) continue;  // Tab / Maj+Tab entre les contrôles
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    ui::shutdown();
    OleUninitialize();
    return (int)m.wParam;
}
