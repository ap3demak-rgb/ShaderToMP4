// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "params.h"
#include "compat.h"
#include "expr.h"
#include "i18n.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

namespace {

bool isIdent(const std::string& s) {
    if (s.empty() || isdigit((unsigned char)s[0])) return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_') return false;
    return true;
}

// Noms qu'un parametre ne peut pas prendre : variables Shadertoy, internes du programme, mots du langage
bool reservedName(const std::string& n) {
    static const char* const kWords[] = {
        "iResolution", "iTime", "iTimeDelta", "iFrame", "iFrameRate", "iMouse", "iDate", "iSampleRate", "iChannel0", "iChannel1", "iChannel2",
        "iChannel3", "iChannelTime", "iChannelResolution", "main", "mainImage", "mainVR", "mainSound", "mainCubemap", "texture", "uniform",
        "float", "int", "bool", "void", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "mat2", "mat3", "mat4", "sampler2D", "sampler3D",
        "samplerCube", "in", "out", "inout", "const", "if", "else", "for", "while", "do", "return", "break", "continue", "discard", "true",
        "false", "struct", "layout", "precision", "highp", "mediump", "lowp", "iBeat", "iBeatCount", "iBPM", "iBass", "iMid", "iTreble", "iEnergy", "iOnset"};
    for (const char* w : kWords)
        if (n == w) return true;
    return n.compare(0, 3, "st_") == 0 || n.compare(0, 3, "gl_") == 0;
}

bool toDouble(const std::string& text, double& out) {
    const std::string t = trim(text);
    if (t.empty()) return false;
    const std::string l = lower(t);
    if (l == "true" || l == "on" || l == "yes") { out = 1; return true; }
    if (l == "false" || l == "off" || l == "no") { out = 0; return true; }
    char* end = nullptr;
    out = strtod(t.c_str(), &end);
    return *end == 0 && std::isfinite(out);
}

// « 0.5 », « 0.5, 0.2 », « #ff8800 », « true » selon le type -> n composantes. Faux si invalide.
bool parseValue(const std::string& type, int n, const std::string& textIn, double out[4]) {
    const std::string text = trim(textIn);
    if (type == "color" && !text.empty() && text[0] == '#') {
        std::string h = text.substr(1);
        if (h.size() == 3) h = std::string({h[0], h[0], h[1], h[1], h[2], h[2]});
        if (h.size() != 6) return false;
        for (char c : h)
            if (!isxdigit((unsigned char)c)) return false;
        for (int i = 0; i < 3; i++) out[i] = strtol(h.substr((size_t)i * 2, 2).c_str(), nullptr, 16) / 255.0;
        return true;
    }
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(',', pos);
        if (e == std::string::npos) e = text.size();
        parts.push_back(text.substr(pos, e - pos));
        pos = e + 1;
    }
    if ((int)parts.size() != n) return false;
    for (int i = 0; i < n; i++)
        if (!toDouble(parts[(size_t)i], out[i])) return false;
    return true;
}

// « iBass * 0.5 + 0.2 attack=0.02 release=0.3 threshold=0.1 smooth=0.1 » : l'expression, puis ses reglages (attack, release, smooth = les deux, threshold)
bool splitExprOptions(const std::string& textIn, std::string& expr, double& attack, double& release, double& threshold) {
    std::string t = trim(textIn);
    attack = release = threshold = 0;
    for (;;) {
        const size_t sp = t.find_last_of(" \t");
        if (sp == std::string::npos) break;
        const std::string tok = t.substr(sp + 1);
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) break;
        const std::string key = lower(tok.substr(0, eq));
        double v = 0;
        if ((key != "attack" && key != "release" && key != "smooth" && key != "threshold") || !toDouble(tok.substr(eq + 1), v) || v < 0) break;
        if (key == "attack" || key == "smooth") attack = std::min(v, 30.0);
        if (key == "release" || key == "smooth") release = std::min(v, 30.0);
        if (key == "threshold") threshold = std::min(v, 0.99);
        t = trim(t.substr(0, sp));
    }
    expr = t;
    return !expr.empty();
}

