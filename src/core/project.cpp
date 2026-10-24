// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "project.h"
#include "filesettings.h"
#include "batches.h"
#include "i18n.h"
#include "json.h"
#include "passfiles.h"
#include "util.h"
#include "platform/platform.h"

const wchar_t* const kProjectExt = L".s2mproj";

namespace {

constexpr size_t kMaxProjectBytes = 16u << 20;
constexpr size_t kMaxShaders = 100000;
constexpr size_t kMaxRecent = 10;

std::string jq(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o + "\"";
}

std::wstring slashes(std::wstring p) {
    for (auto& c : p)
        if (c == L'\\') c = L'/';
    return p;
}

std::wstring nativeSlashes(std::wstring p) {
    const wchar_t sep = platform::pathSeparator();
    for (auto& c : p)
        if (c == L'/' || c == L'\\') c = sep;
    return p;
}

bool isAbsolute(const std::wstring& p) {
    if (p.size() >= 2 && p[1] == L':') return true;
    return !p.empty() && (p[0] == L'/' || p[0] == L'\\');
}

std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// Chemin du fichier tel qu'il est ecrit : relatif au dossier du projet s'il s'y trouve, sinon tel quel
std::string relPath(const std::wstring& path, const std::wstring& dir) {
    const std::wstring p = slashes(path);
    std::wstring d = slashes(dir);
    while (!d.empty() && d.back() == L'/') d.pop_back();
    if (!d.empty() && p.size() > d.size() + 1 && p[d.size()] == L'/' && lowerW(p.substr(0, d.size())) == lowerW(d)) return U8(p.substr(d.size() + 1));
    return U8(p);
}

std::wstring absPath(const std::string& stored, const std::wstring& dir) {
    const std::wstring p = W(stored);
    if (p.empty() || isAbsolute(p)) return nativeSlashes(p);
    std::wstring d = slashes(dir);
    while (!d.empty() && d.back() == L'/') d.pop_back();
    return nativeSlashes(d + L"/" + p);
}

// Les reglages dont la valeur est un chemin de fichier ou de dossier
bool pathKey(const std::string& key) { return key == "outdir" || key == "audio" || key == "lut"; }

std::string settingLine(const Override& o, const std::wstring& dir) {
    if (pathKey(o.key) && !o.value.empty()) return o.key + "=" + relPath(W(o.value), dir);
    return o.key + "=" + o.value;
}

// « cle=valeur » -> Override ; faux si la ligne n'est pas un reglage connu
bool parseSettingLine(const std::string& line, const std::wstring& dir, Override& o) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    o.key = lower(trim(line.substr(0, eq)));
    o.value = line.substr(eq + 1);
    if (!isSettingKey(o.key)) return false;
    if (pathKey(o.key) && !trim(o.value).empty()) o.value = U8(absPath(o.value, dir));
    return true;
}

std::string stringArray(const std::vector<std::string>& v, const char* indent) {
    if (v.empty()) return "[]";
    std::string o = "[\n";
    for (size_t i = 0; i < v.size(); i++) o += std::string(indent) + "  " + jq(v[i]) + (i + 1 < v.size() ? ",\n" : "\n");
    return o + indent + "]";
}

std::vector<std::string> settingLines(const std::vector<Override>& list, const std::wstring& dir) {
    std::vector<std::string> out;
    for (const Override& o : list) out.push_back(settingLine(o, dir));
    return out;
}

std::vector<std::string> pathLines(const std::vector<std::wstring>& list, const std::wstring& dir) {
    std::vector<std::string> out;
    for (const auto& p : list) out.push_back(relPath(p, dir));
    return out;
}

