// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "keys.h"
#include "util.h"
#include "platform/platform.h"

namespace {

struct Named {
    const char* name;
    int vk;
};
const Named kNamed[] = {{"esc", 0x1B}, {"escape", 0x1B}, {"echap", 0x1B}, {"enter", 0x0D}, {"return", 0x0D}, {"space", 0x20}, {"tab", 0x09}, {"backspace", 0x08},
                        {"delete", 0x2E}, {"del", 0x2E}, {"insert", 0x2D}, {"ins", 0x2D}, {"home", 0x24}, {"end", 0x23}, {"pageup", 0x21}, {"pgup", 0x21},
                        {"pagedown", 0x22}, {"pgdn", 0x22}, {"left", 0x25}, {"up", 0x26}, {"right", 0x27}, {"down", 0x28}};

}  // namespace

const Shortcut* KeyMap::find(const std::string& actionId) const {
    for (const auto& b : binding)
        if (b.first == actionId) return &b.second;
    return nullptr;
}

bool parseShortcut(const std::string& text, Shortcut& out) {
    out = Shortcut();
    std::string t = trim(text);
    if (t.empty()) return false;
    std::vector<std::string> parts;
    size_t pos = 0;
    while (true) {
        const size_t e = t.find('+', pos);
        parts.push_back(trim(t.substr(pos, e == std::string::npos ? std::string::npos : e - pos)));
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        const std::string m = lower(parts[i]);
        if (m == "ctrl" || m == "control" || m == "ctl") out.ctrl = true;
        else if (m == "shift" || m == "maj") out.shift = true;
        else if (m == "alt") out.alt = true;
        else return false;
    }
    const std::string key = lower(parts.back());
    if (key.empty()) return false;
    bool letterLike = false;
    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
        out.vk = key[0] - 'a' + 'A';
        letterLike = true;
    } else if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
        out.vk = key[0];
        letterLike = true;
    } else if (key.size() >= 2 && key.size() <= 3 && key[0] == 'f') {
        const int n = atoi(key.c_str() + 1);
        if (n < 1 || n > 12 || key.find_first_not_of("0123456789", 1) != std::string::npos) return false;
        out.vk = 0x70 + n - 1;
    } else {
        for (const Named& n : kNamed)
            if (key == n.name) out.vk = n.vk;
        if (!out.vk) return false;
    }
    if (letterLike && !out.ctrl && !out.alt) return false;  // une lettre seule se tape dans les champs : jamais un raccourci
    return true;
}

std::string shortcutText(const Shortcut& s) {
    if (s.empty()) return "";
    std::string o;
    if (s.ctrl) o += "Ctrl+";
    if (s.shift) o += "Shift+";
    if (s.alt) o += "Alt+";
    if ((s.vk >= 'A' && s.vk <= 'Z') || (s.vk >= '0' && s.vk <= '9')) o += (char)s.vk;
    else if (s.vk >= 0x70 && s.vk <= 0x7B) o += "F" + std::to_string(s.vk - 0x70 + 1);
    else {
        const char* best = nullptr;
        for (const Named& n : kNamed)
            if (n.vk == s.vk) {
                best = n.name;
                break;
            }
        if (!best) return "";
        std::string nm = best;
        nm[0] = (char)toupper((unsigned char)nm[0]);
        o += nm;
    }
    return o;
}

KeyMap defaultKeys(const std::vector<KeyAction>& actions) {
    KeyMap m;
    for (const KeyAction& a : actions) {
        Shortcut s;
        if (!a.defaultKey.empty()) parseShortcut(a.defaultKey, s);
        m.binding.push_back({a.id, s});
    }
    return m;
}