// Declaration globale « float iBass ... » ecrite par l'auteur : le nom est pris, le programme ne declare pas le sien
bool declaredGlobally(const std::string& stripped, const std::string& name) {
    static const char* const kTypes[] = {"float", "int", "uint", "bool", "vec2", "vec3", "vec4", "const", "uniform", "highp", "mediump", "lowp"};
    int depth = 0;
    std::string prev;
    size_t prevEnd = 0;
    for (size_t i = 0; i < stripped.size();) {
        const char c = stripped[i];
        if (c == '{') depth++;
        else if (c == '}') depth = std::max(0, depth - 1);
        if (isalpha((unsigned char)c) || c == '_') {
            size_t e = i;
            while (e < stripped.size() && (isalnum((unsigned char)stripped[e]) || stripped[e] == '_')) e++;
            const std::string id = stripped.substr(i, e - i);
            if (id == name && depth == 0 && !prev.empty()) {
                bool gap = true;
                for (size_t q = prevEnd; q < i; q++)
                    if (!isspace((unsigned char)stripped[q])) gap = false;
                size_t nx = e;
                while (nx < stripped.size() && isspace((unsigned char)stripped[nx])) nx++;
                const char after = nx < stripped.size() ? stripped[nx] : ';';
                bool isType = false;
                for (const char* ty : kTypes)
                    if (prev == ty) isType = true;
                if (gap && isType && (after == '=' || after == ';' || after == '[')) return true;
            }
            prev = id;
            prevEnd = e;
            i = e;
            continue;
        }
        i++;
    }
    return false;
}

// Mot entier `name` dans le code (commentaires retires)
bool usesWord(const std::string& stripped, const std::string& name) {
    for (size_t p = stripped.find(name); p != std::string::npos; p = stripped.find(name, p + 1)) {
        const bool before = p > 0 && (isalnum((unsigned char)stripped[p - 1]) || stripped[p - 1] == '_');
        const bool after = p + name.size() < stripped.size() && (isalnum((unsigned char)stripped[p + name.size()]) || stripped[p + name.size()] == '_');
        if (!before && !after) return true;
    }
    return false;
}

int componentsOf(const std::string& type) {
    if (type == "float" || type == "int" || type == "bool") return 1;
    if (type == "vec2") return 2;
    if (type == "vec3" || type == "color") return 3;
    if (type == "vec4") return 4;
    return 0;
}

void clampValue(const ParamInfo& p, double v[4]) {
    for (int i = 0; i < p.n; i++) {
        if (p.type == "bool") v[i] = v[i] >= 0.5 ? 1 : 0;
        else if (p.type == "color") v[i] = std::min(1.0, std::max(0.0, v[i]));
        else if (p.hasRange) v[i] = std::min(p.hi, std::max(p.lo, v[i]));
        if (p.type == "int") v[i] = std::floor(v[i] + 0.5);
    }
}

// « uniform float vitesse = 1.0 [0..5] » (le « uniform » deja retire de body)
bool parseDecl(const std::string& body, ParamInfo& p, std::wstring& err) {
    std::string rest = trim(body);
    size_t sp = rest.find_first_of(" \t");
    if (sp == std::string::npos) return false;
    p.type = lower(rest.substr(0, sp));
    p.n = componentsOf(p.type);
    rest = trim(rest.substr(sp));
    size_t end = rest.find_first_of(" \t=[");
    p.name = rest.substr(0, end == std::string::npos ? rest.size() : end);
    rest = end == std::string::npos ? "" : trim(rest.substr(end));
    if (p.n == 0 || !isIdent(p.name)) return false;
    if (reservedName(p.name)) {
        err = TXF("error.param_name_taken", W(p.name).c_str());
        return false;
    }
    std::string range;
    const size_t lb = rest.find('[');
    if (lb != std::string::npos) {
        const size_t rb = rest.find(']', lb);
        if (rb == std::string::npos) return false;
        range = rest.substr(lb + 1, rb - lb - 1);
        rest = trim(rest.substr(0, lb));
    }
    if (!range.empty()) {
        const size_t dots = range.find("..");
        if (dots == std::string::npos || !toDouble(range.substr(0, dots), p.lo) || !toDouble(range.substr(dots + 2), p.hi) || p.hi <= p.lo)
            return false;
        p.hasRange = true;
    }
    if (!rest.empty()) {
        if (rest[0] != '=') return false;
        const std::string def = trim(rest.substr(1));
        if (mentionsRhythmVar(def)) {
            if (p.type != "float" && p.type != "int") return false;
            if (!splitExprOptions(def, p.expr, p.attack, p.release, p.threshold)) return false;
            p.audio = "expr";
        } else if (lower(def).compare(0, 6, "audio.") == 0) {
            p.audio = lower(def.substr(6));
            if ((p.audio != "level" && p.audio != "bass" && p.audio != "mid" && p.audio != "treble") || (p.type != "float" && p.type != "int")) return false;
        } else if (!parseValue(p.type, p.n, def, p.def)) {
            return false;
        }
    } else if (p.hasRange && p.type != "color") {
        for (int i = 0; i < p.n; i++) p.def[i] = p.lo;
    } else if (p.type == "color") {
        p.def[0] = p.def[1] = p.def[2] = 1;
    }
    clampValue(p, p.def);
    for (int i = 0; i < 4; i++) p.value[i] = p.def[i];
    return true;
}