// Medias cites par les commentaires `// iChannelN=...` du shader : fichier, ou fichier apres un prefixe (video:, audio:, cube:, volume:, source:)
std::vector<std::wstring> mediaOf(const std::wstring& shaderPath) {
    std::vector<std::wstring> out;
    std::string text;
    if (!readFile(shaderPath, text) || text.size() > (64u << 20)) return out;
    Settings s;
    parseLines(s, text, true);
    const std::wstring dir = shaderPath.substr(0, shaderPath.find_last_of(L"\\/") == std::wstring::npos ? 0 : shaderPath.find_last_of(L"\\/"));
    for (const std::wstring& spec0 : s.channel) {
        std::wstring spec = spec0;
        if (spec.empty()) continue;
        const std::wstring low = lowerW(spec);
        if (low == L"noise" || low == L"grid" || low == L"audio" || low == L"keyboard" || low == L"cubea" || low.rfind(L"buffer", 0) == 0) continue;
        const size_t colon = spec.find(L':');
        if (colon != std::wstring::npos && colon > 1) spec = spec.substr(colon + 1);  // « video:x.mp4 » ; « C:\x.png » garde sa lettre de lecteur
        const size_t at = spec.rfind(L'@');
        if (at != std::wstring::npos && at > 0 && spec.find_first_not_of(L"0123456789", at + 1) == std::wstring::npos) spec = spec.substr(0, at);
        const std::wstring l2 = lowerW(spec);
        if (spec.empty() || l2 == L"noise" || l2 == L"sky" || spec.find(L'%') != std::wstring::npos) continue;
        out.push_back(isAbsolute(spec) || dir.empty() ? nativeSlashes(spec) : absPath(U8(spec), dir));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool listOfStrings(const JsonValue* v, std::vector<std::string>& out) {
    out.clear();
    if (!v) return true;
    if (v->kind != JsonValue::Array) return false;
    for (const auto& it : v->items) {
        if (it.kind != JsonValue::String) return false;
        out.push_back(it.text);
    }
    return true;
}

// Ancien format (sans « version ») : « shaders » = liste de chemins, « settings » = objet cle -> valeur
void migrateV0(const JsonValue& root, const std::wstring& dir, Project& out) {
    if (const JsonValue* sh = root.get("shaders"); sh && sh->kind == JsonValue::Array) {
        for (const auto& it : sh->items) {
            if (it.kind != JsonValue::String || it.text.empty()) continue;
            ProjectShader ps;
            ps.path = absPath(it.text, dir);
            out.shaders.push_back(ps);
            if (out.shaders.size() >= kMaxShaders) break;
        }
    }
    if (const JsonValue* st = root.get("settings"); st && st->kind == JsonValue::Object) {
        for (const auto& kv : st->members) {
            std::string value = kv.second.text;
            if (kv.second.kind == JsonValue::Number) {
                char b[48];
                snprintf(b, sizeof(b), "%g", kv.second.number);
                value = b;
            } else if (kv.second.kind == JsonValue::Bool) {
                value = kv.second.boolean ? "1" : "0";
            } else if (kv.second.kind != JsonValue::String) {
                continue;
            }
            Override o;
            if (parseSettingLine(kv.first + "=" + value, dir, o)) out.general.push_back(o);
        }
    }
}

}  // namespace

std::vector<Override> projectGeneral(const Settings& s) {
    std::vector<Override> out;
    const std::string text = iniText(s);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string value = line.substr(eq + 1);
        const size_t c = value.find("      ;");  // commentaire de fin de ligne ecrit par iniText
        if (c != std::string::npos) value = value.substr(0, c);
        out.push_back({lower(trim(line.substr(0, eq))), value});
    }
    return out;
}

Settings projectSettings(const std::vector<Override>& general, const Settings& base) {
    Settings s = base;
    std::string text;
    for (const Override& o : general) text += o.key + "=" + o.value + "\n";
    parseLines(s, text, false);
    return s;
}

void projectFiles(const std::wstring& shaderPath, std::vector<std::wstring>& passes, std::vector<std::wstring>& media) {
    passes.clear();
    for (const PassTab& t : passTabs(shaderPath))
        if (t.exists && t.id != "image") passes.push_back(nativeSlashes(t.path));
    media = mediaOf(shaderPath);
}

