// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "postfx.h"
#include "i18n.h"
#include "locate.h"
#include "lut.h"
#include "util.h"
#include "platform/platform.h"

namespace {

constexpr GLenum K_TEXTURE0 = 0x84C0;
constexpr GLenum K_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum K_TEXTURE_3D = 0x806F;

// Unites de texture de la chaine (l'image et ses passes gardent 1 a 4)
constexpr int kUnitChannels = 5;  // 5 a 8 : iChannel0-3 de la passe .post
constexpr int kUnitSrc = 9;       // image (ou resultat de la passe .post) / source d'un flou
constexpr int kUnitBloom = 10;    // 10 a 12 : trois niveaux de flou
constexpr int kUnitLut3 = 13;
constexpr int kUnitLut1 = 14;

const std::vector<PostEffectInfo> kEffects = {
    {"sharpen", 0.5, 0.0, 4.0, "ui.post.sharpen"},         {"chroma", 0.01, 0.0, 0.2, "ui.post.chroma"},
    {"bloom", 0.35, 0.0, 4.0, "ui.post.bloom"},            {"contrast", 0.15, -1.0, 2.0, "ui.post.contrast"},
    {"saturation", 0.25, -1.0, 2.0, "ui.post.saturation"}, {"hue", 0.05, -0.5, 0.5, "ui.post.hue"},
    {"vignette", 0.4, 0.0, 2.0, "ui.post.vignette"},       {"grain", 0.06, 0.0, 1.0, "ui.post.grain"}};
enum { kSharpen, kChroma, kBloom, kContrast, kSaturation, kHue, kVignette, kGrain };

const char* kBrightFs =
    "#version 330 core\n"
    "uniform sampler2D uSrc; uniform vec2 uOut; uniform float uThreshold;\n"
    "out vec4 st_o;\n"
    "void main(){ vec2 uv = gl_FragCoord.xy / uOut; vec3 c = texture(uSrc, uv).rgb;\n"
    "  float l = max(c.r, max(c.g, c.b)); c *= max(l - uThreshold, 0.0) / max(l, 1e-4); st_o = vec4(c, 1.0); }\n";

const char* kDownFs =
    "#version 330 core\n"
    "uniform sampler2D uSrc; uniform vec2 uOut;\n"
    "out vec4 st_o;\n"
    "void main(){ vec2 uv = gl_FragCoord.xy / uOut; st_o = vec4(texture(uSrc, uv).rgb, 1.0); }\n";

const char* kBlurFs =
    "#version 330 core\n"
    "uniform sampler2D uSrc; uniform vec2 uOut; uniform vec2 uDir;\n"
    "out vec4 st_o;\n"
    "void main(){ vec2 uv = gl_FragCoord.xy / uOut; vec2 st = uDir / uOut;\n"
    "  vec3 c = texture(uSrc, uv).rgb * 0.2270270270;\n"
    "  c += (texture(uSrc, uv + st * 1.3846153846).rgb + texture(uSrc, uv - st * 1.3846153846).rgb) * 0.3162162162;\n"
    "  c += (texture(uSrc, uv + st * 3.2307692308).rgb + texture(uSrc, uv - st * 3.2307692308).rgb) * 0.0702702703;\n"
    "  st_o = vec4(c, 1.0); }\n";

const char* kCompositeFs =
    "#version 330 core\n"
    "uniform sampler2D uSrc, uBloom0, uBloom1, uBloom2, uLut1; uniform sampler3D uLut3;\n"
    "uniform vec2 uRes; uniform float uFade, uFrame, uLutSize, uWhite; uniform int uLutMode, uAlpha, uHdr; uniform vec3 uLutMin, uLutMax;\n"
    "uniform float aSharpen, aChroma, aBloom, aContrast, aSaturation, aHue, aVignette, aGrain;\n"
    "out vec4 st_o;\n"
    "vec3 lut1d(vec3 c){ vec3 p = clamp((c - uLutMin) / (uLutMax - uLutMin), 0.0, 1.0) * (uLutSize - 1.0);\n"
    "  ivec3 i0 = ivec3(floor(p)); vec3 f = p - vec3(i0); ivec3 i1 = min(i0 + 1, ivec3(int(uLutSize) - 1));\n"
    "  return vec3(mix(texelFetch(uLut1, ivec2(i0.r, 0), 0).r, texelFetch(uLut1, ivec2(i1.r, 0), 0).r, f.r),\n"
    "              mix(texelFetch(uLut1, ivec2(i0.g, 0), 0).g, texelFetch(uLut1, ivec2(i1.g, 0), 0).g, f.g),\n"
    "              mix(texelFetch(uLut1, ivec2(i0.b, 0), 0).b, texelFetch(uLut1, ivec2(i1.b, 0), 0).b, f.b)); }\n"
    "vec3 lut3d(vec3 c){ vec3 p = clamp((c - uLutMin) / (uLutMax - uLutMin), 0.0, 1.0) * (uLutSize - 1.0);\n"
    "  ivec3 i0 = ivec3(floor(p)); vec3 f = p - vec3(i0); ivec3 i1 = min(i0 + 1, ivec3(int(uLutSize) - 1));\n"
    "  vec3 c000 = texelFetch(uLut3, ivec3(i0.x, i0.y, i0.z), 0).rgb, c100 = texelFetch(uLut3, ivec3(i1.x, i0.y, i0.z), 0).rgb;\n"
    "  vec3 c010 = texelFetch(uLut3, ivec3(i0.x, i1.y, i0.z), 0).rgb, c001 = texelFetch(uLut3, ivec3(i0.x, i0.y, i1.z), 0).rgb;\n"
    "  vec3 c110 = texelFetch(uLut3, ivec3(i1.x, i1.y, i0.z), 0).rgb, c101 = texelFetch(uLut3, ivec3(i1.x, i0.y, i1.z), 0).rgb;\n"
    "  vec3 c011 = texelFetch(uLut3, ivec3(i0.x, i1.y, i1.z), 0).rgb, c111 = texelFetch(uLut3, ivec3(i1.x, i1.y, i1.z), 0).rgb;\n"
    "  if (f.r > f.g) {\n"  // interpolation tetraedrique : six cas selon l'ordre de f.r, f.g, f.b
    "    if (f.g > f.b) return c000 + (c100 - c000) * f.r + (c110 - c100) * f.g + (c111 - c110) * f.b;\n"
    "    if (f.r > f.b) return c000 + (c100 - c000) * f.r + (c101 - c100) * f.b + (c111 - c101) * f.g;\n"
    "    return c000 + (c001 - c000) * f.b + (c101 - c001) * f.r + (c111 - c101) * f.g;\n"
    "  }\n"
    "  if (f.b > f.g) return c000 + (c001 - c000) * f.b + (c011 - c001) * f.g + (c111 - c011) * f.r;\n"
    "  if (f.b > f.r) return c000 + (c010 - c000) * f.g + (c011 - c010) * f.b + (c111 - c011) * f.r;\n"
    "  return c000 + (c010 - c000) * f.g + (c110 - c010) * f.r + (c111 - c110) * f.b; }\n"
    "float hash(vec2 p){ vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }\n"
    // HDR : valeurs de shader (sRGB, au-dessus de 1 pour les lumieres fortes) -> lumiere lineaire -> primaires BT.2020 -> PQ ou HLG
    "vec3 srgbToLinear(vec3 c){ vec3 a = abs(c); return sign(c) * mix(a / 12.92, pow((a + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), a)); }\n"
    "vec3 pqOetf(vec3 L){ vec3 p = pow(max(L, 0.0), vec3(0.1593017578125)); return pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), vec3(78.84375)); }\n"
    "vec3 hlgOetf(vec3 E){ E = max(E, 0.0); vec3 hi = 0.17883277 * log(max(12.0 * E - 0.28466892, 1e-6)) + 0.55991073; return mix(sqrt(3.0 * E), hi, step(vec3(1.0 / 12.0), E)); }\n"
    "void main(){\n"
    "  vec2 uv = gl_FragCoord.xy / uRes; vec3 col; float alpha = uAlpha != 0 ? texture(uSrc, uv).a : 1.0;\n"
    "  if (aChroma > 0.0) { vec2 d = (uv - 0.5) * aChroma; col = vec3(texture(uSrc, uv + d).r, texture(uSrc, uv).g, texture(uSrc, uv - d).b); }\n"
    "  else col = texture(uSrc, uv).rgb;\n"
    "  if (aSharpen > 0.0) { vec2 px = 1.0 / uRes;\n"
    "    vec3 blur = (texture(uSrc, uv + vec2(px.x, 0.0)).rgb + texture(uSrc, uv - vec2(px.x, 0.0)).rgb\n"
    "               + texture(uSrc, uv + vec2(0.0, px.y)).rgb + texture(uSrc, uv - vec2(0.0, px.y)).rgb) * 0.25;\n"
    "    col += (col - blur) * aSharpen; }\n"
    "  if (aBloom > 0.0) { vec3 glow = aBloom * (texture(uBloom0, uv).rgb * 0.4 + texture(uBloom1, uv).rgb * 0.6 + texture(uBloom2, uv).rgb * 0.8);\n"
    "    col += glow; if (uAlpha != 0) alpha = clamp(alpha + dot(glow, vec3(0.3333)), 0.0, 1.0); }\n"  // la lueur rend aussi les bords transparents visibles
    "  if (aContrast != 0.0) col = (col - 0.5) * (1.0 + aContrast) + 0.5;\n"
    "  if (aSaturation != 0.0) { float l = dot(col, vec3(0.2126, 0.7152, 0.0722)); col = mix(vec3(l), col, 1.0 + aSaturation); }\n"
    "  if (aHue != 0.0) { float a = aHue * 6.28318530718; float cs = cos(a), sn = sin(a);\n"
    "    vec3 yiq = vec3(dot(col, vec3(0.299, 0.587, 0.114)), dot(col, vec3(0.596, -0.274, -0.322)), dot(col, vec3(0.211, -0.523, 0.312)));\n"
    "    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);\n"
    "    col = vec3(yiq.x + 0.956 * yiq.y + 0.621 * yiq.z, yiq.x - 0.272 * yiq.y - 0.647 * yiq.z, yiq.x - 1.106 * yiq.y + 1.703 * yiq.z); }\n"
    "  if (aVignette > 0.0) { vec2 q = (uv - 0.5) * vec2(uRes.x / uRes.y, 1.0); col *= 1.0 - aVignette * smoothstep(0.3, 1.0, length(q) * 1.2); }\n"
    "  if (aGrain > 0.0) col += (hash(gl_FragCoord.xy + uFrame * 17.0) - 0.5) * aGrain;\n"
    "  if (uLutMode == 1) col = lut1d(col); else if (uLutMode == 3) col = lut3d(col);\n"
    "  if (uHdr != 0) {\n"
    "    vec3 lin = max(mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956) * srgbToLinear(col) * uFade, 0.0);\n"
    "    col = uHdr == 1 ? pqOetf(lin * uWhite / 10000.0) : hlgOetf(lin * 0.2650 * uWhite / 203.0);\n"  // HLG : blanc de reference (1.0) = signal 0.75
    "    st_o = vec4(col, 1.0); return; }\n"                                                           // le fondu a deja agi en lumiere lineaire
    "  st_o = uAlpha != 0 ? vec4(col, alpha * uFade) : vec4(col * uFade, 1.0); }\n";  // transparence : le fondu agit sur l'alpha

}  // namespace

