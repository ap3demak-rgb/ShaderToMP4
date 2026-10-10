// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "util.h"

std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
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

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::wstring fmtTime(double sec) {
    if (sec < 0 || sec > 359999) sec = 0;
    int s = (int)(sec + 0.5);
    wchar_t b[32];
    swprintf(b, 32, L"%02d:%02d", s / 60, s % 60);
    return b;
}

std::wstring wfmt(const wchar_t* f, ...) {
    wchar_t b[1024];
    va_list a;
    va_start(a, f);
    _vsnwprintf(b, 1023, f, a);
    va_end(a);
    b[1023] = 0;
    return b;
}

std::string fnvHex(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char b[24];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    return b;
}

void clearDir(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

void removeTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(dir + L"\\" + n);
            else DeleteFileW((dir + L"\\" + n).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

std::wstring makeFilter(const std::vector<std::pair<std::wstring, std::wstring>>& pairs) {
    std::wstring f;
    for (const auto& p : pairs) {
        f += p.first;
        f += L'\0';
        f += p.second;
        f += L'\0';
    }
    f += L'\0';
    return f;
}

std::wstring outputPath(const std::wstring& shaderPath, const std::wstring& outdir, const std::wstring& ext) {
    size_t slash = shaderPath.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"" : shaderPath.substr(0, slash + 1);
    std::wstring name = shaderPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.erase(dot);
    if (!outdir.empty()) {
        dir = outdir;
        if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    }
    return dir + name + ext;
}