// Mot « uniform <type> <nom> » ecrit par l'auteur du shader dans le code (hors commentaires) : le nom est pris
bool declaredByUser(const std::string& stripped, const std::string& name) {
    for (size_t p = stripped.find("uniform"); p != std::string::npos; p = stripped.find("uniform", p + 1)) {
        if (p > 0 && (isalnum((unsigned char)stripped[p - 1]) || stripped[p - 1] == '_')) continue;
        size_t q = p + 7;
        std::vector<std::string> words;
        while (words.size() < 4 && q < stripped.size() && stripped[q] != ';') {
            while (q < stripped.size() && (isspace((unsigned char)stripped[q]))) q++;
            const size_t s = q;
            while (q < stripped.size() && (isalnum((unsigned char)stripped[q]) || stripped[q] == '_')) q++;
            if (q == s) break;
            words.push_back(stripped.substr(s, q - s));
        }
        for (size_t i = 1; i < words.size(); i++)  // type puis nom (un qualificatif de precision peut s'intercaler)
            if (words[i] == name) return true;
    }
    return false;
}

}  // namespace

void paramRange(const ParamInfo& p, double& lo, double& hi) {
    lo = p.lo;
    hi = p.hi;
    if (!p.hasRange && p.type != "color" && p.type != "bool") {
        lo = std::min(0.0, p.def[0] * 2);
        hi = std::max(1.0, p.def[0] * 2);
    }
}

std::wstring paramsFile(const std::wstring& exeDir, const std::wstring& shaderPath) {
    const size_t cut = shaderPath.find_last_of(L"\\/");
    std::wstring stem = cut == std::wstring::npos ? shaderPath : shaderPath.substr(cut + 1);
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem = stem.substr(0, dot);
    std::string key = U8(shaderPath);
    for (auto& c : key) {
        if (c == '\\') c = '/';
        c = (char)tolower((unsigned char)c);
    }
    return exeDir + (exeDir.empty() || exeDir.back() == L'/' || exeDir.back() == L'\\' ? L"" : L"/") + L"params/" + stem + L"-" + W(fnvHex(key).substr(0, 8)) + L".ini";
}

bool saveParamValue(const std::wstring& file, const std::string& name, const std::string& value) {
    platform::makeDir(file.substr(0, file.find_last_of(L"\\/")));
    std::string text, out;
    readFile(file, text);
    bool done = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq != std::string::npos && trim(line.substr(0, eq)) == name) {
            if (!done) out += name + "=" + value + "\n";
            done = true;
        } else {
            out += line + "\n";
        }
    }
    if (!done) out += name + "=" + value + "\n";
    FILE* f = platform::openFile(file, "wb");
    if (!f) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool resetParamValues(const std::wstring& file) { return !fileExists(file) || platform::removeFile(file); }

std::string formatParamValue(const ParamInfo& info, const double v[4]) {
    char b[96];
    if (info.type == "color") {
        snprintf(b, sizeof(b), "#%02x%02x%02x", (int)std::lround(std::min(1.0, std::max(0.0, v[0])) * 255), (int)std::lround(std::min(1.0, std::max(0.0, v[1])) * 255),
                 (int)std::lround(std::min(1.0, std::max(0.0, v[2])) * 255));
        return b;
    }
    if (info.type == "bool") return v[0] >= 0.5 ? "true" : "false";
    std::string r;
    for (int i = 0; i < info.n; i++) {
        snprintf(b, sizeof(b), "%s%.9g", i ? "," : "", v[i]);
        r += b;
    }
    return r;
}

