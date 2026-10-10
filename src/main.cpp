// ShaderToMP4 : Shadertoy (onglet Image) -> video parfaite via ffmpeg.exe.
// Point d'entree : ligne de commande, preference GPU, fenetre principale. Le moteur est dans src/core, l'interface dans src/gui.
#include "gui/app.h"
#include "core/ffmpeg_download.h"
#include "core/gpu_pref.h"
#include "core/selftest.h"
#include "core/util.h"

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// Demande le GPU puissant sur les PC portables hybrides (NVIDIA / AMD)
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    wchar_t exePath[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    g.exeDir = exePath;
    g.exeDir.erase(g.exeDir.find_last_of(L"\\/") + 1);

    // config.ini (cree automatiquement au premier lancement)
    std::wstring ini = g.exeDir + L"config.ini";
    if (!fileExists(ini)) writeIni(ini, g.base);
    std::string iniText;
    if (readFile(ini, iniText)) parseLines(g.base, iniText, false);

    // Ligne de commande : --selftest [--update] [--quiet] ou fichiers shader a convertir
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> files;
    bool selftest = false, update = false, quiet = false, getFfmpeg = false;
    for (int i = 1; argv && i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--selftest") selftest = true;
        else if (a == L"--update") update = true;
        else if (a == L"--quiet") quiet = true;
        else if (a == L"--get-ffmpeg") getFfmpeg = true;
        else files.push_back(a);
    }
    if (argv) LocalFree(argv);
    if (selftest) return runSelfTest(g.exeDir, update, quiet);
    if (getFfmpeg) {  // telechargement sans fenetre : ShaderToMP4.exe --get-ffmpeg [--quiet]
        std::wstring e;
        bool ok = downloadFfmpeg(g.exeDir, [](int, const std::wstring&) {}, nullptr, e);
        if (!quiet)
            MessageBoxW(nullptr, ok ? L"ffmpeg.exe installe." : e.c_str(), L"ShaderToMP4", ok ? MB_ICONINFORMATION : MB_ICONWARNING);
        return ok ? 0 : 1;
    }

    // Preference GPU (config.ini : gpu=high|low) : prise en compte au lancement suivant -> relance unique
    if (applyGpuPreference(g.base.gpu, exePath)) {
        STARTUPINFOW rsi{sizeof(rsi)};
        PROCESS_INFORMATION rpi{};
        std::wstring cl = GetCommandLineW();
        if (CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &rsi, &rpi)) {
            CloseHandle(rpi.hProcess);
            CloseHandle(rpi.hThread);
            return 0;
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"ShaderToMP4Main";
    RegisterClassExW(&wc);

    HWND wnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"ShaderToMP4 " APP_VER_WSTR, WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 940, 880, nullptr, nullptr, hi, nullptr);
    if (!wnd) return 1;
    ShowWindow(wnd, show);
    UpdateWindow(wnd);


    // Cartes graphiques detectees (si plusieurs : voir gpu= dans config.ini)
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
        if (count > 1)
            appendLog(L"Cartes graphiques : " + names + L"\r\n(gpu=high ou gpu=low dans config.ini choisit celle que Windows utilise pour ce programme.)");
    }
    appendLog(L"ShaderToMP4 " APP_VER_WSTR);
    // Fichiers glisses sur le .exe : ajoutes puis rendu lance directement
    if (!files.empty()) {
        addFiles(files);
        startJobs();
    } else if (!ffmpegPresent(g.exeDir)) {
        promptDownload();
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
