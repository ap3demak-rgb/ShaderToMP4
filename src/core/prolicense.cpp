// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "prolicense.h"
#include "app_info.h"
#include "ed25519.h"
#include "i18n.h"
#include "util.h"
#include <cstring>
#include <ctime>
#include <mutex>

namespace {

// Cle publique de verification des licences (Ed25519). La cle secrete correspondante est dans le generateur prive, jamais dans un depot.
const char kPublicKeyHex[] = "d11627234a5f5695bb1142545ef0c5afe4a881695c871b532c44b2a5ede8a3ef";

bool fromHex(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != n * 2) return false;
    auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < n; i++) {
        const int a = digit(s[2 * i]), b = digit(s[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (uint8_t)(a * 16 + b);
    }
    return true;
}

// AAAA-MM-JJ -> jours depuis 1970-01-01 ; -1 si le texte n'est pas une date
long long parseDate(const std::string& s) {
    int y = 0, m = 0, d = 0;
    char extra = 0;
    if (s.size() != 10 || sscanf(s.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3 || s[4] != '-' || s[7] != '-') return -1;
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
    return daysFromCivil(y, m, d);
}

std::mutex gMutex;
ProLicense gLicense;
std::wstring gDir;
bool gLoaded = false;

struct ProSession {
    std::vector<std::string> used;
};
thread_local ProSession gPro;

}  // namespace

const wchar_t* licenseFileName() { return L"ShaderToMP4.license"; }

long long daysFromCivil(int y, int m, int d) {  // algorithme de H. Hinnant
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

void licensePublicKey(uint8_t out[32]) { fromHex(kPublicKeyHex, out, 32); }

ProLicense checkLicenseText(const std::string& raw, const uint8_t pub[32], int appMajor, long long todayDays) {
    ProLicense li;
    li.state = ProLicenseState::Invalid;
    std::string text;  // fins de ligne CRLF/CR ramenees a LF (le fichier peut avoir transite par un courrier ou un editeur)
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\r') {
            if (i + 1 < raw.size() && raw[i + 1] == '\n') continue;
            text += '\n';
        } else {
            text += raw[i];
        }
    }
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // marque d'ordre des octets d'un editeur
    const size_t at = text.rfind("signature=");
    if (at == std::string::npos || (at != 0 && text[at - 1] != '\n')) return li;
    std::string hex = text.substr(at + 10);
    while (!hex.empty() && (hex.back() == '\n' || hex.back() == ' ')) hex.pop_back();
    uint8_t sig[64];
    if (!fromHex(hex, sig, 64)) return li;  // tronquee ou mal formee
    if (!ed25519::verify((const uint8_t*)text.data(), at, sig, pub)) return li;

    // La signature est exacte : lecture des champs signes
    const std::string payload = text.substr(0, at);
    size_t pos = 0;
    bool header = false;
    while (pos < payload.size()) {
        size_t nl = payload.find('\n', pos);
        if (nl == std::string::npos) nl = payload.size();
        const std::string line = payload.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "ShaderToMP4-License") header = v == "1";
        else if (k == "holder") li.holder = v;
        else if (k == "edition") li.edition = v;
        else if (k == "major") li.major = atoi(v.c_str());
        else if (k == "expires") li.expires = v;
        else if (k == "id") li.id = v;
    }
    if (!header || li.edition != "pro" || li.major <= 0) return li;
    if (li.major != appMajor) {
        li.state = ProLicenseState::OtherMajor;
        return li;
    }
    if (!li.expires.empty() && li.expires != "never") {
        const long long end = parseDate(li.expires);
        if (end < 0) return li;  // date illisible : pas de licence sans fin supposee
        const long long today = todayDays >= 0 ? todayDays : (long long)(std::time(nullptr) / 86400);
        if (today > end) {
            li.state = ProLicenseState::Expired;
            return li;
        }
    }
    li.state = ProLicenseState::Valid;
    return li;
}

ProLicense loadLicense(const std::wstring& dir, const uint8_t* pubKey) {
    std::string text;
    if (!readFile(dir + licenseFileName(), text)) return ProLicense();
    uint8_t pub[32];
    if (pubKey) memcpy(pub, pubKey, 32);
    else licensePublicKey(pub);
    return checkLicenseText(text, pub, APP_VER_MAJOR);
}

void setLicenseForTest(const ProLicense& li) {
    std::lock_guard<std::mutex> l(gMutex);
    gLicense = li;
    gLoaded = true;
}

void initLicense(const std::wstring& exeDir) {
    ProLicense li = loadLicense(exeDir);
    std::lock_guard<std::mutex> l(gMutex);
    gLicense = std::move(li);
    gDir = exeDir;
    gLoaded = true;
}

void ensureLicense(const std::wstring& exeDir) {
    {
        std::lock_guard<std::mutex> l(gMutex);
        if (gLoaded) return;
    }
    initLicense(exeDir);
}

ProLicense currentLicense() {
    {
        std::lock_guard<std::mutex> l(gMutex);
        if (gLoaded) return gLicense;
    }
    return ProLicense();  // jamais charge : sans licence (le chargement se fait au demarrage ou au debut d'un rendu)
}

bool isRegistered() { return currentLicense().state == ProLicenseState::Valid; }
bool showTrialUi() { return !isRegistered(); }

std::wstring licenseStatusText(const ProLicense& li) {
    switch (li.state) {
        case ProLicenseState::Valid: return TXF("lic.status.valid", W(li.holder).c_str());
        case ProLicenseState::Expired: return TXF("lic.status.expired", W(li.expires).c_str());
        case ProLicenseState::Invalid: return TX("lic.status.invalid");
        case ProLicenseState::OtherMajor: return TXF("lic.status.major", li.major, APP_VER_MAJOR);
        default: return TX("lic.status.none");
    }
}

std::wstring licenseInfoText(const ProLicense& li, const std::wstring& exeDir) {
    std::wstring t = licenseStatusText(li) + L"\n";
    t += TXF("lic.info.file", (exeDir + licenseFileName()).c_str()) + L"\n";
    if (li.state == ProLicenseState::Valid || li.state == ProLicenseState::Expired || li.state == ProLicenseState::OtherMajor) {
        t += TXF("lic.info.holder", W(li.holder).c_str()) + L"\n";
        t += TXF("lic.info.edition", W(li.edition).c_str(), li.major) + L"\n";
        t += TXF("lic.info.expires", li.expires == "never" ? TX("lic.never").c_str() : W(li.expires).c_str()) + L"\n";
        t += TXF("lic.info.id", W(li.id).c_str()) + L"\n";
    }
    if (li.state != ProLicenseState::Valid) t += TXF("lic.info.buy", W(APP_PURCHASE_URL).c_str()) + L"\n";
    return t;
}

const std::vector<int>& purchaseAmounts() {
    static const std::vector<int> v = {5, 10, 15};
    return v;
}

std::wstring purchaseUrl(int euros) {
    const auto& v = purchaseAmounts();
    int e = v[0];
    for (int a : v)
        if (a == euros) e = a;
    return std::wstring(APP_PURCHASE_URL_WSTR) + L"/" + std::to_wstring(e) + L"EUR";
}

void proBegin() { gPro.used.clear(); }

bool proCheck(const char* feature) {
    if (isRegistered()) return true;
    for (const auto& f : gPro.used)
        if (f == feature) return false;
    gPro.used.push_back(feature);
    return false;
}

std::vector<std::string> proUsed() { return gPro.used; }

bool watermarkRequired() { return !isRegistered() && !gPro.used.empty(); }