std::string collectShaderCode(const std::wstring& shaderPath) {
    std::string all, text;
    Hooks quiet;
    quiet.log = [](const std::wstring&) {};
    auto get = [&](const std::wstring& path) {
        if (!readFile(path, text)) return false;
        normalizeShaderText(text, quiet);  // BOM, UTF-16 et fins de ligne comme pour le rendu
        return true;
    };
    if (get(shaderPath)) all += text + "\n";
    const size_t dot = shaderPath.find_last_of(L'.');
    const size_t slash = shaderPath.find_last_of(L"\\/");
    const std::wstring stem = (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) ? shaderPath : shaderPath.substr(0, dot);
    const std::wstring ext = (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) ? L".glsl" : shaderPath.substr(dot);
    for (const char* n : {"common", "bufferA", "bufferB", "bufferC", "bufferD", "cubemapA", "post"})
        if (get(stem + L"." + W(n) + ext)) all += text + "\n";
    return all;
}

bool loadShaderParams(const std::wstring& exeDir, const std::wstring& shaderPath, std::vector<ParamInfo>& out, std::wstring& err, std::string* codeHash,
                      std::vector<double>* keyTimes) {
    out.clear();
    if (keyTimes) keyTimes->clear();
    const std::string code = collectShaderCode(shaderPath);
    if (codeHash) *codeHash = fnvHex(code);
    Settings s;
    parseLines(s, code, true);
    ParamSet ps;
    if (!ps.load(code, s, paramsFile(exeDir, shaderPath), err)) return false;
    out = ps.infos();
    if (keyTimes) *keyTimes = ps.keyTimes();
    return true;
}

