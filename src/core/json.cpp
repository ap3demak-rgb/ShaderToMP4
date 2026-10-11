// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "json.h"

const JsonValue* JsonValue::get(const std::string& key) const {
    if (kind != Object) return nullptr;
    for (const auto& m : members)
        if (m.first == key) return &m.second;
    return nullptr;
}

std::string JsonValue::str(const std::string& key, const std::string& fallback) const {
    const JsonValue* v = get(key);
    return v && v->kind == String ? v->text : fallback;
}

double JsonValue::num(const std::string& key, double fallback) const {
    const JsonValue* v = get(key);
    if (v && v->kind == Number) return v->number;
    if (v && v->kind == String) return atof(v->text.c_str());  // les exports ecrivent parfois les nombres entre guillemets
    return fallback;
}

namespace {

struct Reader {
    const std::string& s;
    size_t i = 0;
    std::string err;
    int depth = 0;

    explicit Reader(const std::string& text) : s(text) {}

    bool fail(const std::string& m) {
        if (err.empty()) err = m + " (octet " + std::to_string(i) + ")";
        return false;
    }
    void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return fail("sequence \\u incomplete");
        v = 0;
        for (int k = 0; k < 4; k++) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return fail("chiffre hexadecimal attendu");
        }
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("chaine attendue");
        i++;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            const char e = s[i++];
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
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {  // paire de substitution
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("echappement inconnu");
            }
        }
        return fail("chaine non terminee");
    }
    bool value(JsonValue& v) {
        if (++depth > 64) return fail("imbrication trop profonde");
        skip();
        if (i >= s.size()) return fail("valeur attendue");
        bool ok = true;
        const char c = s[i];
        if (c == '{') {
            v.kind = JsonValue::Object;
            i++;
            skip();
            if (i < s.size() && s[i] == '}') {
                i++;
            } else {
                for (;;) {
                    skip();
                    std::string key;
                    if (!string(key)) return false;
                    skip();
                    if (i >= s.size() || s[i] != ':') return fail("« : » attendu");
                    i++;
                    JsonValue member;
                    if (!value(member)) return false;
                    v.members.emplace_back(std::move(key), std::move(member));
                    skip();
                    if (i < s.size() && s[i] == ',') { i++; continue; }
                    if (i < s.size() && s[i] == '}') { i++; break; }
                    return fail("« , » ou « } » attendu");
                }
            }
        } else if (c == '[') {
            v.kind = JsonValue::Array;
            i++;
            skip();
            if (i < s.size() && s[i] == ']') {
                i++;
            } else {
                for (;;) {
                    JsonValue item;
                    if (!value(item)) return false;
                    v.items.push_back(std::move(item));
                    skip();
                    if (i < s.size() && s[i] == ',') { i++; continue; }
                    if (i < s.size() && s[i] == ']') { i++; break; }
                    return fail("« , » ou « ] » attendu");
                }
            }
        } else if (c == '"') {
            v.kind = JsonValue::String;
            ok = string(v.text);
        } else if (s.compare(i, 4, "true") == 0) {
            v.kind = JsonValue::Bool;
            v.boolean = true;
            i += 4;
        } else if (s.compare(i, 5, "false") == 0) {
            v.kind = JsonValue::Bool;
            i += 5;
        } else if (s.compare(i, 4, "null") == 0) {
            i += 4;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            v.number = strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) return fail("nombre attendu");
            v.kind = JsonValue::Number;
            i = (size_t)(end - s.c_str());
        } else {
            return fail("valeur inattendue");
        }
        depth--;
        return ok;
    }
};

}  // namespace

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
    std::string t = text;
    if (t.size() >= 3 && (uint8_t)t[0] == 0xEF && (uint8_t)t[1] == 0xBB && (uint8_t)t[2] == 0xBF) t.erase(0, 3);
    Reader r(t);
    out = JsonValue();
    if (!r.value(out)) {
        error = r.err;
        return false;
    }
    r.skip();
    if (r.i != t.size()) {
        error = "texte apres la valeur (octet " + std::to_string(r.i) + ")";
        return false;
    }
    return true;
}