const std::vector<PostEffectInfo>& postEffectInfos() { return kEffects; }

bool parsePostEffects(const std::string& text, std::vector<PostEffect>& out, std::wstring& unknown) {
    out.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(',', pos);
        if (e == std::string::npos) e = text.size();
        const std::string part = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (part.empty()) continue;
        const size_t colon = part.find(':');
        const std::string name = lower(part.substr(0, colon));
        int idx = -1;
        for (size_t i = 0; i < kEffects.size(); i++)
            if (name == kEffects[i].name) idx = (int)i;
        if (idx < 0) {
            unknown = W(name);
            return false;
        }
        PostEffect fx;
        fx.index = idx;
        fx.amount = kEffects[(size_t)idx].def;
        if (colon != std::string::npos) {
            char* end = nullptr;
            const std::string a = trim(part.substr(colon + 1));
            fx.amount = strtod(a.c_str(), &end);
            if (a.empty() || *end != 0 || !std::isfinite(fx.amount)) {
                unknown = W(part);
                return false;
            }
            fx.amount = std::min(kEffects[(size_t)idx].hi, std::max(kEffects[(size_t)idx].lo, fx.amount));
        }
        out.push_back(fx);
    }
    return true;
}

bool PostChain::makeTarget(Target& t, int w, int h, std::wstring& err) {
    t.w = std::max(1, w);
    t.h = std::max(1, h);
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA16F, t.w, t.h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, K_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, K_CLAMP_TO_EDGE);
    ogl::GenFramebuffers(1, &t.fbo);
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, t.fbo);
    ogl::FramebufferTexture2D(ogl::K_FRAMEBUFFER, ogl::K_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (ogl::CheckFramebufferStatus(ogl::K_FRAMEBUFFER) != ogl::K_FRAMEBUFFER_COMPLETE) {
        err = TX("error.post_buffers");
        return false;
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

bool PostChain::build(const Settings& s, const BufferPasses& bp, const std::wstring& shaderDir, const std::wstring& ffmpeg, const Hooks& H,
                      std::wstring& err, int w, int h, const std::string& paramDecls) {
    release();
    w_ = w;
    h_ = h;
    alpha_ = s.alpha;
    std::vector<PostEffect> fx;
    std::wstring bad;
    if (!parsePostEffects(s.post, fx, bad)) {
        err = TXF("error.post_unknown", bad.c_str());
        return false;
    }
    for (const PostEffect& e : fx) amount_[e.index] = e.amount;
    custom_ = bp.hasPost();
    // LUT : chemin relatif au dossier du shader
    Lut lut;
    if (!s.lut.empty()) {
        const std::wstring& p = s.lut;
        const bool absolute = p.size() > 1 && (p[1] == L':' || p[0] == L'/' || p[0] == L'\\');
        if (!loadCubeLut(absolute ? p : shaderDir + p, lut, err)) return false;
    }
    hdr_ = s.hdr == "pq" ? 1 : s.hdr == "hlg" ? 2 : 0;
    hdrWhite_ = (float)s.hdrWhite;
    composite_ = !fx.empty() || lut.dim != 0 || hdr_ != 0;
    active_ = custom_ || composite_;
    if (!active_) return true;
    ogl::ActiveTexture(K_TEXTURE0);
    if (!makeTarget(in_, w, h, err)) return false;
    if (custom_ && composite_ && !makeTarget(mid_, w, h, err)) return false;
    if (amount_[kBloom] > 0) {
        for (int i = 0; i < 3; i++) {
            const int div = 2 << (2 * i);  // 2, 8, 32
            if (!makeTarget(bloomA_[i], w / div, h / div, err) || !makeTarget(bloomB_[i], w / div, h / div, err)) return false;
        }
        progBright_ = buildFullscreenProgram(kBrightFs, err);
        progDown_ = progBright_ ? buildFullscreenProgram(kDownFs, err) : 0;
        progBlur_ = progDown_ ? buildFullscreenProgram(kBlurFs, err) : 0;
        if (!progBlur_) return false;
    }
    if (composite_) {
        progComposite_ = buildFullscreenProgram(kCompositeFs, err);
        if (!progComposite_) return false;
        if (lut.dim == 3) {
            glGenTextures(1, &lut3_);
            ogl::ActiveTexture(K_TEXTURE0 + kUnitLut3);
            glBindTexture(K_TEXTURE_3D, lut3_);
            glPixelStorei(0x0CF5, 1);
            ogl::TexImage3D(K_TEXTURE_3D, 0, (GLint)ogl::K_RGBA32F, lut.size, lut.size, lut.size, 0, GL_RGBA, GL_FLOAT, lut.rgba.data());
            glTexParameteri(K_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(K_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else if (lut.dim == 1) {
            glGenTextures(1, &lut1_);
            ogl::ActiveTexture(K_TEXTURE0 + kUnitLut1);
            glBindTexture(GL_TEXTURE_2D, lut1_);
            glPixelStorei(0x0CF5, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, ogl::K_RGBA32F, lut.size, 1, 0, GL_RGBA, GL_FLOAT, lut.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        ogl::ActiveTexture(K_TEXTURE0);
        lutMode_ = lut.dim;
        lutSize_ = lut.size;
        for (int i = 0; i < 3; i++) {
            lutMin_[i] = lut.dmin[i];
            lutMax_[i] = lut.dmax[i];
        }
    }
    if (custom_) {
        std::string types[4];
        channelTypes(bp.postSettings(), types);
        std::wstring e;
        progCustom_ = buildProgram(bp.withCommon(bp.postCode()), e, false, types, false, false, 0, paramDecls, s.alpha);
        if (!progCustom_) {
            err = L"[Post] " + locateGlslErrors(e, bp.postPath(), 1 + (int)std::count(bp.postCode().begin(), bp.postCode().end(), '\n'), bp.commonPath());  // nom-technique
            return false;
        }
        if (!loadChannels(bp.postSettings(), shaderDir, ffmpeg, chans_, H, err, w, h, s.start, s.fps)) return false;
        bp.fixChannelSizes(chans_);
        chans_.w[0] = w;
        chans_.h[0] = h;
    }
    if (custom_ || !fx.empty() || lut.dim != 0) H.log(L"  " + TXF("log.post_active", W(std::string(custom_ ? "post.glsl " : "") + s.post + (lut.dim ? (s.post.empty() ? "lut" : " lut") : "")).c_str()));
    return true;
}

void PostChain::drawTo(GLuint fbo, int w, int h) {
    ogl::BindFramebuffer(ogl::K_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostChain::apply(GLuint targetFbo, long long n, double t, float fade, const std::function<void(GLuint prog, long long n)>& setUniforms,
                      const GLuint* bufferTex) {
    glDisable(GL_BLEND);
    GLuint srcTex = in_.tex;
    if (custom_) {  // passe .post.glsl : l'image dans iChannel0
        ogl::UseProgram(progCustom_);
        setUniforms(progCustom_, n);
        updateChannels(chans_, t, n);
        bindChannels(chans_, progCustom_, bufferTex, kUnitChannels);
        setChannelTimes(chans_, progCustom_);
        ogl::ActiveTexture(K_TEXTURE0 + kUnitChannels);
        glBindTexture(GL_TEXTURE_2D, in_.tex);
        ogl::Uniform1f(ogl::GetUniformLocation(progCustom_, "st_weight"), composite_ ? 1.0f : fade);
        drawTo(composite_ ? mid_.fbo : targetFbo, w_, h_);
        srcTex = mid_.tex;
    }
    if (composite_) {
        ogl::ActiveTexture(K_TEXTURE0 + kUnitSrc);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        if (amount_[kBloom] > 0) {  // flou de la partie lumineuse a trois echelles (1/2, 1/8, 1/32)
            GLuint from = srcTex;
            for (int i = 0; i < 3; i++) {
                const Target &a = bloomA_[i], &b = bloomB_[i];
                ogl::UseProgram(i == 0 ? progBright_ : progDown_);
                ogl::ActiveTexture(K_TEXTURE0 + kUnitSrc);
                glBindTexture(GL_TEXTURE_2D, from);
                ogl::Uniform1i(ogl::GetUniformLocation(i == 0 ? progBright_ : progDown_, "uSrc"), kUnitSrc);
                ogl::Uniform2f(ogl::GetUniformLocation(i == 0 ? progBright_ : progDown_, "uOut"), (float)a.w, (float)a.h);
                if (i == 0) ogl::Uniform1f(ogl::GetUniformLocation(progBright_, "uThreshold"), 0.7f);
                drawTo(a.fbo, a.w, a.h);
                ogl::UseProgram(progBlur_);
                ogl::Uniform1i(ogl::GetUniformLocation(progBlur_, "uSrc"), kUnitSrc);
                ogl::Uniform2f(ogl::GetUniformLocation(progBlur_, "uOut"), (float)a.w, (float)a.h);
                glBindTexture(GL_TEXTURE_2D, a.tex);
                ogl::Uniform2f(ogl::GetUniformLocation(progBlur_, "uDir"), 1.0f, 0.0f);
                drawTo(b.fbo, b.w, b.h);
                glBindTexture(GL_TEXTURE_2D, b.tex);
                ogl::Uniform2f(ogl::GetUniformLocation(progBlur_, "uDir"), 0.0f, 1.0f);
                drawTo(a.fbo, a.w, a.h);
                from = a.tex;
            }
        }
        ogl::UseProgram(progComposite_);
        ogl::ActiveTexture(K_TEXTURE0 + kUnitSrc);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        for (int i = 0; i < 3; i++) {
            ogl::ActiveTexture(K_TEXTURE0 + kUnitBloom + i);
            glBindTexture(GL_TEXTURE_2D, amount_[kBloom] > 0 ? bloomA_[i].tex : 0);
        }
        auto loc = [&](const char* name) { return ogl::GetUniformLocation(progComposite_, name); };
        ogl::Uniform1i(loc("uSrc"), kUnitSrc);
        ogl::Uniform1i(loc("uBloom0"), kUnitBloom);
        ogl::Uniform1i(loc("uBloom1"), kUnitBloom + 1);
        ogl::Uniform1i(loc("uBloom2"), kUnitBloom + 2);
        ogl::Uniform1i(loc("uLut3"), kUnitLut3);
        ogl::Uniform1i(loc("uLut1"), kUnitLut1);
        ogl::Uniform2f(loc("uRes"), (float)w_, (float)h_);
        ogl::Uniform1f(loc("uFade"), fade);
        ogl::Uniform1f(loc("uFrame"), (float)(n & 0xFFFF));
        ogl::Uniform1i(loc("uAlpha"), alpha_ ? 1 : 0);
        ogl::Uniform1i(loc("uHdr"), hdr_);
        ogl::Uniform1f(loc("uWhite"), hdrWhite_);
        ogl::Uniform1i(loc("uLutMode"), lutMode_);
        ogl::Uniform1f(loc("uLutSize"), (float)lutSize_);
        ogl::Uniform3f(loc("uLutMin"), lutMin_[0], lutMin_[1], lutMin_[2]);
        ogl::Uniform3f(loc("uLutMax"), lutMax_[0], lutMax_[1], lutMax_[2]);
        static const char* const kNames[8] = {"aSharpen", "aChroma", "aBloom", "aContrast", "aSaturation", "aHue", "aVignette", "aGrain"};
        for (int i = 0; i < 8; i++) ogl::Uniform1f(loc(kNames[i]), (float)amount_[i]);
        drawTo(targetFbo, w_, h_);
    }
    ogl::UseProgram(0);
    ogl::ActiveTexture(K_TEXTURE0);
}

void PostChain::release() {
    auto del = [](Target& t) {
        if (t.fbo) ogl::DeleteFramebuffers(1, &t.fbo);
        if (t.tex) glDeleteTextures(1, &t.tex);
        t = Target();
    };
    del(in_);
    del(mid_);
    for (int i = 0; i < 3; i++) {
        del(bloomA_[i]);
        del(bloomB_[i]);
    }
    for (GLuint* p : {&progCustom_, &progComposite_, &progBright_, &progBlur_, &progDown_})
        if (*p) {
            ogl::DeleteProgram(*p);
            *p = 0;
        }
    if (lut3_) glDeleteTextures(1, &lut3_);
    if (lut1_) glDeleteTextures(1, &lut1_);
    lut3_ = lut1_ = 0;
    freeChannels(chans_);
    chans_ = ChannelSet();
    active_ = custom_ = composite_ = alpha_ = false;
    hdr_ = 0;
    lutMode_ = lutSize_ = 0;
    for (double& a : amount_) a = 0;
}