bool ParamSet::load(const std::string& code, const Settings& s, const std::wstring& valuesFile, std::wstring& err, std::vector<std::wstring>* notes) {
    params_.clear();
    decls_.clear();
    hasExpr_ = false;
    hasSpectrum_ = false;
    bpm_ = s.bpm;
    beatsPerBar_ = s.beatsPerBar;
    if (rhythm_.loaded() && (rhythm_.forcedBpm() != bpm_ || rhythm_.beatsPerBar() != beatsPerBar_)) rhythm_.retune(bpm_, beatsPerBar_);
    const std::string stripped = stripCodeComments(code);
    // 1. declarations « // uniform type nom = defaut [min..max] »
    size_t pos = 0;
    while (pos <= code.size()) {
        size_t e = code.find('\n', pos);
        if (e == std::string::npos) e = code.size();
        const std::string line = trim(code.substr(pos, e - pos));
        pos = e + 1;
        if (line.compare(0, 2, "//") != 0) continue;
        const std::string body = trim(line.substr(2));
        if (body.compare(0, 8, "uniform ") != 0 && body.compare(0, 8, "uniform\t") != 0) continue;
        Param p;
        if (!parseDecl(body.substr(8), p.info, err)) {
            if (err.empty()) err = TXF("error.param_decl", W(body).c_str());
            return false;
        }
        for (const Param& o : params_)
            if (o.info.name == p.info.name) {
                err = TXF("error.param_name_taken", W(p.info.name).c_str());
                return false;
            }
        if (declaredByUser(stripped, p.info.name)) {
            err = TXF("error.param_name_taken", W(p.info.name).c_str());
            return false;
        }
        if (!p.info.expr.empty() && !compileExpr(p, err)) return false;
        params_.push_back(p);
    }
    auto find = [&](const std::string& name) -> Param* {
        for (Param& p : params_)
            if (p.info.name == name) return &p;
        return nullptr;
    };
    // 2. valeurs enregistrees par le panneau
    std::string saved;
    if (!params_.empty() && readFile(valuesFile, saved)) {
        size_t sp = 0;
        while (sp < saved.size()) {
            size_t e = saved.find('\n', sp);
            if (e == std::string::npos) e = saved.size();
            const std::string line = trim(saved.substr(sp, e - sp));
            sp = e + 1;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            Param* p = find(trim(line.substr(0, eq)));
            double v[4] = {0, 0, 0, 0};
            if (p && p->info.audio.empty() && parseValue(p->info.type, p->info.n, line.substr(eq + 1), v)) {
                clampValue(p->info, v);
                for (int i = 0; i < 4; i++) p->info.value[i] = v[i];
            }
        }
    }
    // 3. reglages du shader (// key nom=...) et de la ligne de commande (--param=nom=...), dans l'ordre
    for (const std::string& specLine : s.paramSpecs) {
        const size_t eq = specLine.find('=');
        const std::string name = eq == std::string::npos ? "" : trim(specLine.substr(0, eq));
        Param* p = find(name);
        if (!p) {
            if (notes) notes->push_back(TXF("log.param_unknown", W(name).c_str()));
            continue;
        }
        std::string spec = trim(specLine.substr(eq + 1));
        if (mentionsRhythmVar(spec)) {  // `--param=vitesse=iBass*0.5+0.2 release=0.3` : expression du rythme
            if (p->info.type != "float" && p->info.type != "int") {
                err = TXF("error.param_spec", W(name).c_str(), W(spec).c_str());
                return false;
            }
            if (!splitExprOptions(spec, p->info.expr, p->info.attack, p->info.release, p->info.threshold) || !compileExpr(*p, err)) {
                if (err.empty()) err = TXF("error.param_spec", W(name).c_str(), W(spec).c_str());
                return false;
            }
            p->info.audio = "expr";
            p->info.animated = false;
            p->keys.clear();
            continue;
        }
        Interp interp = Interp::Linear;
        for (const char* w : {"linear", "smooth", "constant", "step"}) {
            const size_t n = strlen(w);
            if (lower(spec).compare(0, n, w) == 0 && (spec.size() == n || isspace((unsigned char)spec[n]))) {
                interp = std::string(w) == "smooth" ? Interp::Smooth : std::string(w) == "linear" ? Interp::Linear : Interp::Constant;
                spec = trim(spec.substr(n));
                break;
            }
        }
        std::vector<Key> keys;
        bool constant = false;
        double cval[4] = {0, 0, 0, 0};
        size_t sp = 0;
        bool bad = spec.empty();
        while (!bad && sp <= spec.size()) {
            size_t e = spec.find(';', sp);
            if (e == std::string::npos) e = spec.size();
            const std::string part = trim(spec.substr(sp, e - sp));
            sp = e + 1;
            if (part.empty()) continue;
            const size_t colon = part.find(':');
            if (colon == std::string::npos) {  // une valeur seule : constante
                constant = parseValue(p->info.type, p->info.n, part, cval) && keys.empty() && sp > spec.size();
                if (!constant) bad = true;
                continue;
            }
            Key k;
            char* end = nullptr;
            const std::string ts = trim(part.substr(0, colon));
            k.t = strtod(ts.c_str(), &end);
            if (ts.empty() || *end != 0 || !std::isfinite(k.t) || (!keys.empty() && k.t < keys.back().t) ||
                !parseValue(p->info.type, p->info.n, part.substr(colon + 1), k.v))
                bad = true;
            else {
                clampValue(p->info, k.v);
                keys.push_back(k);
            }
        }
        if (bad || (!constant && keys.empty())) {
            err = TXF("error.param_spec", W(name).c_str(), W(spec).c_str());
            return false;
        }
        if (!p->info.audio.empty()) {  // un parametre lie au son ne s'anime pas (sauf a le remplacer par une expression)
            if (notes) notes->push_back(TXF("log.param_audio_ignored", W(name).c_str()));
            continue;
        }
        if (constant) {
            clampValue(p->info, cval);
            for (int i = 0; i < 4; i++) p->info.value[i] = cval[i];
            p->keys.clear();
            p->info.animated = false;
        } else {
            p->keys = keys;
            p->interp = interp;
            p->info.animated = true;
        }
    }
    for (const Param& p : params_) {
        const char* glType = p.info.type == "color" ? "vec3" : p.info.type.c_str();
        decls_ += std::string("uniform ") + glType + " " + p.info.name + ";\n";
        if (!p.info.expr.empty()) hasExpr_ = true;
        if (!p.info.audio.empty() && p.info.audio != "expr") hasSpectrum_ = true;
    }
    // variables du rythme lues par le code du shader : declarees ici (rien a declarer dans le shader)
    for (int i = 0; i < EV_TIME; i++) {
        rhythmUsed_[i] = usesWord(stripped, kExprVarNames[i]) && !declaredGlobally(stripped, kExprVarNames[i]);
        if (rhythmUsed_[i]) decls_ += std::string("uniform ") + (i == EV_BEATCOUNT ? "int " : "float ") + kExprVarNames[i] + ";\n";
    }
    return true;
}