KeyMap parseKeys(const std::string& text, const std::vector<KeyAction>& actions, std::vector<std::string>& problems) {
    KeyMap m = defaultKeys(actions);
    const KeyMap defaults = m;
    struct Assign {
        std::string id;
        Shortcut sc;  // vide = retirer le raccourci
        int line;
    };
    std::vector<Assign> list;
    size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        lineNo++;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const std::string where = "line " + std::to_string(lineNo) + ": ";
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            problems.push_back(where + "no '=' in '" + line + "'");
            continue;
        }
        const std::string id = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        const size_t cmt = value.find(';');
        if (cmt != std::string::npos) value = trim(value.substr(0, cmt));
        if (!m.find(id)) {
            problems.push_back(where + "unknown action '" + id + "'");
            continue;
        }
        Assign a{id, Shortcut(), lineNo};
        if (!value.empty() && !parseShortcut(value, a.sc)) {
            problems.push_back(where + "invalid shortcut '" + value + "' for '" + id + "'");
            continue;
        }
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Assign& o) { return o.id == id; }), list.end());  // la derniere ligne d'une action l'emporte
        list.push_back(a);
    }
    auto slot = [&](const std::string& id) -> Shortcut& {
        for (auto& b : m.binding)
            if (b.first == id) return b.second;
        return m.binding.front().second;  // inatteignable : l'identifiant a deja ete verifie
    };
    // Les actions citees par le fichier perdent d'abord leur raccourci d'origine : un raccourci libere par une ligne peut servir a une autre, quel que soit l'ordre
    // des lignes. Elles le recoivent ensuite dans l'ordre du fichier ; un raccourci deja pris (par une action non citee ou par une ligne plus haute) est refuse.
    std::sort(list.begin(), list.end(), [](const Assign& x, const Assign& y) { return x.line < y.line; });
    for (const Assign& a : list) slot(a.id) = Shortcut();
    for (const Assign& a : list) {
        if (a.sc.empty()) continue;
        const std::string* taken = nullptr;
        for (const auto& b : m.binding)
            if (b.first != a.id && b.second == a.sc) taken = &b.first;
        if (!taken) {
            slot(a.id) = a.sc;
            continue;
        }
        problems.push_back("line " + std::to_string(a.line) + ": '" + shortcutText(a.sc) + "' is already used by '" + *taken + "', not given to '" + a.id + "'");
        Shortcut back = *defaults.find(a.id);  // le raccourci d'origine, s'il est libre
        for (const auto& b : m.binding)
            if (b.first != a.id && b.second == back) back = Shortcut();
        slot(a.id) = back;
    }
    return m;
}

KeyMap loadKeys(const std::wstring& exeDir, const std::vector<KeyAction>& actions, std::vector<std::string>& problems) {
    std::string text;
    if (!readFile(exeDir + L"keys.ini", text) || text.size() > (1u << 20)) return defaultKeys(actions);
    return parseKeys(text, actions, problems);
}

std::vector<std::pair<std::string, std::string>> keyConflicts(const KeyMap& map) {
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i < map.binding.size(); i++)
        for (size_t j = i + 1; j < map.binding.size(); j++)
            if (!map.binding[i].second.empty() && map.binding[i].second == map.binding[j].second) out.push_back({map.binding[i].first, map.binding[j].first});
    return out;
}

std::string keysTemplate(const KeyMap& map) {
    std::string o = "; ShaderToMP4 keys: action=shortcut (Ctrl+Shift+Alt+key, F1..F12, Esc, Delete...). An empty value removes the shortcut.\r\n";
    for (const auto& b : map.binding) o += b.first + "=" + shortcutText(b.second) + "\r\n";
    return o;
}

std::wstring foldText(const std::wstring& s) {
    static const struct { wchar_t from, to; } kMap[] = {
        {L'à', L'a'}, {L'á', L'a'}, {L'â', L'a'}, {L'ã', L'a'}, {L'ä', L'a'}, {L'å', L'a'}, {L'ç', L'c'}, {L'è', L'e'}, {L'é', L'e'},
        {L'ê', L'e'}, {L'ë', L'e'}, {L'ì', L'i'}, {L'í', L'i'}, {L'î', L'i'}, {L'ï', L'i'}, {L'ñ', L'n'}, {L'ò', L'o'}, {L'ó', L'o'},
        {L'ô', L'o'}, {L'õ', L'o'}, {L'ö', L'o'}, {L'ù', L'u'}, {L'ú', L'u'}, {L'û', L'u'}, {L'ü', L'u'}, {L'ý', L'y'}, {L'ÿ', L'y'}};
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        wchar_t l = c;
        if (l >= L'A' && l <= L'Z') l = (wchar_t)(l + 32);
        else if (l >= 0xC0 && l <= 0xDE && l != 0xD7) l = (wchar_t)(l + 32);  // majuscules latines accentuees : independant de la locale
        else if (l >= 0x80) l = (wchar_t)towlower(l);
        for (const auto& m : kMap)
            if (l == m.from) l = m.to;
        o += l;
    }
    return o;
}

std::vector<int> filterNames(const std::vector<std::wstring>& names, const std::wstring& query) {
    std::vector<std::wstring> words;
    {
        const std::wstring q = foldText(query);
        size_t pos = 0;
        while (pos < q.size()) {
            while (pos < q.size() && q[pos] == L' ') pos++;
            size_t e = q.find(L' ', pos);
            if (e == std::wstring::npos) e = q.size();
            if (e > pos) words.push_back(q.substr(pos, e - pos));
            pos = e;
        }
    }
    std::vector<int> prefix, rest;
    const std::wstring q = foldText(query);
    for (size_t i = 0; i < names.size(); i++) {
        const std::wstring n = foldText(names[i]);
        bool all = true;
        for (const auto& w : words)
            if (n.find(w) == std::wstring::npos) all = false;
        if (!all) continue;
        size_t b = 0;
        while (b < q.size() && q[b] == L' ') b++;
        (!words.empty() && n.compare(0, q.size() - b, q, b, std::wstring::npos) == 0 ? prefix : rest).push_back((int)i);
    }
    prefix.insert(prefix.end(), rest.begin(), rest.end());
    return prefix;
}
