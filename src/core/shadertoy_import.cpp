// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "shadertoy_import.h"
#include "i18n.h"
#include "json.h"
#include "util.h"
#include "platform/platform.h"
#include <cwctype>
#include <map>

namespace {

std::string safeName(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_') o += (char)c;
        else if (c == ' ' || c == '.') o += '_';
        else if (c >= 0x80) o += (char)c;  // lettres accentuees conservees (UTF-8)
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o.empty() ? "shader" : o.substr(0, 60);
}

// Nom de passe Shadertoy -> suffixe de fichier / specification de canal
const char* bufferSuffix(const std::string& passName) {
    if (passName == "Buffer A") return "bufferA";
    if (passName == "Buffer B") return "bufferB";
    if (passName == "Buffer C") return "bufferC";
    if (passName == "Buffer D") return "bufferD";
    return nullptr;
}

bool writeText(const std::wstring& path, const std::string& text) {
    FILE* f = platform::openFile(path, "wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

}  // namespace

bool importShadertoyJson(const std::wstring& jsonPath, const std::wstring& outDir, ShadertoyImport& res, std::wstring& err) {
    std::string text;
    if (!readFile(jsonPath, text)) {
        err = TX("error.read_file");
        return false;
    }
    JsonValue doc;
    std::string jerr;
    if (!parseJson(text, doc, jerr)) {
        err = TXF("error.import_json", W(jerr).c_str());
        return false;
    }
    const JsonValue* root = doc.get("Shader") ? doc.get("Shader") : &doc;  // l'API enveloppe l'export dans « Shader »
    const JsonValue* passes = root->get("renderpass");
    if (!passes || passes->kind != JsonValue::Array || passes->items.empty()) {
        err = TX("error.import_norenderpass");
        return false;
    }
    const JsonValue* info = root->get("info");
    const std::string title = info ? info->str("name") : "", author = info ? info->str("username") : "", id = info ? info->str("id") : "";
    const std::string base = safeName(title.empty() ? (id.empty() ? "shader" : id) : title);
    res.title = W(title);
    res.author = W(author);
    res.id = W(id);

    platform::makeDir(outDir);
    const std::wstring stem = outDir + platform::pathSeparator() + W(base);

    // Sortie (id) -> nom de passe : un canal « buffer » pointe vers l'identifiant d'une sortie
    std::map<long long, std::string> outputOwner;
    for (const auto& p : passes->items) {
        const JsonValue* outs = p.get("outputs");
        if (!outs) continue;
        for (const auto& o : outs->items) outputOwner[(long long)o.num("id", -1)] = p.str("name");
    }

    std::string header = "// Shadertoy";
    if (!title.empty()) header += ": " + title;
    if (!author.empty()) header += " - " + author;
    if (!id.empty()) header += " - https://www.shadertoy.com/view/" + id;
    header += "\n";
    res.notes.push_back(TXF("log.import_license", W(title.empty() ? base : title).c_str(), W(author.empty() ? "?" : author).c_str()));

    for (const auto& p : passes->items) {
        const std::string type = p.str("type"), name = p.str("name");
        std::string code = p.str("code");
        std::string suffix;
        if (type == "image") suffix = "";
        else if (type == "common") suffix = ".common";
        else if (type == "buffer" && bufferSuffix(name)) suffix = std::string(".") + bufferSuffix(name);
        else if (type == "cubemap") suffix = ".cubemapA";
        else if (type == "sound") suffix = ".sound";
        else {
            res.notes.push_back(TXF("log.import_note_pass", W(name).c_str()));  // « Sound » et autres : ignores
            continue;
        }
        std::string lines;
        const JsonValue* inputs = p.get("inputs");
        if (inputs && type != "common") {
            for (const auto& in : inputs->items) {
                const int ch = (int)in.num("channel", -1);
                if (ch < 0 || ch > 3) continue;
                const std::string ctype = in.str("ctype"), src = in.str("src");
                const JsonValue* sm = in.get("sampler");
                const std::string filter = sm ? sm->str("filter") : "", wrap = sm ? sm->str("wrap") : "", vflip = sm ? sm->str("vflip") : "";
                std::string spec;
                bool texture = false;
                if (ctype == "buffer") {
                    auto it = outputOwner.find((long long)in.num("id", -1));
                    const char* s = it != outputOwner.end() ? bufferSuffix(it->second) : nullptr;
                    if (!s) {  // repli : « buffer00 » a « buffer03 » dans le nom du media
                        const size_t k = src.find("buffer0");
                        if (k != std::string::npos && k + 7 < src.size() && src[k + 7] >= '0' && src[k + 7] <= '3')
                            s = bufferSuffix(std::string("Buffer ") + (char)('A' + (src[k + 7] - '0')));
                    }
                    if (s) spec = s;
                } else if (ctype == "cubemap") {
                    if (src.find("previz/cubemap") != std::string::npos) {
                        spec = "cubeA";
                    } else {
                        spec = "cube:sky";
                        res.notes.push_back(TXF("log.import_note_media", ch, W(name).c_str(), W(src).c_str(), ch));
                    }
                } else if (ctype == "texture") {
                    spec = "noise";
                    texture = true;
                    res.notes.push_back(TXF("log.import_note_media", ch, W(name).c_str(), W(src).c_str(), ch));
                } else if (ctype == "volume") {
                    spec = "volume:noise";
                    res.notes.push_back(TXF("log.import_note_media", ch, W(name).c_str(), W(src).c_str(), ch));
                } else if (ctype == "music" || ctype == "musicstream" || ctype == "mic") {
                    spec = "audio";
                    res.notes.push_back(TXF("log.import_note_audio", ch, W(name).c_str()));
                } else {
                    res.notes.push_back(TXF("log.import_note_unsupported", ch, W(name).c_str(), W(ctype).c_str()));  // webcam, clavier, video de Shadertoy
                }
                if (spec.empty()) continue;
                const std::string key = "// iChannel" + std::to_string(ch);
                lines += key + "=" + spec + "\n";
                if (!filter.empty() && (filter == "nearest" || filter == "linear" || filter == "mipmap")) lines += key + ".filter=" + filter + "\n";
                if (!wrap.empty() && (wrap == "clamp" || wrap == "repeat")) lines += key + ".wrap=" + wrap + "\n";
                if (texture && !vflip.empty()) lines += key + ".vflip=" + (vflip == "true" ? "1" : "0") + "\n";
            }
        }
        const std::wstring file = stem + W(suffix) + L".glsl";
        if (!writeText(file, header + lines + code + "\n")) {
            err = TXF("error.write_failed", file.c_str());
            return false;
        }
        res.files.push_back(file);
        if (type == "image") res.mainFile = file;
    }
    if (res.mainFile.empty()) {
        err = TX("error.import_norenderpass");
        return false;
    }
    return true;
}

std::wstring resolveShaderInput(const std::wstring& exeDir, const std::wstring& path, const std::function<void(const std::wstring&)>& log,
                                std::wstring& err) {
    std::wstring ext = path.size() > 5 ? path.substr(path.size() - 5) : L"";
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (ext != L".json") return path;
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    name.erase(name.size() - 5);
    const std::wstring root = exeDir + L"shadertoy";
    platform::makeDir(root);
    ShadertoyImport r;
    if (!importShadertoyJson(path, root + platform::pathSeparator() + W(safeName(U8(name))), r, err)) return L"";
    if (log) {
        log(TXF("log.import_done", r.title.empty() ? name.c_str() : r.title.c_str(), r.mainFile.c_str()));
        for (const auto& n : r.notes) log(L"  " + n);
    }
    return r.mainFile;
}