bool ParamSet::compileExpr(Param& p, std::wstring& err) const {
    std::string e;
    if (!p.expr.parse(p.info.expr, e)) {
        err = TXF("error.param_expr", W(p.info.name).c_str(), W(e).c_str());
        return false;
    }
    p.seriesBuilt = false;
    p.series.clear();
    return true;
}

bool ParamSet::needsRhythm() const {
    if (hasExpr_) return true;
    for (bool u : rhythmUsed_)
        if (u) return true;
    return false;
}

bool ParamSet::needsAudio() const { return hasSpectrum_ || needsRhythm(); }

bool ParamSet::setAudio(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err) {
    audioOk_ = !hasSpectrum_;
    if (hasSpectrum_ && !(audioOk_ = audio_.load(ffmpeg, path, err))) return false;
    return !needsRhythm() || loadRhythm(ffmpeg, path, err);
}

bool ParamSet::loadRhythm(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err) {
    if (rhythm_.loaded()) return true;
    return rhythm_.load(ffmpeg, path, bpm_, beatsPerBar_, err);
}

double ParamSet::rawExpr(const Param& p, double t) const {
    ExprEnv env;
    env.v[EV_TIME] = t;
    if (rhythm_.loaded()) {
        const RhythmValues r = rhythm_.at(t);
        const double th = p.info.threshold;
        auto gate = [&](double v) { return th > 0 ? (v < th ? 0.0 : (v - th) / (1.0 - th)) : v; };
        env.v[EV_BEAT] = r.beat;
        env.v[EV_BEATCOUNT] = r.beatCount;
        env.v[EV_BPM] = r.bpm;
        env.v[EV_BASS] = gate(r.bass);
        env.v[EV_MID] = gate(r.mid);
        env.v[EV_TREBLE] = gate(r.treble);
        env.v[EV_ENERGY] = gate(r.energy);
        env.v[EV_ONSET] = gate(r.onset);
    }
    return p.expr.eval(env);
}

// Valeur de l'expression au temps t, lissee par attack / release : une fonction du temps seulement (la serie entiere est calculee une fois depuis le debut du
// son), donc la meme a chaque rendu, en apercu, apres une coupure ou en rendu par parties
double ParamSet::exprValue(const Param& p, double t) const {
    if ((p.info.attack <= 0 && p.info.release <= 0) || !rhythm_.loaded()) return rawExpr(p, t);
    constexpr double kStep = 0.01;
    if (!p.seriesBuilt) {
        const size_t n = (size_t)std::ceil(std::max(1.0, rhythm_.duration()) / kStep) + 2;
        p.series.assign(n, 0.0f);
        double y = 0;
        for (size_t i = 0; i < n; i++) {
            const double x = rawExpr(p, (double)i * kStep);
            if (i == 0) y = x;
            const double tau = x > y ? p.info.attack : p.info.release;
            y = tau > 0 ? y + (x - y) * (1.0 - std::exp(-kStep / tau)) : x;
            p.series[i] = (float)y;
        }
        p.seriesBuilt = true;
    }
    const double pos = std::min((double)(p.series.size() - 1), std::max(0.0, t / kStep));
    const size_t i0 = (size_t)std::floor(pos), i1 = std::min(p.series.size() - 1, i0 + 1);
    return p.series[i0] + (p.series[i1] - p.series[i0]) * (pos - (double)i0);
}

void ParamSet::evaluate(const Param& p, double t, double out[4]) const {
    const ParamInfo& in = p.info;
    for (int i = 0; i < 4; i++) out[i] = in.value[i];
    if (!in.expr.empty()) {
        out[0] = exprValue(p, t);
        clampValue(in, out);
        return;
    }
    if (!in.audio.empty()) {
        if (!audioOk_) {
            out[0] = in.def[0];
            return;
        }
        const AudioAnalyzer::Levels lv = audio_.levels(t);
        const double a = in.audio == "level" ? lv.level : in.audio == "bass" ? lv.bass : in.audio == "mid" ? lv.mid : lv.treble;
        out[0] = in.lo + a * (in.hi - in.lo);  // sans [min..max], 0 a 1
        clampValue(in, out);
        return;
    }
    if (p.keys.empty()) return;
    if (t <= p.keys.front().t) {
        for (int i = 0; i < 4; i++) out[i] = p.keys.front().v[i];
    } else if (t >= p.keys.back().t) {
        for (int i = 0; i < 4; i++) out[i] = p.keys.back().v[i];
    } else {
        size_t k = 0;
        while (k + 2 < p.keys.size() && p.keys[k + 1].t <= t) k++;
        const Key &a = p.keys[k], &b = p.keys[k + 1];
        double f = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1.0;
        if (p.interp == Interp::Constant) f = 0;
        else if (p.interp == Interp::Smooth) f = f * f * (3 - 2 * f);
        for (int i = 0; i < 4; i++) out[i] = a.v[i] + (b.v[i] - a.v[i]) * f;
    }
    clampValue(in, out);
}

