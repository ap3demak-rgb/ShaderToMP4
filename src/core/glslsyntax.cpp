// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "glslsyntax.h"
#include "settings.h"
#include "util.h"
#include <set>

namespace {

const std::set<std::wstring>& keywords() {
    static const std::set<std::wstring> s = {L"if", L"else", L"for", L"while", L"do", L"break", L"continue", L"return", L"discard", L"switch", L"case",
                                             L"default", L"struct", L"const", L"in", L"out", L"inout", L"uniform", L"layout", L"flat", L"smooth",
                                             L"precision", L"highp", L"mediump", L"lowp", L"true", L"false", L"attribute", L"varying", L"centroid",
                                             L"invariant", L"noperspective", L"define", L"include"};
    return s;
}

const std::set<std::wstring>& types() {
    static const std::set<std::wstring> s = {L"void", L"bool", L"int", L"uint", L"float", L"double", L"vec2", L"vec3", L"vec4", L"ivec2", L"ivec3",
                                             L"ivec4", L"uvec2", L"uvec3", L"uvec4", L"bvec2", L"bvec3", L"bvec4", L"dvec2", L"dvec3", L"dvec4",
                                             L"mat2", L"mat3", L"mat4", L"mat2x2", L"mat2x3", L"mat2x4", L"mat3x2", L"mat3x3", L"mat3x4", L"mat4x2",
                                             L"mat4x3", L"mat4x4", L"sampler2D", L"sampler3D", L"samplerCube", L"sampler2DArray", L"isampler2D",
                                             L"usampler2D", L"sampler2DShadow"};
    return s;
}

const std::set<std::wstring>& functions() {
    static const std::set<std::wstring> s = {
        L"radians", L"degrees", L"sin", L"cos", L"tan", L"asin", L"acos", L"atan", L"sinh", L"cosh", L"tanh", L"asinh", L"acosh", L"atanh",
        L"pow", L"exp", L"log", L"exp2", L"log2", L"sqrt", L"inversesqrt", L"abs", L"sign", L"floor", L"trunc", L"round", L"roundEven", L"ceil",
        L"fract", L"mod", L"modf", L"min", L"max", L"clamp", L"mix", L"step", L"smoothstep", L"isnan", L"isinf", L"length", L"distance", L"dot",
        L"cross", L"normalize", L"faceforward", L"reflect", L"refract", L"matrixCompMult", L"outerProduct", L"transpose", L"determinant",
        L"inverse", L"lessThan", L"lessThanEqual", L"greaterThan", L"greaterThanEqual", L"equal", L"notEqual", L"any", L"all", L"not",
        L"texture", L"textureLod", L"textureGrad", L"textureSize", L"texelFetch", L"textureProj", L"textureOffset", L"texture2D", L"textureCube",
        L"dFdx", L"dFdy", L"fwidth", L"floatBitsToInt", L"intBitsToFloat", L"packUnorm4x8", L"unpackUnorm4x8", L"mainImage", L"mainSound",
        L"mainVR", L"mainCubemap"};
    return s;
}

const std::set<std::wstring>& shadertoyUniforms() {
    static const std::set<std::wstring> s = {L"iResolution", L"iTime", L"iTimeDelta", L"iFrame", L"iMouse", L"iDate", L"iSampleRate", L"iChannel0",
                                             L"iChannel1", L"iChannel2", L"iChannel3", L"iChannelTime", L"iChannelResolution", L"fragCoord",
                                             L"fragColor", L"iFrameRate", L"iBeat", L"iBeatCount", L"iBPM", L"iBass", L"iMid", L"iTreble", L"iEnergy", L"iOnset", L"iProgress"};
    return s;
}

bool identStart(wchar_t c) { return c == L'_' || (c < 128 && iswalpha(c)); }
bool identPart(wchar_t c) { return c == L'_' || (c < 128 && iswalnum(c)); }

// « // cle=valeur », « // uniform ... », « // key ... » : commentaires que le programme lit
bool isDirective(const std::wstring& line) {
    size_t i = 2;
    while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) i++;
    const std::wstring body = line.substr(i);
    if (body.compare(0, 8, L"uniform ") == 0 || body.compare(0, 4, L"key ") == 0) return true;
    const size_t eq = body.find(L'=');
    if (eq == std::wstring::npos || eq == 0) return false;
    std::wstring key = body.substr(0, eq);
    while (!key.empty() && (key.back() == L' ' || key.back() == L'\t')) key.pop_back();
    return isSettingKey(lower(U8(key)));
}

}  // namespace

