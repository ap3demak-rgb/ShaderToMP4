// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "filesettings.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"

namespace {

std::string num(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%g", v);
    return b;
}

}  // namespace

std::vector<Override> mainSettingValues(const Settings& s) {
    return {{"width", std::to_string(s.width)},   {"height", std::to_string(s.height)}, {"fps", num(s.fps)},
            {"duration", num(s.duration)},        {"start", num(s.start)},              {"crf", std::to_string(s.crf)},
            {"preset", s.preset},                 {"codec", s.codec},                   {"container", s.container},
            {"depth", std::to_string(s.depth)},   {"ss", std::to_string(s.ss)},         {"outdir", U8(s.outdir)},
            {"bitrate", std::to_string(s.bitrate)}, {"color", s.color},                 {"blur", std::to_string(s.blur)},
            {"loop", num(s.loop)},                {"loopmode", s.loopMode},            {"loopfade", num(s.loopFade)},
            {"timemap", s.timemap},               {"speed", num(s.speed)}, {"audiofollow", s.audioFollow ? "1" : "0"},              {"audio", U8(s.audio)},               {"fadein", num(s.fadein)},
            {"fadeout", num(s.fadeout)},          {"encoder", s.encoder},
            // fenetre Sortie : transparence, HDR, taille, interpolation, volume, texte, fichier, parties
            {"alpha", s.alpha ? "1" : "0"},       {"hdr", s.hdr},                       {"hdrpeak", std::to_string(s.hdrPeak)},
            {"hdrfall", std::to_string(s.hdrFall)}, {"hdrwhite", std::to_string(s.hdrWhite)}, {"interp", num(s.interp)},
            {"scale", s.scale},                   {"scalefilter", s.scaleFilter},       {"loudnorm", num(s.loudnorm)},
            {"title", U8(s.title)},               {"credit", U8(s.credit)},             {"titledur", num(s.titleDur)},
            {"name", U8(s.name)},                 {"exists", s.exists},                 {"chunk", num(s.chunk)},
            {"pbo", std::to_string(s.pbo)},       {"samples", std::to_string(s.samples)}, {"stillformat", s.stillFormat}, {"precheck", s.precheck ? "1" : "0"}, {"tile", std::to_string(s.tile)}, {"printdpi", std::to_string(s.dpi)}, {"shutter", num(s.shutter)},          {"metadata", s.metadata ? "1" : "0"}};
}

std::vector<Override> diffMainSettings(const Settings& base, const Settings& edited) {
    const std::vector<Override> a = mainSettingValues(base), b = mainSettingValues(edited);
    std::vector<Override> out;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (!(a[i] == b[i])) out.push_back(b[i]);
    if (base.variants != edited.variants) {  // les variantes sont une liste : toutes celles du shader, ou « variant= » vide pour n'en vouloir aucune
        for (const std::string& v : edited.variants) out.push_back({"variant", v});
        if (edited.variants.empty()) out.push_back({"variant", ""});
    }
    return out;
}

void copyMainSettings(Settings& dst, const Settings& src) {
    for (const Override& o : mainSettingValues(src)) applySetting(dst, o.key, o.value);
    dst.variants = src.variants;
}

void applyFileOverrides(Settings& s, const std::vector<Override>& list) {
    bool variantsReset = false;
    for (const Override& o : list) {
        if (o.key == "variant") {  // remplace la liste de variantes au lieu de s'y ajouter
            if (!variantsReset) s.variants.clear();
            variantsReset = true;
            if (!trim(o.value).empty()) s.variants.push_back(trim(o.value));
        } else {
            applySetting(s, o.key, o.value);
        }
    }
}

std::wstring fileSettingsPath(const std::wstring& exeDir, const std::wstring& shaderPath) {
    const size_t cut = shaderPath.find_last_of(L"\\/");
    std::wstring stem = cut == std::wstring::npos ? shaderPath : shaderPath.substr(cut + 1);
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem = stem.substr(0, dot);
    std::string key = U8(shaderPath);
    for (auto& c : key) {
        if (c == '\\') c = '/';
        c = (char)tolower((unsigned char)c);
    }
    return exeDir + (exeDir.empty() || exeDir.back() == L'/' || exeDir.back() == L'\\' ? L"" : L"/") + L"jobs/" + stem + L"-" + W(fnvHex(key).substr(0, 8)) + L".ini";
}

bool fileSettingsExist(const std::wstring& exeDir, const std::wstring& shaderPath) { return platform::fileExists(fileSettingsPath(exeDir, shaderPath)); }

std::vector<Override> loadFileSettings(const std::wstring& exeDir, const std::wstring& shaderPath) {
    std::vector<Override> list;
    std::string text;
    if (!readFile(fileSettingsPath(exeDir, shaderPath), text)) return list;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        if (isSettingKey(key)) list.push_back({key, line.substr(eq + 1)});
    }
    return list;
}

bool saveFileSettings(const std::wstring& exeDir, const std::wstring& shaderPath, const std::vector<Override>& list) {
    const std::wstring path = fileSettingsPath(exeDir, shaderPath);
    platform::makeDir(path.substr(0, path.find_last_of(L"\\/")));
    FILE* f = platform::openFile(path, "wb");
    if (!f) return false;
    std::string text = "; " + U8(shaderPath) + "\r\n";  // quel shader : le nom du fichier ne contient qu'une empreinte
    for (const Override& o : list) text += o.key + "=" + o.value + "\r\n";
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return ok;
}

void removeFileSettings(const std::wstring& exeDir, const std::wstring& shaderPath) { platform::removeFile(fileSettingsPath(exeDir, shaderPath)); }

Settings settingsForFile(const Settings& general, const std::wstring& exeDir, const std::wstring& shaderPath) {
    Settings s = general;
    applyFileOverrides(s, loadFileSettings(exeDir, shaderPath));
    return s;
}

bool writeSettingsIntoShader(const std::wstring& shaderPath, const std::vector<Override>& list, std::wstring& err) {
    std::string text;
    if (!readFile(shaderPath, text)) {
        err = TX("error.read_file");
        return false;
    }
    if (text.size() >= 2 && (((uint8_t)text[0] == 0xFF && (uint8_t)text[1] == 0xFE) || ((uint8_t)text[0] == 0xFE && (uint8_t)text[1] == 0xFF))) {
        err = TX("error.shader_utf16");
        return false;
    }
    std::string bom;
    if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) {
        bom = text.substr(0, 3);
        text.erase(0, 3);
    }
    const bool crlf = text.find("\r\n") != std::string::npos;
    const std::string eol = crlf ? "\r\n" : "\n";
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(pos, e - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        pos = e + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();  // le dernier saut de ligne est remis a l'ecriture
    std::vector<std::string> fresh;
    for (const Override& o : list) {
        bool replaced = false;
        for (std::string& l : lines) {  // « // cle = valeur » deja ecrit : remplace
            const std::string t = trim(l);
            if (t.compare(0, 2, "//") != 0) continue;
            const std::string body = trim(t.substr(2));
            const size_t eq = body.find('=');
            if (eq == std::string::npos || lower(trim(body.substr(0, eq))) != lower(o.key)) continue;
            l = "// " + o.key + "=" + o.value;
            replaced = true;
            break;
        }
        if (!replaced) fresh.push_back("// " + o.key + "=" + o.value);
    }
    std::string out = bom;
    for (const std::string& l : fresh) out += l + eol;
    for (const std::string& l : lines) out += l + eol;
    if (!writeFileAtomic(shaderPath, out)) {
        err = TXF("error.write_failed", shaderPath.c_str());
        return false;
    }
    return true;
}