void ParamSet::apply(GLuint prog, double t) const {
    if (rhythm_.loaded() || needsRhythm()) {  // variables du rythme, lues au temps exact de l'image
        const RhythmValues r = rhythm_.loaded() ? rhythm_.at(t) : RhythmValues();
        for (int i = 0; i < EV_TIME; i++) {
            if (!rhythmUsed_[i]) continue;
            const GLint loc = ogl::GetUniformLocation(prog, kExprVarNames[i]);
            if (loc < 0) continue;
            switch (i) {
                case EV_BEAT: ogl::Uniform1f(loc, r.beat); break;
                case EV_BEATCOUNT: ogl::Uniform1i(loc, r.beatCount); break;
                case EV_BPM: ogl::Uniform1f(loc, r.bpm); break;
                case EV_BASS: ogl::Uniform1f(loc, r.bass); break;
                case EV_MID: ogl::Uniform1f(loc, r.mid); break;
                case EV_TREBLE: ogl::Uniform1f(loc, r.treble); break;
                case EV_ENERGY: ogl::Uniform1f(loc, r.energy); break;
                default: ogl::Uniform1f(loc, r.onset); break;
            }
        }
    }
    for (const Param& p : params_) {
        const GLint loc = ogl::GetUniformLocation(prog, p.info.name.c_str());
        if (loc < 0) continue;  // ce programme (une autre passe) ne le lit pas
        double v[4];
        evaluate(p, t, v);
        if (p.info.type == "float") ogl::Uniform1f(loc, (float)v[0]);
        else if (p.info.type == "int" || p.info.type == "bool") ogl::Uniform1i(loc, (GLint)std::lround(v[0]));
        else if (p.info.n == 2) ogl::Uniform2f(loc, (float)v[0], (float)v[1]);
        else if (p.info.n == 3) ogl::Uniform3f(loc, (float)v[0], (float)v[1], (float)v[2]);
        else ogl::Uniform4f(loc, (float)v[0], (float)v[1], (float)v[2], (float)v[3]);
    }
}

std::string ParamSet::signature() const {
    std::string s;
    char b[64];
    for (const Param& p : params_) {
        s += "|" + p.info.name + ":" + p.info.type + ":" + p.info.audio;
        if (!p.info.expr.empty()) {
            snprintf(b, sizeof(b), ":%.9g:%.9g:%.9g", p.info.attack, p.info.release, p.info.threshold);
            s += "{" + p.info.expr + b + "}";
        }
        for (int i = 0; i < 4; i++) {
            snprintf(b, sizeof(b), ",%.9g", p.info.value[i]);
            s += b;
        }
        snprintf(b, sizeof(b), ";%.9g;%.9g;%d", p.info.lo, p.info.hi, (int)p.interp);
        s += b;
        for (const Key& k : p.keys) {
            snprintf(b, sizeof(b), "@%.9g", k.t);
            s += b;
            for (int i = 0; i < 4; i++) {
                snprintf(b, sizeof(b), ",%.9g", k.v[i]);
                s += b;
            }
        }
    }
    if (rhythm_.loaded()) s += rhythm_.signature();
    return s;
}

std::vector<ParamInfo> ParamSet::infos() const {
    std::vector<ParamInfo> r;
    for (const Param& p : params_) r.push_back(p.info);
    return r;
}

std::vector<double> ParamSet::keyTimes() const {
    std::vector<double> t;
    for (const Param& p : params_)
        for (const Key& k : p.keys) t.push_back(k.t);
    std::sort(t.begin(), t.end());
    t.erase(std::unique(t.begin(), t.end()), t.end());
    return t;
}

void ParamSet::valueAt(const std::string& name, double t, double out[4]) const {
    for (int i = 0; i < 4; i++) out[i] = 0;
    for (const Param& p : params_)
        if (p.info.name == name) evaluate(p, t, out);
}
