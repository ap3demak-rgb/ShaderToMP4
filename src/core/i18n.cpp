// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "i18n.h"
#include "util.h"
#include "platform/platform.h"
#include <set>

namespace i18n {
namespace {

using Table = std::map<std::string, std::wstring>;

struct State {
    std::mutex m;
    std::wstring exeDir;
    std::string lang = "en";
    Table base;     // anglais (repli)
    Table current;  // langue choisie
    std::set<std::string> missing;
} S;

// --- Lecteur JSON minimal : objets imbriqués de chaînes, aplatis en "a.b.c" ------------------------------------
struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    explicit Parser(const std::string& text) : s(text) {}

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    }
    bool fail(const char* what) {
        if (err.empty()) {
            size_t line = 1;
            for (size_t k = 0; k < i && k < s.size(); k++)
                if (s[k] == '\n') line++;
            err = std::string(what) + " (line " + std::to_string(line) + ")";
        }
        return false;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return fail("incomplete \\u sequence");
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return fail("invalid hexadecimal digit");
        }
        return true;
    }
    bool str(std::string& out) {
        out.clear();
        if (i >= s.size() || s[i] != '"') return fail("string expected");
        i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }
    bool object(const std::string& prefix, Table& t) {
        ws();
        if (i >= s.size() || s[i] != '{') return fail("'{' expected");
        i++;
        ws();
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
            ws();
            std::string key;
            if (!str(key)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return fail("':' expected");
            i++;
            ws();
            std::string full = prefix.empty() ? key : prefix + "." + key;
            if (i < s.size() && s[i] == '{') {
                if (!object(full, t)) return false;
            } else {
                std::string val;
                if (!str(val)) return false;
                t[full] = W(val);
            }
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return fail("',' or '}' expected");
        }
    }
};

bool parse(const std::string& text, Table& out, std::string& err) {
    std::string t = text;
    if (t.size() >= 3 && (uint8_t)t[0] == 0xEF && (uint8_t)t[1] == 0xBB && (uint8_t)t[2] == 0xBF) t.erase(0, 3);
    Parser p(t);
    Table tmp;
    if (!p.object("", tmp)) {
        err = p.err;
        return false;
    }
    for (auto& kv : tmp) out[kv.first] = std::move(kv.second);
    return true;
}

bool readEmbedded(const std::string& code, std::string& out) {
    return platform::embeddedData(("lang/" + code + ".json").c_str(), out);
}

// Tables d'une langue : version embarquée, puis fichier lang/<code>.json (qui prime). Verrou S.m déjà pris par l'appelant.
bool loadLanguage(const std::string& code, Table& out) {
    bool any = false;
    std::string text, err;
    if (readEmbedded(code, text)) {
        Table t;
        if (parse(text, t, err)) {
            for (auto& kv : t) out[kv.first] = std::move(kv.second);
            any = true;
        }
    }
    std::wstring path = S.exeDir + L"lang/" + W(code) + L".json";
    if (fileExists(path) && readFile(path, text)) {
        Table t;
        if (parse(text, t, err)) {
            for (auto& kv : t) out[kv.first] = std::move(kv.second);
            any = true;
        }
    }
    return any;
}

std::wstring vformat(const std::wstring& fmt, va_list a) {
    wchar_t b[2048];
#ifdef _WIN32
    int n = _vswprintf_p(b, 2048, fmt.c_str(), a);
#else
    int n = vswprintf(b, 2048, fmt.c_str(), a);
#endif
    if (n < 0) return fmt;
    b[2047] = 0;
    return b;
}

}  // namespace

// Langue du systeme si elle existe (embarquee ou fichier lang/<code>.json), sinon l'anglais
std::string systemLanguage() {
    const std::string sys = platform::systemLanguage();
    for (const auto& c : available())
        if (c == sys) return sys;
    return "en";
}

bool init(const std::wstring& exeDir, const std::string& code) {
    {
        std::lock_guard<std::mutex> l(S.m);
        S.exeDir = exeDir;
        S.base.clear();
        if (!loadLanguage("en", S.base)) return false;  // l'anglais embarqué doit toujours exister
    }
    return setLanguage(code.empty() ? systemLanguage() : code);
}

bool setLanguage(const std::string& code) {
    Table t;
    std::lock_guard<std::mutex> l(S.m);
    if (code != "en" && !loadLanguage(code, t)) return false;
    S.current = code == "en" ? Table() : std::move(t);
    S.lang = code;
    S.missing.clear();
    return true;
}

std::string language() {
    std::lock_guard<std::mutex> l(S.m);
    return S.lang;
}

std::vector<std::string> available() {
    std::set<std::string> codes = {"en", "fr"};
    std::lock_guard<std::mutex> l(S.m);
    std::vector<platform::DirEntry> list;
    platform::listDir(S.exeDir + L"lang", list);
    for (const auto& e : list) {
        std::wstring n = e.name;
        if (e.isDir || n.size() < 6 || n.compare(n.size() - 5, 5, L".json") != 0) continue;
        n.erase(n.size() - 5);
        if (n.size() <= 8) codes.insert(U8(n));
    }
    return std::vector<std::string>(codes.begin(), codes.end());
}

std::wstring tr(const char* key) {
    std::lock_guard<std::mutex> l(S.m);
    auto it = S.current.find(key);
    if (it != S.current.end()) return it->second;
    auto b = S.base.find(key);
    if (b != S.base.end()) return b->second;
    S.missing.insert(key);
    return W(std::string("[") + key + "]");
}

std::string tr8(const char* key) { return U8(tr(key)); }

std::wstring trf(const char* key, ...) {
    std::wstring f = tr(key);
    va_list a;
    va_start(a, key);
    std::wstring r = vformat(f, a);
    va_end(a);
    return r;
}

std::wstring num(double v) {
    std::wstring s = wfmt(L"%g", v);
    if (language() == "fr")
        for (auto& c : s)
            if (c == L'.') c = L',';
    return s;
}

std::wstring fixed(double v, int decimals) {
    wchar_t fmt[16];
    swprintf(fmt, 16, L"%%.%df", decimals);
    std::wstring s = wfmt(fmt, v);
    if (language() == "fr")
        for (auto& c : s)
            if (c == L'.') c = L',';
    return s;
}

std::vector<std::string> missingKeys() {
    std::lock_guard<std::mutex> l(S.m);
    return std::vector<std::string>(S.missing.begin(), S.missing.end());
}

std::vector<std::string> keys(const std::string& code) {
    std::vector<std::string> out;
    Table t;
    {
        std::lock_guard<std::mutex> l(S.m);
        loadLanguage(code, t);
    }
    for (auto& kv : t) out.push_back(kv.first);
    return out;
}

}  // namespace i18n
