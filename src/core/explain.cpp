// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "explain.h"
#include "i18n.h"
#include <cwctype>
#include <regex>

namespace {

bool has(const std::wstring& s, const wchar_t* pattern) {
    return std::regex_search(s, std::wregex(pattern, std::regex::ECMAScript | std::regex::icase));
}

}  // namespace

bool explainGlslError(const std::wstring& line, ErrorHelp& out) {
    std::wstring low = line;
    for (wchar_t& c : low) c = (wchar_t)std::towlower(c);
    std::wstring ident;
    {
        std::wsmatch m;
        if (std::regex_search(low, m, std::wregex(L"['\"]([a-z_][a-z0-9_]*)['\"]"))) ident = m[1].str();
        std::wsmatch o;  // le nom garde sa casse d'origine
        if (!ident.empty() && std::regex_search(line, o, std::wregex(L"['\"]([A-Za-z_][A-Za-z0-9_]*)['\"]"))) ident = o[1].str();
    }
    std::string id;
    if (low.find(L"mainimage") != std::wstring::npos) id = "main";
    else if (has(low, L"undeclared|not declared|undefined identifier|no such variable|undefined variable")) {  // nom-technique
        if (has(low, L"['\"](texture2d|texturecube|texture2dlod|texturelod|texture3d)['\"]")) id = "texture";
        else if (has(low, L"['\"]ichannel[0-9]+['\"]")) id = "channel";
        else id = "undeclared";
    }
    else if (has(low, L"texture2d|texturecube|sampler")) id = "texture";
    else if (has(low, L"constructor|too many arguments|not enough arguments")) id = "ctor";  // nom-technique
    else if (has(low, L"no matching|cannot resolve|no overload|wrong parameter|wrong number")) id = "overload";  // nom-technique
    else if (has(low, L"cannot convert|can't convert|type mismatch|incompatible|wrong operand|no operator|cannot implicitly|implicit conversion|implicit cast|operand types")) id = "type";  // nom-technique
    else if (has(low, L"syntax error|parse error|unexpected|expecting|missing")) id = "syntax";  // nom-technique
    else if (has(low, L"redefin|already defined|redeclar|duplicate")) id = "redefined";  // nom-technique
    else if (has(low, L"l-value|lvalue|cannot assign|read-only|assign.*const")) id = "assign";  // nom-technique
    else if (has(low, L"swizzle|field selection|invalid field|vector field")) id = "swizzle";  // nom-technique
    else if (has(low, L"division by zero|divide by zero")) id = "divzero";  // nom-technique
    if (id.empty()) return false;
    static const struct { const char* id; const char* help; const char* fix; } texts[] = {
        {"undeclared", "explain.undeclared", "explain.undeclared.fix"}, {"channel", "explain.channel", "explain.channel.fix"},
        {"texture", "explain.texture", "explain.texture.fix"},         {"type", "explain.type", "explain.type.fix"},
        {"overload", "explain.overload", "explain.overload.fix"},      {"ctor", "explain.ctor", "explain.ctor.fix"},
        {"syntax", "explain.syntax", "explain.syntax.fix"},            {"main", "explain.main", "explain.main.fix"},
        {"redefined", "explain.redefined", "explain.redefined.fix"},   {"assign", "explain.assign", "explain.assign.fix"},
        {"swizzle", "explain.swizzle", "explain.swizzle.fix"},         {"divzero", "explain.divzero", "explain.divzero.fix"},
    };
    for (const auto& t : texts)
        if (id == t.id) {
            out.id = id;
            out.help = TXF(t.help, ident.c_str());
            out.fix = TXF(t.fix, ident.c_str());
            return true;
        }
    return false;
}
