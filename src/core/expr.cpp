// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "expr.h"
#include "i18n.h"
#include "proext.h"
#include "util.h"
#include <cmath>

const char* const kExprVarNames[EV_COUNT] = {"iBeat", "iBeatCount", "iBPM", "iBass", "iMid", "iTreble", "iEnergy", "iOnset", "iTime"};

namespace {

struct FuncDef {
    const char* name;
    int argc;
};
const FuncDef kFuncs[] = {{"sin", 1},   {"cos", 1},  {"tan", 1}, {"abs", 1},  {"sqrt", 1},       {"exp", 1},  {"log", 1},  {"floor", 1}, {"ceil", 1}, {"fract", 1},
                          {"sign", 1},  {"min", 2},  {"max", 2}, {"pow", 2},  {"mod", 2},        {"step", 2}, {"clamp", 3}, {"mix", 3},  {"smoothstep", 3}};
constexpr int kFuncCount = (int)(sizeof(kFuncs) / sizeof(kFuncs[0]));

double callFunc(int idx, const double* a) {
    switch (idx) {
        case 0: return std::sin(a[0]);
        case 1: return std::cos(a[0]);
        case 2: return std::tan(a[0]);
        case 3: return std::fabs(a[0]);
        case 4: return a[0] > 0 ? std::sqrt(a[0]) : 0.0;
        case 5: return std::exp(std::min(a[0], 80.0));
        case 6: return a[0] > 0 ? std::log(a[0]) : 0.0;
        case 7: return std::floor(a[0]);
        case 8: return std::ceil(a[0]);
        case 9: return a[0] - std::floor(a[0]);
        case 10: return a[0] > 0 ? 1.0 : a[0] < 0 ? -1.0 : 0.0;
        case 11: return std::min(a[0], a[1]);
        case 12: return std::max(a[0], a[1]);
        case 13: return std::pow(a[0], a[1]);
        case 14: return a[1] != 0 ? a[0] - a[1] * std::floor(a[0] / a[1]) : 0.0;
        case 15: return a[1] < a[0] ? 0.0 : 1.0;
        case 16: return std::min(std::max(a[0], a[1]), a[2]);
        case 17: return a[0] + (a[1] - a[0]) * a[2];
        default: {
            const double e = a[1] != a[0] ? std::min(1.0, std::max(0.0, (a[2] - a[0]) / (a[1] - a[0]))) : (a[2] < a[0] ? 0.0 : 1.0);
            return e * e * (3 - 2 * e);
        }
    }
}

}  // namespace

struct ExprParser {
    const std::string& s;
    Expr& out;
    size_t pos = 0;
    std::string err;

    ExprParser(const std::string& text, Expr& e) : s(text), out(e) {}

    void ws() {
        while (pos < s.size() && isspace((unsigned char)s[pos])) pos++;
    }
    bool fail(const char* key) {
        if (err.empty()) err = U8(TXF(key, (int)pos + 1));  // texte localise : « ... (position N) »
        return false;
    }
    bool failNamed(const char* key, const std::string& what) {
        if (err.empty()) err = U8(TXF(key, W(what).c_str(), (int)pos + 1));
        return false;
    }
    void emit(int kind, double value = 0, int index = 0, int argc = 0) {
        Expr::Op o;
        o.kind = kind;
        o.value = value;
        o.index = index;
        o.argc = argc;
        out.code_.push_back(o);
    }

