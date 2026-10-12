// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Couche plateforme, version Windows : fichiers, système, processus.
#include "platform/platform.h"
#include "core/util.h"
#include "core/i18n.h"
#include "app_info.h"
#include <wininet.h>
#include <wincrypt.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "advapi32.lib")
#include "../../../res/resource.h"
#include <shellapi.h>
#include <shlobj.h>

namespace platform {

// --- Chemins et fichiers -------------------------------------------------------------------------------------------
std::wstring appDir() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    std::wstring p(b, n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"" : p.substr(0, s + 1);
}

bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

FILE* openFile(const std::wstring& path, const char* mode) {
    return _wfopen(path.c_str(), W(mode).c_str());
}

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = openFile(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(sz > 0 ? (size_t)sz : 0);
    if (sz > 0 && fread(&out[0], 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

bool makeDir(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool removeFile(const std::wstring& path) { return DeleteFileW(path.c_str()) != 0; }
bool removeDir(const std::wstring& path) { return RemoveDirectoryW(path.c_str()) != 0; }

bool moveFile(const std::wstring& from, const std::wstring& to, bool overwrite) {
    return MoveFileExW(from.c_str(), to.c_str(), overwrite ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
}

bool freeSpace(const std::wstring& path, uint64_t& bytes) {
    ULARGE_INTEGER freeBytes{};
    if (!GetDiskFreeSpaceExW(path.c_str(), &freeBytes, nullptr, nullptr)) return false;
    bytes = freeBytes.QuadPart;
    return true;
}

bool listDir(const std::wstring& path, std::vector<DirEntry>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        out.push_back({n, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

std::wstring homeDir() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", b, MAX_PATH * 2);
    return n > 0 && n < MAX_PATH * 2 ? std::wstring(b, n) : appDir();
}

wchar_t pathSeparator() { return L'\\'; }

// --- Système -------------------------------------------------------------------------------------------------------
std::string systemLanguage() {
    wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
    if (!GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH)) return "en";
    std::string s = lower(U8(loc));
    return s.size() >= 2 ? s.substr(0, 2) : "en";
}

std::wstring exeFileName(const std::wstring& baseName) { return baseName + L".exe"; }

std::wstring findInPath(const std::wstring& fileName) {
    wchar_t b[MAX_PATH * 2];
    DWORD n = SearchPathW(nullptr, fileName.c_str(), nullptr, MAX_PATH * 2, b, nullptr);
    return n > 0 && n < MAX_PATH * 2 ? std::wstring(b, n) : std::wstring();
}

void openWithDefault(const std::wstring& pathOrUrl) {
    ShellExecuteW(nullptr, L"open", pathOrUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void revealInFolder(const std::wstring& filePath) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + filePath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void sleepMs(int ms) { Sleep((DWORD)ms); }

void applyGpuPreference(const std::string&) {}  // le programme exporte NvOptimusEnablement / AmdPowerXpress ; rien d'autre sans toucher au registre

DateTime localTime() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return {t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond};
}

std::wstring systemFontFile() {
    wchar_t dir[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    for (const wchar_t* name : {L"\\Fonts\\segoeui.ttf", L"\\Fonts\\arial.ttf", L"\\Fonts\\tahoma.ttf"}) {
        const std::wstring p = std::wstring(dir) + name;
        if (fileExists(p)) return p;
    }
    return L"";
}

void showMessage(const std::wstring& title, const std::wstring& text, MessageKind kind) {
    UINT icon = kind == MessageKind::Error ? MB_ICONERROR : kind == MessageKind::Warning ? MB_ICONWARNING : MB_ICONINFORMATION;
    MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | icon);
}

// --- Données embarquées ----------------------------------------------------------------------------------------
bool embeddedData(const char* name, std::string& out) {
    int id = 0;
    if (!strcmp(name, "lang/en.json")) id = IDR_LANG_EN;
    else if (!strcmp(name, "lang/fr.json")) id = IDR_LANG_FR;
    if (!id) return false;
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    const void* d = g ? LockResource(g) : nullptr;
    DWORD n = SizeofResource(nullptr, r);
    if (!d || !n) return false;
    out.assign((const char*)d, n);
    return true;
}

// --- Processus -----------------------------------------------------------------------------------------------------
struct Process::Impl {
    HANDLE proc = nullptr;
    HANDLE in = nullptr;   // écriture vers stdin du programme
    HANDLE out = nullptr;  // lecture de stdout/stderr
};

Process::Process(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

static std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            backslashes++;
            continue;
        }
        if (c == L'"') r.append(backslashes * 2 + 1, L'\\');
        else r.append(backslashes, L'\\');
        backslashes = 0;
        r += c;
    }
    r.append(backslashes * 2, L'\\');
    return r + L"\"";
}

std::unique_ptr<Process> Process::start(const std::vector<std::wstring>& argv, bool pipeStdin, bool captureOutput) {
    if (argv.empty()) return nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inRd = nullptr, inWr = nullptr, outRd = nullptr, outWr = nullptr, nul = INVALID_HANDLE_VALUE;
    if (pipeStdin) {
        if (!CreatePipe(&inRd, &inWr, &sa, 16 << 20)) return nullptr;
        SetHandleInformation(inWr, HANDLE_FLAG_INHERIT, 0);
    } else {
        nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }
    if (captureOutput) {
        if (!CreatePipe(&outRd, &outWr, &sa, 0)) {
            if (inRd) CloseHandle(inRd);
            if (inWr) CloseHandle(inWr);
            if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
            return nullptr;
        }
        SetHandleInformation(outRd, HANDLE_FLAG_INHERIT, 0);
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = pipeStdin ? inRd : nul;
    si.hStdOutput = captureOutput ? outWr : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = captureOutput ? outWr : GetStdHandle(STD_ERROR_HANDLE);
    std::wstring cmd;
    for (size_t i = 0; i < argv.size(); i++) cmd += (i ? L" " : L"") + quoteArg(argv[i]);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (inRd) CloseHandle(inRd);
    if (outWr) CloseHandle(outWr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        if (inWr) CloseHandle(inWr);
        if (outRd) CloseHandle(outRd);
        return nullptr;
    }
    CloseHandle(pi.hThread);
    auto impl = std::make_unique<Impl>();
    impl->proc = pi.hProcess;
    impl->in = inWr;
    impl->out = outRd;
    return std::unique_ptr<Process>(new Process(std::move(impl)));
}

Process::~Process() {
    if (!impl_) return;
    if (impl_->in) CloseHandle(impl_->in);
    if (impl_->proc && WaitForSingleObject(impl_->proc, 0) == WAIT_TIMEOUT) TerminateProcess(impl_->proc, 1);
    if (impl_->out) CloseHandle(impl_->out);
    if (impl_->proc) CloseHandle(impl_->proc);
}

bool Process::writeStdin(const uint8_t* data, size_t size) {
    if (!impl_->in) return false;
    while (size > 0) {
        DWORD n = 0;
        if (!WriteFile(impl_->in, data, (DWORD)std::min<size_t>(size, 1 << 20), &n, nullptr) || n == 0) return false;
        data += n;
        size -= n;
    }
    return true;
}

void Process::closeStdin() {
    if (impl_->in) CloseHandle(impl_->in);
    impl_->in = nullptr;
}

size_t Process::readOutput(char* buf, size_t capacity) {
    if (!impl_->out) return 0;
    DWORD got = 0;
    if (!ReadFile(impl_->out, buf, (DWORD)capacity, &got, nullptr)) return 0;
    return got;
}

void Process::terminate() { TerminateProcess(impl_->proc, 1); }

int Process::wait() {
    WaitForSingleObject(impl_->proc, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(impl_->proc, &code);
    return (int)code;
}

// --- Réseau et empreintes ------------------------------------------------------------------------------------------
bool sha256File(const std::wstring& path, std::string& hex) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH h = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
    bool ok = false;
    if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
        if (FILE* f = openFile(path, "rb")) {
            std::vector<uint8_t> buf(1 << 20);
            size_t n;
            ok = true;
            while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
                if (!CryptHashData(h, buf.data(), (DWORD)n, 0)) { ok = false; break; }
            fclose(f);
        }
        BYTE d[32];
        DWORD len = sizeof(d);
        if (ok && CryptGetHashParam(h, HP_HASHVAL, d, &len, 0)) {
            char b[3];
            hex.clear();
            for (DWORD i = 0; i < len; i++) {
                snprintf(b, sizeof(b), "%02x", d[i]);
                hex += b;
            }
        } else {
            ok = false;
        }
        CryptDestroyHash(h);
    }
    CryptReleaseContext(prov, 0);
    return ok;
}

bool httpGet(const std::wstring& url, const std::function<bool(const uint8_t*, size_t, unsigned long long)>& sink,
                    std::wstring& err) {
    HINTERNET net = InternetOpenW(APP_NAME_WSTR L"/" APP_VER_WSTR, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) { err = TX("error.net_unavailable"); return false; }
    HINTERNET req = InternetOpenUrlW(net, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!req) {
        err = TXF("error.net_unreachable", url.c_str());
        InternetCloseHandle(net);
        return false;
    }
    DWORD status = 0, sz = sizeof(status);
    HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &sz, nullptr);
    if (status != 200) {
        err = TXF("error.net_status", (unsigned long)status, url.c_str());
        InternetCloseHandle(req);
        InternetCloseHandle(net);
        return false;
    }
    wchar_t lenBuf[32] = {0};
    DWORD lsz = sizeof(lenBuf), idx = 0;
    unsigned long long total = 0;
    if (HttpQueryInfoW(req, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lsz, &idx)) total = _wcstoui64(lenBuf, nullptr, 10);
    std::vector<uint8_t> buf(1 << 16);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!InternetReadFile(req, buf.data(), (DWORD)buf.size(), &got)) { err = TX("error.net_interrupted"); ok = false; break; }
        if (got == 0) break;
        if (!sink(buf.data(), got, total)) { err = TX("error.net_cancelled"); ok = false; break; }
    }
    InternetCloseHandle(req);
    InternetCloseHandle(net);
    return ok;
}

// --- OpenGL --------------------------------------------------------------------------------------------------------
namespace {
struct WinGL {
    HWND wnd = nullptr;
    HDC dc = nullptr;
    HGLRC rc = nullptr;
};
}  // namespace

void destroyGLContext(void*& ctx) {
    WinGL* c = (WinGL*)ctx;
    if (!c) return;
    if (c->rc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(c->rc);
    }
    if (c->wnd) {
        if (c->dc) ReleaseDC(c->wnd, c->dc);
        DestroyWindow(c->wnd);
    }
    delete c;
    ctx = nullptr;
}

bool prepareOffThreadContexts() { return true; }
void releaseOffThreadContexts() {}

bool createGLContext(void*& ctx, std::wstring& err) {
    WinGL* c = new WinGL;
    ctx = c;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hi;
    wc.lpszClassName = L"ShaderToMP4Hidden";
    RegisterClassW(&wc);  // échoue sans gravité si déjà enregistrée
    c->wnd = CreateWindowExW(0, wc.lpszClassName, L"ShaderToMP4", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, hi, nullptr);
    if (!c->wnd) { err = TX("error.gl_window"); return false; }
    c->dc = GetDC(c->wnd);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    int pf = ChoosePixelFormat(c->dc, &pfd);
    if (!pf || !SetPixelFormat(c->dc, pf, &pfd)) { err = TX("error.gl_pixelformat"); return false; }

    HGLRC tmp = wglCreateContext(c->dc);
    c->rc = tmp;
    if (!tmp || !wglMakeCurrent(c->dc, tmp)) { err = TX("error.gl_context"); return false; }

    typedef HGLRC(WINAPI * PFN_CCA)(HDC, HGLRC, const int*);
    PFN_CCA cca = (PFN_CCA)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!cca) { err = TX("error.gl_version"); return false; }
    const int attr[] = {0x2091, 3, 0x2092, 3, 0x9126, 0x1, 0};  // 3.3 core
    HGLRC rc = cca(c->dc, nullptr, attr);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tmp);
    c->rc = nullptr;
    if (!rc || !wglMakeCurrent(c->dc, rc)) { err = TX("error.gl_core"); return false; }
    c->rc = rc;
    return true;
}

void* glProc(const char* name) { return (void*)wglGetProcAddress(name); }

}  // namespace platform