std::vector<SyntaxSpan> highlightGlsl(const std::wstring& t) {
    std::vector<SyntaxSpan> out;
    const int n = (int)t.size();
    auto add = [&](int a, int b, SyntaxKind k) {
        if (b > a) out.push_back({a, b - a, k});
    };
    bool lineStart = true;  // seulement des espaces depuis le debut de la ligne
    int i = 0;
    while (i < n) {
        const wchar_t c = t[(size_t)i];
        if (c == L'\n') {
            lineStart = true;
            i++;
        } else if (c == L' ' || c == L'\t' || c == L'\r') {
            i++;
        } else if (c == L'/' && i + 1 < n && t[(size_t)i + 1] == L'/') {
            int e = i;
            while (e < n && t[(size_t)e] != L'\n') e++;
            int stop = e;
            if (stop > i && t[(size_t)stop - 1] == L'\r') stop--;
            add(i, stop, isDirective(t.substr((size_t)i, (size_t)(stop - i))) ? SyntaxKind::Directive : SyntaxKind::Comment);
            i = e;
        } else if (c == L'/' && i + 1 < n && t[(size_t)i + 1] == L'*') {
            int e = i + 2;
            while (e + 1 < n && !(t[(size_t)e] == L'*' && t[(size_t)e + 1] == L'/')) e++;
            e = std::min(n, e + 2);
            add(i, e, SyntaxKind::Comment);
            i = e;
            lineStart = false;
        } else if (c == L'#' && lineStart) {
            int e = i;
            while (e < n && t[(size_t)e] != L'\n') {
                if (t[(size_t)e] == L'/' && e + 1 < n && (t[(size_t)e + 1] == L'/' || t[(size_t)e + 1] == L'*')) break;
                e++;
            }
            int stop = e;
            while (stop > i && (t[(size_t)stop - 1] == L'\r' || t[(size_t)stop - 1] == L' ' || t[(size_t)stop - 1] == L'\t')) stop--;
            add(i, stop, SyntaxKind::Preprocessor);
            i = e;
        } else if ((c >= L'0' && c <= L'9') || (c == L'.' && i + 1 < n && t[(size_t)i + 1] >= L'0' && t[(size_t)i + 1] <= L'9')) {
            int e = i + 1;
            while (e < n && (identPart(t[(size_t)e]) || t[(size_t)e] == L'.' ||
                             ((t[(size_t)e] == L'+' || t[(size_t)e] == L'-') && (t[(size_t)e - 1] == L'e' || t[(size_t)e - 1] == L'E') &&
                              !(t[(size_t)i] == L'0' && i + 1 < n && (t[(size_t)i + 1] == L'x' || t[(size_t)i + 1] == L'X')))))
                e++;
            add(i, e, SyntaxKind::Number);
            i = e;
            lineStart = false;
        } else if (identStart(c)) {
            int e = i + 1;
            while (e < n && identPart(t[(size_t)e])) e++;
            const std::wstring w = t.substr((size_t)i, (size_t)(e - i));
            if (keywords().count(w)) add(i, e, SyntaxKind::Keyword);
            else if (types().count(w)) add(i, e, SyntaxKind::Type);
            else if (shadertoyUniforms().count(w)) add(i, e, SyntaxKind::Uniform);
            else if (functions().count(w)) add(i, e, SyntaxKind::Function);
            i = e;
            lineStart = false;
        } else {
            i++;
            lineStart = false;
        }
    }
    return out;
}