    // expr = terme { (+|-) terme }
    bool expr() {
        if (!term()) return false;
        for (;;) {
            ws();
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                const char c = s[pos++];
                if (!term()) return false;
                emit(2, 0, c == '+' ? 0 : 1);
            } else {
                return true;
            }
        }
    }
    bool term() {
        if (!unary()) return false;
        for (;;) {
            ws();
            if (pos < s.size() && (s[pos] == '*' || s[pos] == '/')) {
                const char c = s[pos++];
                if (!unary()) return false;
                emit(2, 0, c == '*' ? 2 : 3);
            } else {
                return true;
            }
        }
    }
    bool unary() {
        ws();
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) {
            const char c = s[pos++];
            if (!unary()) return false;
            if (c == '-') emit(3);
            return true;
        }
        return power();
    }
    bool power() {
        if (!primary()) return false;
        ws();
        if (pos < s.size() && s[pos] == '^') {
            pos++;
            if (!unary()) return false;  // associatif a droite
            emit(2, 0, 4);
        }
        return true;
    }
    bool primary() {
        ws();
        if (pos >= s.size()) return fail("expr.err.end");
        const char c = s[pos];
        if (c == '(') {
            pos++;
            if (!expr()) return false;
            ws();
            if (pos >= s.size() || s[pos] != ')') return fail("expr.err.paren");
            pos++;
            return true;
        }
        if (isdigit((unsigned char)c) || c == '.') {
            char* end = nullptr;
            const double v = strtod(s.c_str() + pos, &end);
            if (end == s.c_str() + pos) return fail("expr.err.number");
            pos = (size_t)(end - s.c_str());
            emit(0, v);
            return true;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            const size_t b = pos;
            while (pos < s.size() && (isalnum((unsigned char)s[pos]) || s[pos] == '_' || (out.sig_ && s[pos] == '.'))) pos++;  // `midi.cc1` : le point fait partie du nom d'un signal
            const std::string name = s.substr(b, pos - b);
            ws();
            if (pos < s.size() && s[pos] == '(') {
                int fi = -1;
                for (int i = 0; i < kFuncCount; i++)
                    if (name == kFuncs[i].name) fi = i;
                if (fi < 0 && !out.sig_) return failNamed("expr.err.func", name);
                pos++;
                int argc = 0;
                ws();
                if (pos < s.size() && s[pos] == ')') {
                    pos++;
                } else {
                    for (;;) {
                        if (!expr()) return false;
                        argc++;
                        ws();
                        if (pos < s.size() && s[pos] == ',') {
                            pos++;
                            continue;
                        }
                        if (pos < s.size() && s[pos] == ')') {
                            pos++;
                            break;
                        }
                        return fail("expr.err.comma");
                    }
                }
                if (fi < 0) {  // fonction d'un signal : `midi.note(60)`
                    const int h = out.sig_->resolve(name, argc);
                    if (h < 0) return failNamed("expr.err.func", name);
                    out.usesSignals_ = true;
                    emit(5, 0, h, argc);
                    return true;
                }
                if (argc != kFuncs[fi].argc) return failNamed("expr.err.args", name);
                emit(4, 0, fi, argc);
                return true;
            }
            if (name == "pi") {
                emit(0, 3.14159265358979323846);
                return true;
            }
            for (int i = 0; i < EV_COUNT; i++)
                if (name == kExprVarNames[i]) {
                    emit(1, 0, i);
                    if (i != EV_TIME) out.usesRhythm_ = true;
                    return true;
                }
            if (out.sig_) {
                const int h = out.sig_->resolve(name, -1);
                if (h >= 0) {
                    out.usesSignals_ = true;
                    emit(5, 0, h, -1);
                    return true;
                }
            }
            return failNamed("expr.err.var", name);
        }
        return failNamed("expr.err.char", std::string(1, c));
    }
};

bool Expr::parse(const std::string& text, std::string& err, const ProSignals* sig) {
    code_.clear();
    usesRhythm_ = usesSignals_ = false;
    sig_ = sig;
    ExprParser p(text, *this);
    if (trim(text).empty()) {
        err = U8(TX("expr.err.empty"));
        return false;
    }
    const bool ok = p.expr();
    p.ws();
    if (ok && p.pos < text.size()) {
        p.failNamed("expr.err.char", std::string(1, text[p.pos]));
    } else if (!ok) {
        // le message est deja pose
    }
    if (!p.err.empty()) {
        err = p.err;
        code_.clear();
        return false;
    }
    return true;
}

double Expr::eval(const ExprEnv& env) const {
    double st[64];
    int sp = 0;
    for (const Op& o : code_) {
        switch (o.kind) {
            case 0: st[sp++] = o.value; break;
            case 1: st[sp++] = env.v[o.index]; break;
            case 2: {
                const double b = st[--sp], a = st[sp - 1];
                double r = 0;
                switch (o.index) {
                    case 0: r = a + b; break;
                    case 1: r = a - b; break;
                    case 2: r = a * b; break;
                    case 3: r = b != 0 ? a / b : 0.0; break;
                    default: r = std::pow(a, b); break;
                }
                st[sp - 1] = std::isfinite(r) ? r : 0.0;
                break;
            }
            case 3: st[sp - 1] = -st[sp - 1]; break;
            case 5: {  // signal exterieur : sans signaux, 0
                const int argc = std::max(0, o.argc);
                sp -= argc;
                const double r = env.sig ? env.sig->value(o.index, st + sp, o.argc, env.v[EV_TIME]) : 0.0;
                st[sp++] = std::isfinite(r) ? r : 0.0;
                break;
            }
            default: {
                sp -= o.argc;
                const double r = callFunc(o.index, st + sp);
                st[sp++] = std::isfinite(r) ? r : 0.0;
                break;
            }
        }
        if (sp >= 62) break;  // pile pleine : expression absurde, on s'arrete
    }
    return sp > 0 ? st[sp - 1] : 0.0;
}

static std::function<bool(const std::string&)>& exprMention() {
    static std::function<bool(const std::string&)> fn;
    return fn;
}

void registerExprMention(std::function<bool(const std::string&)> fn) { exprMention() = std::move(fn); }

bool mentionsRhythmVar(const std::string& text) {
    if (exprMention() && exprMention()(text)) return true;
    for (int i = 0; i < EV_TIME; i++) {
        const std::string n = kExprVarNames[i];
        for (size_t p = text.find(n); p != std::string::npos; p = text.find(n, p + 1)) {
            const bool before = p > 0 && (isalnum((unsigned char)text[p - 1]) || text[p - 1] == '_');
            const bool after = p + n.size() < text.size() && (isalnum((unsigned char)text[p + n.size()]) || text[p + n.size()] == '_');
            if (!before && !after) return true;
        }
    }
    return false;
}
