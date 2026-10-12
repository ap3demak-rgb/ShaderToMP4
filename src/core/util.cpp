// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "util.h"
#include "platform/platform.h"

// UTF-8 <-> std::wstring (UTF-16 sous Windows, UTF-32 ailleurs) ; octets invalides remplaces par U+FFFD
std::wstring W(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD;
        int len = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6) { len = 2; cp = c & 0x1F; }
        else if ((c >> 4) == 14) { len = 3; cp = c & 0x0F; }
        else if ((c >> 3) == 30) { len = 4; cp = c & 0x07; }
        if (len > 1) {
            bool ok = i + len <= s.size();
            for (int k = 1; ok && k < len; k++) {
                const unsigned char d = (unsigned char)s[i + k];
                if ((d >> 6) != 2) ok = false;
                else cp = (cp << 6) | (d & 0x3F);
            }
            if (!ok) { cp = 0xFFFD; len = 1; }
        }
        i += len;
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            w += (wchar_t)(0xD800 + (cp >> 10));
            w += (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            w += (wchar_t)cp;
        }
    }
    return w;
}

std::string U8(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (size_t i = 0; i < w.size(); i++) {
        uint32_t cp = (uint32_t)w[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size() && (w[i + 1] & 0xFC00) == 0xDC00)
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)w[++i] - 0xDC00);
        if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }
    return s;
}

bool readFile(const std::wstring& path, std::string& out) { return platform::readFile(path, out); }

bool fileExists(const std::wstring& p) { return platform::fileExists(p); }

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
#ifdef _WIN32
    _vsnwprintf(b, 1023, f, a);
#else
    vswprintf(b, 1023, f, a);
#endif
    va_end(a);
    b[1023] = 0;
    return b;
}

static uint32_t crc32Bytes(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool pngTagSrgb(const std::wstring& path) {
    uint64_t size = 0;
    if (!platform::fileSize(path, size) || size < 45 || size > 1500ull * 1024 * 1024) return false;
    std::string b;
    if (!readFile(path, b)) return false;
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 45 || memcmp(b.data(), sig, 8) != 0 || b.compare(12, 4, "IHDR") != 0) return false;
    // blocs presents avant les donnees : sRGB deja la, ou iCCP (profil integre) -> rien a faire
    size_t pos = 8;
    while (pos + 12 <= b.size()) {
        const uint32_t len = (uint32_t)(uint8_t)b[pos] << 24 | (uint32_t)(uint8_t)b[pos + 1] << 16 | (uint32_t)(uint8_t)b[pos + 2] << 8 | (uint8_t)b[pos + 3];
        const std::string type = b.substr(pos + 4, 4);
        if (type == "sRGB" || type == "iCCP") return true;
        if (type == "IDAT") break;
        pos += 12 + (size_t)len;
    }
    const size_t after = 8 + 25;  // signature + bloc IHDR (13 octets de donnees)
    unsigned char chunk[13] = {0, 0, 0, 1, 's', 'R', 'G', 'B', 0, 0, 0, 0, 0};
    const uint32_t crc = crc32Bytes(chunk + 4, 5);
    chunk[9] = (unsigned char)(crc >> 24);
    chunk[10] = (unsigned char)(crc >> 16);
    chunk[11] = (unsigned char)(crc >> 8);
    chunk[12] = (unsigned char)crc;
    b.insert(after, (const char*)chunk, sizeof(chunk));
    return writeFileAtomic(path, b);
}

bool writeFileAtomic(const std::wstring& path, const std::string& bytes) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = platform::openFile(tmp, "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    if (!ok || !platform::moveFile(tmp, path, true)) {
        platform::removeFile(tmp);
        return false;
    }
    return true;
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
    std::vector<platform::DirEntry> list;
    if (!platform::listDir(dir, list)) return;
    for (const auto& e : list)
        if (!e.isDir) platform::removeFile(dir + platform::pathSeparator() + e.name);
}

void removeTree(const std::wstring& dir) {
    std::vector<platform::DirEntry> list;
    if (platform::listDir(dir, list)) {
        for (const auto& e : list) {
            const std::wstring p = dir + platform::pathSeparator() + e.name;
            if (e.isDir) removeTree(p);
            else platform::removeFile(p);
        }
    }
    platform::removeDir(dir);
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
        if (dir.back() != L'\\' && dir.back() != L'/') dir += platform::pathSeparator();
    }
    return dir + name + ext;
}