Project captureProject(const std::wstring& exeDir, const std::wstring& name, const Settings& general, const std::vector<std::wstring>& shaders,
                       const std::map<std::wstring, int>& batches, const std::map<std::wstring, int>& priorities) {
    Project p;
    p.name = name;
    p.general = projectGeneral(general);
    for (const std::wstring& path : shaders) {
        ProjectShader ps;
        ps.path = path;
        ps.batch = batchOf(batches, path);
        if (const auto it = priorities.find(path); it != priorities.end()) ps.priority = std::max(-1, std::min(1, it->second));
        if (fileSettingsExist(exeDir, path)) {
            ps.ownSettings = true;
            ps.own = loadFileSettings(exeDir, path);
        }
        projectFiles(path, ps.passes, ps.media);
        p.shaders.push_back(std::move(ps));
    }
    return p;
}

std::string projectToJson(const Project& p, const std::wstring& dir) {
    std::string o = "{\n  \"format\": \"ShaderToMP4 project\",\n  \"version\": " + std::to_string(p.version) + ",\n  \"name\": " + jq(U8(p.name)) + ",\n";
    o += "  \"general\": " + stringArray(settingLines(p.general, dir), "  ") + ",\n  \"shaders\": ";
    if (p.shaders.empty()) {
        o += "[]\n}\n";
        return o;
    }
    o += "[\n";
    for (size_t i = 0; i < p.shaders.size(); i++) {
        const ProjectShader& s = p.shaders[i];
        o += "    {\n      \"path\": " + jq(relPath(s.path, dir));
        if (s.batch > 0) o += ",\n      \"batch\": " + std::to_string(s.batch);
        if (s.priority != 0) o += ",\n      \"priority\": " + std::to_string(s.priority);
        if (s.ownSettings) o += ",\n      \"settings\": " + stringArray(settingLines(s.own, dir), "      ");
        if (!s.passes.empty()) o += ",\n      \"passes\": " + stringArray(pathLines(s.passes, dir), "      ");
        if (!s.media.empty()) o += ",\n      \"media\": " + stringArray(pathLines(s.media, dir), "      ");
        o += "\n    }";
        o += i + 1 < p.shaders.size() ? ",\n" : "\n";
    }
    return o + "  ]\n}\n";
}

bool projectFromJson(const std::string& text, const std::wstring& dir, Project& out, std::string& err) {
    out = Project();
    if (text.size() > kMaxProjectBytes) {
        err = U8(TX("project.err.big"));
        return false;
    }
    JsonValue root;
    std::string jerr;
    if (!parseJson(text, root, jerr)) {
        err = U8(TXF("project.err.json", W(jerr).c_str()));
        return false;
    }
    if (root.kind != JsonValue::Object) {
        err = U8(TX("project.err.kind"));
        return false;
    }
    const JsonValue* ver = root.get("version");
    const int version = ver && ver->kind == JsonValue::Number ? (int)ver->number : 0;
    if (version > kProjectVersion) {
        err = U8(TXF("project.err.newer", version));
        return false;
    }
    out.name = W(root.str("name"));
    if (version < 1) {
        migrateV0(root, dir, out);
        out.version = kProjectVersion;
        return true;
    }
    if (root.str("format") != "ShaderToMP4 project") {
        err = U8(TX("project.err.kind"));
        return false;
    }
    std::vector<std::string> lines;
    if (!listOfStrings(root.get("general"), lines)) {
        err = U8(TXF("project.err.field", L"general"));
        return false;
    }
    for (const std::string& l : lines) {
        Override o;
        if (parseSettingLine(l, dir, o)) out.general.push_back(o);  // un reglage inconnu (projet d'une version plus recente du meme format) est ignore
    }
    const JsonValue* sh = root.get("shaders");
    if (sh && sh->kind != JsonValue::Array) {
        err = U8(TXF("project.err.field", L"shaders"));
        return false;
    }
    if (sh) {
        for (const JsonValue& it : sh->items) {
            if (out.shaders.size() >= kMaxShaders) break;
            if (it.kind != JsonValue::Object || it.str("path").empty()) {
                err = U8(TXF("project.err.field", L"shaders"));
                return false;
            }
            ProjectShader ps;
            ps.path = absPath(it.str("path"), dir);
            const int b = (int)it.num("batch", 0);
            ps.batch = b >= 1 && b <= kBatchCount ? b : 0;
            ps.priority = std::max(-1, std::min(1, (int)it.num("priority", 0)));
            if (const JsonValue* st = it.get("settings")) {
                if (!listOfStrings(st, lines)) {
                    err = U8(TXF("project.err.field", L"settings"));
                    return false;
                }
                ps.ownSettings = true;
                for (const std::string& l : lines) {
                    Override o;
                    if (parseSettingLine(l, dir, o)) ps.own.push_back(o);
                }
            }
            for (const char* key : {"passes", "media"}) {
                if (!listOfStrings(it.get(key), lines)) {
                    err = U8(TXF("project.err.field", W(key).c_str()));
                    return false;
                }
                for (const std::string& l : lines) (std::string(key) == "passes" ? ps.passes : ps.media).push_back(absPath(l, dir));
            }
            out.shaders.push_back(std::move(ps));
        }
    }
    return true;
}

