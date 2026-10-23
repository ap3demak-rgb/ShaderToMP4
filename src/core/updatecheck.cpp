// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "updatecheck.h"
#include "app_info.h"
#include "i18n.h"
#include "json.h"
#include "platform/platform.h"
#include "util.h"
#include <cctype>

const wchar_t* defaultUpdateUrl() { return L"https://api.github.com/repos/ap3demak-rgb/ShaderToMP4/releases/latest"; }

namespace {

std::vector<int> parts(const std::string& v) {
    std::vector<int> out;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    int cur = 0;
    bool any = false;
    for (; i < v.size(); i++) {
        if (isdigit((unsigned char)v[i])) {
            cur = cur * 10 + (v[i] - '0');
            any = true;
        } else if (v[i] == '.') {
            out.push_back(cur);
            cur = 0;
            any = false;
        } else break;  // « 2.2.0-rc1 » : on s'arrete au suffixe
    }
    if (any || out.empty()) out.push_back(cur);
    return out;
}

bool endsWith(const std::wstring& s, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

}  // namespace

int compareVersions(const std::string& a, const std::string& b) {
    std::vector<int> x = parts(a), y = parts(b);
    const size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

bool updateUrlAllowed(const std::wstring& url) {
    if (url.compare(0, 8, L"https://") == 0) return true;
    for (const wchar_t* local : {L"http://127.0.0.1", L"http://localhost"}) {
        const size_t n = wcslen(local);
        if (url.compare(0, n, local) == 0 && (url.size() == n || url[n] == L':' || url[n] == L'/')) return true;
    }
    return false;
}

bool parseRelease(const std::string& json, const std::string& current, UpdateInfo& out) {
    JsonValue root;
    std::string e;
    out.current = current;
    if (!parseJson(json, root, e) || root.kind != JsonValue::Object || root.str("tag_name").empty()) {
        out.err = TX("update.err_parse");
        return false;
    }
    std::string tag = root.str("tag_name");
    out.latest = (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
    out.pageUrl = W(root.str("html_url"));
    out.newer = compareVersions(out.latest, current) > 0;
    const JsonValue* assets = root.get("assets");
    std::wstring setup, zip;
    std::wstring setupUrl, zipUrl;
    if (assets && assets->kind == JsonValue::Array) {
        for (const JsonValue& a : assets->items) {
            const std::wstring name = W(a.str("name")), url = W(a.str("browser_download_url"));
            if (name == L"SHA256SUMS.txt") out.sumsUrl = url;
            else if (endsWith(name, L"-win64-setup.exe")) { setup = name; setupUrl = url; }
            else if (endsWith(name, L"-win64.zip")) { zip = name; zipUrl = url; }
        }
    }
    out.assetName = setup.empty() ? zip : setup;  // l'installeur d'abord, sinon l'archive portable
    out.assetUrl = setup.empty() ? zipUrl : setupUrl;
    out.ok = true;
    return true;
}

std::string findChecksum(const std::string& text, const std::wstring& assetName) {
    const std::string name = U8(assetName);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t sp = line.find_first_of(" \t");
        if (sp != 64) continue;  // SHA-256 : 64 chiffres hexadecimaux
        std::string rest = line.substr(sp);
        rest.erase(0, rest.find_first_not_of(" \t*"));
        if (rest != name) continue;
        std::string hex = line.substr(0, 64);
        for (char& c : hex) c = (char)tolower((unsigned char)c);
        for (char c : hex)
            if (!isxdigit((unsigned char)c)) return "";
        return hex;
    }
    return "";
}

UpdateInfo checkForUpdate(const std::wstring& apiUrl) {
    UpdateInfo info;
    info.current = APP_VER_STR;
    if (!updateUrlAllowed(apiUrl)) {
        info.err = TXF("update.err_scheme", apiUrl.c_str());
        return info;
    }
    std::string body;
    std::wstring err;
    if (!platform::httpGet(apiUrl, [&](const uint8_t* d, size_t n, unsigned long long) { body.append((const char*)d, n); return body.size() < (4u << 20); }, err)) {
        info.err = err;
        return info;
    }
    if (!parseRelease(body, APP_VER_STR, info)) return info;
    if (info.newer && !info.sumsUrl.empty() && updateUrlAllowed(info.sumsUrl)) {
        std::string sums;
        std::wstring e2;
        if (platform::httpGet(info.sumsUrl, [&](const uint8_t* d, size_t n, unsigned long long) { sums.append((const char*)d, n); return sums.size() < (1u << 20); }, e2))
            info.sha256 = findChecksum(sums, info.assetName);
    }
    return info;
}

bool downloadUpdate(const UpdateInfo& info, const std::wstring& dir, const std::function<bool(unsigned long long, unsigned long long)>& progress,
                    std::wstring& savedPath, std::wstring& err) {
    if (info.assetUrl.empty()) {
        err = TX("update.err_no_asset");
        return false;
    }
    if (!updateUrlAllowed(info.assetUrl)) {
        err = TXF("update.err_scheme", info.assetUrl.c_str());
        return false;
    }
    if (info.sha256.empty()) {  // sans somme publiee, on ne telecharge rien : le controle est la raison d'etre de cette fonction
        err = TXF("update.err_no_sum", info.assetName.c_str());
        return false;
    }
    const std::wstring name = info.assetName.substr(info.assetName.find_last_of(L"/\\") == std::wstring::npos ? 0 : info.assetName.find_last_of(L"/\\") + 1);
    platform::makeDir(dir);
    const std::wstring part = dir + L"/" + name + L".part", final_ = dir + L"/" + name;
    FILE* f = platform::openFile(part, "wb");
    if (!f) {
        err = TXF("error.write_failed", part.c_str());
        return false;
    }
    unsigned long long got = 0;
    const bool ok = platform::httpGet(info.assetUrl, [&](const uint8_t* d, size_t n, unsigned long long total) {
        if (fwrite(d, 1, n, f) != n) return false;
        got += n;
        return progress ? progress(got, total) : true;
    }, err);
    fclose(f);
    if (!ok) {
        platform::removeFile(part);
        return false;
    }
    std::string actual;
    if (!platform::sha256File(part, actual) || actual != info.sha256) {
        platform::removeFile(part);
        err = TX("error.sha_mismatch");
        return false;
    }
    if (!platform::moveFile(part, final_, true)) {
        platform::removeFile(part);
        err = TXF("error.write_failed", final_.c_str());
        return false;
    }
    savedPath = final_;
    return true;
}
