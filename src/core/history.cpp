// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "history.h"
#include "filesettings.h"
#include "json.h"
#include "params.h"
#include "util.h"
#include "platform/platform.h"
#include <ctime>

namespace {

std::wstring historyFile(const std::wstring& exeDir) { return exeDir + L"history.jsonl"; }

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

}  // namespace

std::string historyHash(const std::wstring& shaderPath, const Settings& s) {
    std::string text = collectShaderCode(shaderPath);
    for (const Override& o : mainSettingValues(s)) text += "\n" + o.key + "=" + o.value;
    for (const auto& kv : s.pro) text += "\n" + kv.first + "=" + kv.second;
    return fnvHex(text);
}

std::string historyLine(const HistoryEntry& e) {
    char sec[48];
    snprintf(sec, sizeof(sec), "%.3f", e.seconds);
    std::string o = "{\"time\":" + jq(e.time) + ",\"shader\":" + jq(U8(e.shader)) + ",\"output\":" + jq(U8(e.output)) + ",\"seconds\":" + sec + ",\"hash\":" + jq(e.hash) +
                    ",\"result\":" + jq(e.result) + ",\"settings\":[";
    for (size_t i = 0; i < e.settings.size(); i++) o += (i ? "," : "") + jq(e.settings[i].key + "=" + e.settings[i].value);
    return o + "]}";
}

bool parseHistoryLine(const std::string& line, HistoryEntry& out) {
    out = HistoryEntry();
    JsonValue j;
    std::string err;
    if (line.size() > (1u << 20) || !parseJson(line, j, err) || j.kind != JsonValue::Object) return false;
    out.time = j.str("time");
    out.shader = W(j.str("shader"));
    out.output = W(j.str("output"));
    out.seconds = j.num("seconds");
    out.hash = j.str("hash");
    out.result = j.str("result");
    if (out.shader.empty() || out.result.empty()) return false;
    if (const JsonValue* st = j.get("settings"); st && st->kind == JsonValue::Array) {
        for (const JsonValue& it : st->items) {
            if (it.kind != JsonValue::String) continue;
            const size_t eq = it.text.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            const std::string key = lower(trim(it.text.substr(0, eq)));
            if (isSettingKey(key)) out.settings.push_back({key, it.text.substr(eq + 1)});
        }
    }
    return true;
}

static std::vector<std::string> readLines(const std::wstring& path) {
    std::vector<std::string> lines;
    std::string text;
    if (!readFile(path, text)) return lines;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(pos, e - pos);
        pos = e + 1;
        while (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) lines.push_back(std::move(l));
    }
    return lines;
}

static std::map<std::wstring, size_t>& lineCounts() {
    static std::map<std::wstring, size_t> counts;  // lignes deja ecrites par fichier : evite de relire tout le fichier a chaque rendu
    return counts;
}

void appendHistory(const std::wstring& exeDir, const HistoryEntry& e) {
    const std::wstring path = historyFile(exeDir);
    auto& counts = lineCounts();
    if (!counts.count(path)) counts[path] = readLines(path).size();
    if (FILE* f = platform::openFile(path, "ab")) {
        const std::string line = historyLine(e) + "\n";
        fwrite(line.data(), 1, line.size(), f);
        fclose(f);
        counts[path]++;
    }
    if (counts[path] > kHistoryMax + 200) {  // un peu de marge avant de reecrire le fichier sans les plus anciennes lignes
        std::vector<std::string> lines = readLines(path);
        if (lines.size() > kHistoryMax) lines.erase(lines.begin(), lines.begin() + (long)(lines.size() - kHistoryMax));
        std::string text;
        for (const auto& l : lines) text += l + "\n";
        if (writeFileAtomic(path, text)) counts[path] = lines.size();
    }
}

std::vector<HistoryEntry> loadHistory(const std::wstring& exeDir) {
    std::vector<HistoryEntry> out;
    const std::vector<std::string> lines = readLines(historyFile(exeDir));
    for (size_t i = lines.size(); i-- > 0 && out.size() < kHistoryMax;) {
        HistoryEntry e;
        if (parseHistoryLine(lines[i], e)) out.push_back(std::move(e));
    }
    return out;
}

void clearHistory(const std::wstring& exeDir) {
    lineCounts().erase(historyFile(exeDir));
    platform::removeFile(historyFile(exeDir));
}

HistoryEntry makeHistoryEntry(const std::wstring& shaderPath, const Settings& s, const std::wstring& output, double seconds, const std::string& result) {
    HistoryEntry e;
    const time_t now = time(nullptr);
    struct tm tmv;
    localtime_s(&tmv, &now);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tmv);
    e.time = b;
    e.shader = shaderPath;
    e.output = output;
    e.seconds = seconds;
    e.hash = historyHash(shaderPath, s);
    e.result = result;
    e.settings = mainSettingValues(s);
    return e;
}