static std::wstring dirOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? L"." : path.substr(0, cut);
}

bool saveProject(const std::wstring& path, const Project& p, std::wstring& err) {
    if (!writeFileAtomic(path, projectToJson(p, dirOf(path)))) {
        err = TXF("project.err.write", path.c_str());
        return false;
    }
    return true;
}

bool loadProject(const std::wstring& path, Project& out, std::wstring& err) {
    std::string text;
    if (!readFile(path, text)) {
        err = TXF("project.err.read", path.c_str());
        return false;
    }
    std::string e;
    if (!projectFromJson(text, dirOf(path), out, e)) {
        err = W(e);
        return false;
    }
    if (out.name.empty()) {
        const size_t cut = path.find_last_of(L"\\/");
        std::wstring n = cut == std::wstring::npos ? path : path.substr(cut + 1);
        const size_t dot = n.find_last_of(L'.');
        out.name = dot == std::wstring::npos ? n : n.substr(0, dot);
    }
    return true;
}

std::vector<std::wstring> projectMissing(const Project& p) {
    std::vector<std::wstring> out;
    auto check = [&](const std::wstring& f) {
        if (!f.empty() && !fileExists(f)) out.push_back(f);
    };
    for (const ProjectShader& s : p.shaders) {
        check(s.path);
        for (const auto& f : s.passes) check(f);
        for (const auto& f : s.media) check(f);
    }
    return out;
}

Settings applyProject(const std::wstring& exeDir, const Project& p, const Settings& base, std::map<std::wstring, int>& batches, std::map<std::wstring, int>* priorities) {
    batches.clear();
    if (priorities) priorities->clear();
    for (const ProjectShader& s : p.shaders) {
        if (s.ownSettings) saveFileSettings(exeDir, s.path, s.own);
        else removeFileSettings(exeDir, s.path);
        if (s.batch > 0) batches[s.path] = s.batch;
        if (priorities && s.priority != 0) (*priorities)[s.path] = s.priority;
    }
    saveBatches(exeDir, batches);
    return projectSettings(p.general, base);
}

static std::wstring recentFile(const std::wstring& exeDir) { return exeDir + L"jobs/recent_projects.txt"; }

std::vector<std::wstring> recentProjects(const std::wstring& exeDir) {
    std::vector<std::wstring> out;
    std::string text;
    if (!readFile(recentFile(exeDir), text)) return out;
    size_t pos = 0;
    while (pos < text.size() && out.size() < kMaxRecent) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::wstring p = W(trim(text.substr(pos, e - pos)));
        pos = e + 1;
        if (!p.empty() && fileExists(p)) out.push_back(p);
    }
    return out;
}

void addRecentProject(const std::wstring& exeDir, const std::wstring& path) {
    std::vector<std::wstring> list = recentProjects(exeDir);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const std::wstring& p) { return lowerW(slashes(p)) == lowerW(slashes(path)); }), list.end());
    list.insert(list.begin(), path);
    if (list.size() > kMaxRecent) list.resize(kMaxRecent);
    std::string text;
    for (const auto& p : list) text += U8(p) + "\n";
    platform::makeDir(exeDir + L"jobs");
    writeFileAtomic(recentFile(exeDir), text);
}
