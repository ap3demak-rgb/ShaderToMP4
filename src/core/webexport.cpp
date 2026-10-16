// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "webexport.h"
#include "expr.h"
#include "app_info.h"
#include "compat.h"
#include "hooks.h"
#include "i18n.h"
#include "publish.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

namespace {

std::string jsString(const std::string& s) {
    std::string o = "\"";
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': break;
            case '\t': o += "\\t"; break;
            case '<': o += "\\u003c"; break;  // jamais de « </script> » dans la page
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

// Meme bruit et meme grille que le moteur (channels.cpp) : l'image de la page est celle du rendu
std::string noiseBytes(int channel) {
    std::string d(256 * 256 * 4, 0);
    uint32_t x = 0x9E3779B9u ^ (uint32_t)(channel * 0x85EBCA6Bu);
    for (auto& b : d) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = (char)(uint8_t)(x >> 8);
    }
    for (size_t i = 3; i < d.size(); i += 4) d[i] = (char)255;
    return d;
}

std::string gridBytes() {
    std::string d(256 * 256 * 4, 0);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            const bool line = x % 32 == 0 || y % 32 == 0;
            const bool axis = x == 0 || y == 0;
            const char v = (char)(uint8_t)(axis ? 230 : line ? 150 : 24);
            char* px = &d[((size_t)y * 256 + x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = (char)255;
        }
    return d;
}

std::string mimeOf(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const std::string ext = dot == std::wstring::npos ? "" : lower(U8(path.substr(dot + 1)));
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "webp") return "image/webp";
    if (ext == "gif") return "image/gif";
    return "";
}

// Un canal en objet JavaScript, ou faux (et message) s'il n'est pas reproductible
bool channelJson(const Settings& st, int i, const std::wstring& dir, std::string& out, std::wstring& err) {
    const std::wstring spec = st.channel[i];
    std::string filter = st.chFilter[i], wrap = st.chWrap[i];
    if (spec.empty()) {
        out = "null";
        return true;
    }
    std::string kind, extra;
    if (spec.compare(0, 6, L"buffer") == 0 && spec.size() == 7 && spec[6] >= L'A' && spec[6] <= L'D') {
        kind = "buffer";
        extra = ",\"buffer\":" + std::to_string(spec[6] - L'A');
        if (filter.empty()) filter = "linear";
        if (wrap.empty()) wrap = "clamp";
    } else if (spec == L"noise" || spec == L"grid") {
        kind = "data";
        const std::string bytes = spec == L"noise" ? noiseBytes(i) : gridBytes();
        extra = ",\"w\":256,\"h\":256,\"data\":\"" + base64Encode(bytes) + "\"";
        if (filter.empty()) filter = "mipmap";
        if (wrap.empty()) wrap = "repeat";
    } else if (spec == L"audio" || spec.compare(0, 6, L"audio:") == 0 || spec.compare(0, 6, L"video:") == 0 || spec == L"keyboard" || spec.compare(0, 5, L"cube:") == 0 ||
               spec == L"cubeA" || spec.compare(0, 7, L"volume:") == 0) {
        err = TXF("error.html_unsupported", spec.c_str());
        return false;
    } else {  // un fichier image, relatif au shader
        const bool absolute = spec.size() > 1 && (spec[1] == L':' || spec[0] == L'/' || spec[0] == L'\\');
        const std::wstring path = absolute ? spec : dir + spec;
        const std::string mime = mimeOf(path);
        std::string bytes;
        if (mime.empty() || !readFile(path, bytes)) {
            err = TXF("error.html_unsupported", spec.c_str());
            return false;
        }
        kind = "image";
        extra = ",\"uri\":\"data:" + mime + ";base64," + base64Encode(bytes) + "\",\"vflip\":" + (st.chVflip[i] ? "true" : "false");
        if (filter.empty()) filter = "mipmap";
        if (wrap.empty()) wrap = "repeat";
    }
    out = "{\"kind\":\"" + kind + "\",\"filter\":\"" + filter + "\",\"wrap\":\"" + wrap + "\"" + extra + "}";
    return true;
}

const char* kPage = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>@@TITLE@@</title>
<style>html,body{margin:0;height:100%;background:#0b0c10;overflow:hidden}canvas{display:block;position:absolute;left:0;top:0}
#bar{position:fixed;left:8px;bottom:8px;font:12px/1.4 system-ui,sans-serif;color:#9aa3b8;background:#0b0c10b0;padding:4px 8px;border-radius:6px;user-select:none}</style></head>
<body><canvas id="c"></canvas><div id="bar"></div>
<script>
"use strict";
// Page autonome generee par @@APP@@ : shader Image et tampons en WebGL 2, aucune ressource externe.
const CFG = @@CONFIG@@;
const canvas = document.getElementById("c"), bar = document.getElementById("bar");
const gl = canvas.getContext("webgl2", {antialias: false, preserveDrawingBuffer: true});
if (!gl) { bar.textContent = "WebGL 2 is not available in this browser."; throw new Error("no webgl2"); }
const floatOk = !!gl.getExtension("EXT_color_buffer_float");
const floatLinear = !!gl.getExtension("OES_texture_float_linear");
const params = new URLSearchParams(location.hash.slice(1));
const fixedTime = params.has("t") ? parseFloat(params.get("t")) : null;
const fixedW = params.has("w") ? parseInt(params.get("w"), 10) : 0, fixedH = params.has("h") ? parseInt(params.get("h"), 10) : 0;
let paused = params.get("paused") === "1";
const VS = "#version 300 es\nvoid main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
const HEAD = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" +
  "uniform vec3 iResolution;\nuniform float iTime;\nuniform float iTimeDelta;\nuniform int iFrame;\nuniform float iFrameRate;\nuniform vec4 iMouse;\nuniform vec4 iDate;\nuniform float iSampleRate;\n" +
  "uniform sampler2D iChannel0;\nuniform sampler2D iChannel1;\nuniform sampler2D iChannel2;\nuniform sampler2D iChannel3;\nuniform vec3 iChannelResolution[4];\nuniform float iChannelTime[4];\n" +
  "#define texture2D texture\n#define texture2DLod textureLod\n";
function program(code, isBuffer) {
  const fsSrc = HEAD + CFG.common + "\n" + code + "\nout vec4 st_out;\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy); st_out = " + (isBuffer ? "c" : "vec4(c.rgb, 1.0)") + "; }\n";
  const make = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { bar.textContent = gl.getShaderInfoLog(s); throw new Error(gl.getShaderInfoLog(s)); } return s; };
  const p = gl.createProgram(); gl.attachShader(p, make(gl.VERTEX_SHADER, VS)); gl.attachShader(p, make(gl.FRAGMENT_SHADER, fsSrc)); gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) { bar.textContent = gl.getProgramInfoLog(p); throw new Error(gl.getProgramInfoLog(p)); }
  const u = {}; for (const n of ["iResolution","iTime","iTimeDelta","iFrame","iFrameRate","iMouse","iDate","iSampleRate","iChannel0","iChannel1","iChannel2","iChannel3","iChannelResolution","iChannelTime"]) u[n] = gl.getUniformLocation(p, n);
  return {p, u};
}
function texParams(t, filter, wrap, mips) {
  gl.bindTexture(gl.TEXTURE_2D, t);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter === "nearest" ? gl.NEAREST : (filter === "mipmap" && mips) ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter === "nearest" ? gl.NEAREST : gl.LINEAR);
  const m = wrap === "clamp" ? gl.CLAMP_TO_EDGE : gl.REPEAT;
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, m); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, m);
}
function bytes(b64) { const s = atob(b64), a = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i); return a; }
const empty = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, empty); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0,0,0,255]));
const fileTex = {};  // textures des canaux image / bruit / grille, par passe et canal
const pending = [];
function loadChannel(key, ch) {
  if (!ch || ch.kind === "buffer") return null;
  const t = gl.createTexture(); const info = {tex: t, w: ch.w || 1, h: ch.h || 1};
  gl.bindTexture(gl.TEXTURE_2D, t); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0,0,0,255]));
  if (ch.kind === "data") {
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1); gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.bindTexture(gl.TEXTURE_2D, t); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, ch.w, ch.h, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes(ch.data));
    if (ch.filter === "mipmap") gl.generateMipmap(gl.TEXTURE_2D); texParams(t, ch.filter, ch.wrap, true);
  } else {
    pending.push(new Promise(resolve => { const img = new Image(); img.onload = () => {
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, ch.vflip); gl.bindTexture(gl.TEXTURE_2D, t); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false); info.w = img.width; info.h = img.height;
      if (ch.filter === "mipmap") gl.generateMipmap(gl.TEXTURE_2D); texParams(t, ch.filter, ch.wrap, true); resolve(); }; img.onerror = resolve; img.src = ch.uri; }));
  }
  fileTex[key] = info; return info;
}
const image = {prog: program(CFG.image.code, false), ch: CFG.image.channels};
const bufs = CFG.buffers.map((b, i) => b ? {prog: program(b.code, true), ch: b.channels, tex: [null, null], fbo: [null, null], cur: 0} : null);
CFG.image.channels.forEach((c, i) => loadChannel("I" + i, c));
CFG.buffers.forEach((b, bi) => b && b.channels.forEach((c, i) => loadChannel("B" + bi + "_" + i, c)));
let W = 0, H = 0;
function resize() {
  const dpr = window.devicePixelRatio || 1;
  const nw = fixedW || Math.max(16, Math.floor(window.innerWidth * dpr)), nh = fixedH || Math.max(16, Math.floor(window.innerHeight * dpr));
  if (nw === W && nh === H) return;
  W = nw; H = nh; canvas.width = W; canvas.height = H; canvas.style.width = fixedW ? W + "px" : "100%"; canvas.style.height = fixedH ? H + "px" : "100%";
  bufs.forEach(b => { if (!b) return;
    for (let k = 0; k < 2; k++) { if (b.tex[k]) { gl.deleteTexture(b.tex[k]); gl.deleteFramebuffer(b.fbo[k]); }
      const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);
      if (floatOk) gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, W, H, 0, gl.RGBA, gl.FLOAT, null); else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, W, H, 0, gl.RGBA, gl.HALF_FLOAT, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, floatLinear ? gl.LINEAR : gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, floatLinear ? gl.LINEAR : gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      const f = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, f); gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0);
      gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT); b.tex[k] = t; b.fbo[k] = f; }
    b.cur = 0; });
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
}
const mouse = [0, 0, 0, 0]; let mouseDown = false;
canvas.addEventListener("pointerdown", e => { mouseDown = true; const r = canvas.getBoundingClientRect(); mouse[0] = (e.clientX - r.left) * W / r.width; mouse[1] = H - (e.clientY - r.top) * H / r.height; mouse[2] = mouse[0]; mouse[3] = mouse[1]; });
canvas.addEventListener("pointermove", e => { if (!mouseDown) return; const r = canvas.getBoundingClientRect(); mouse[0] = (e.clientX - r.left) * W / r.width; mouse[1] = H - (e.clientY - r.top) * H / r.height; });
window.addEventListener("pointerup", () => { mouseDown = false; mouse[2] = -Math.abs(mouse[2]); mouse[3] = -Math.abs(mouse[3]); });
window.addEventListener("keydown", e => { if (e.code === "Space") { paused = !paused; e.preventDefault(); } });
function bindChannels(prog, chans, key) {
  const res = new Float32Array(12);
  for (let i = 0; i < 4; i++) {
    gl.activeTexture(gl.TEXTURE0 + i); const c = chans[i]; let tex = empty, w = 1, h = 1;
    if (c && c.kind === "buffer") { const b = bufs[c.buffer]; if (b) { tex = b.tex[b.cur]; w = W; h = H; } }
    else if (c) { const f = fileTex[key + i]; if (f) { tex = f.tex; w = f.w; h = f.h; } }
    gl.bindTexture(gl.TEXTURE_2D, tex); gl.uniform1i(prog.u["iChannel" + i], i); res[i * 3] = w; res[i * 3 + 1] = h; res[i * 3 + 2] = 1;
  }
  gl.uniform3fv(prog.u.iChannelResolution, res);
}
function setUniforms(prog, t, n) {
  const d = new Date();
  gl.uniform3f(prog.u.iResolution, W, H, 1); gl.uniform1f(prog.u.iTime, t); gl.uniform1f(prog.u.iTimeDelta, 1 / CFG.fps); gl.uniform1i(prog.u.iFrame, n); gl.uniform1f(prog.u.iFrameRate, CFG.fps);
  gl.uniform4f(prog.u.iMouse, mouse[0], mouse[1], mouse[2], mouse[3]);
  gl.uniform4f(prog.u.iDate, d.getFullYear(), d.getMonth(), d.getDate(), d.getHours() * 3600 + d.getMinutes() * 60 + d.getSeconds() + d.getMilliseconds() / 1000);
  gl.uniform1f(prog.u.iSampleRate, 44100); gl.uniform1fv(prog.u.iChannelTime, new Float32Array(4));
}
function frame(t, n, toScreen) {
  gl.disable(gl.BLEND); gl.viewport(0, 0, W, H);
  bufs.forEach((b, i) => { if (!b) return; gl.useProgram(b.prog.p); setUniforms(b.prog, t, n); bindChannels(b.prog, b.ch, "B" + i + "_");
    const w = 1 - b.cur; gl.bindFramebuffer(gl.FRAMEBUFFER, b.fbo[w]); gl.drawArrays(gl.TRIANGLES, 0, 3); b.cur = w; });
  if (toScreen) { gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.useProgram(image.prog.p); setUniforms(image.prog, t, n); bindChannels(image.prog, image.ch, "I"); gl.drawArrays(gl.TRIANGLES, 0, 3); }
}
function emptyVao() { gl.bindVertexArray(gl.createVertexArray()); }
emptyVao(); resize();
Promise.all(pending).then(() => {
  if (fixedTime !== null) {  // image fixe : les tampons avancent image par image jusqu'a cet instant, comme dans ShaderToMP4
    const n = Math.max(0, Math.round((fixedTime - CFG.start) * CFG.fps));
    for (let k = 0; k < n; k++) frame(CFG.start + k / CFG.fps, k, false);
    frame(fixedTime, n, true); bar.style.display = "none"; document.title = "ready"; return;
  }
  let t0 = performance.now(), n = 0, shown = CFG.start;
  function loop(now) {
    resize();
    if (!paused) { shown = CFG.start + (now - t0) / 1000; n++; } else t0 = now - (shown - CFG.start) * 1000;
    frame(shown, n, true); bar.textContent = shown.toFixed(2) + " s  -  Space: " + (paused ? "play" : "pause");
    requestAnimationFrame(loop); }
  requestAnimationFrame(loop);
});
</script></body></html>
)HTML";

}  // namespace

bool exportHtml(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const std::wstring& outPath, std::wstring& err) {
    (void)exeDir;
    std::string code;
    if (!readFile(shaderPath, code)) {
        err = TX("error.read_file");
        return false;
    }
    Hooks quiet;
    quiet.log = [](const std::wstring&) {};
    normalizeShaderText(code, quiet);
    std::vector<std::wstring> notes;
    code = adaptShaderCode(code, notes);
    if (code.find("mainImage") == std::string::npos) {
        err = code.find("mainVR") != std::string::npos ? TXF("error.html_unsupported", L"mainVR") : TX("error.no_main_image");
        return false;
    }
    Settings st = s;
    parseLines(st, code, true);
    if (code.find("// uniform") != std::string::npos) {  // parametres animes : pas dans la page
        err = TXF("error.html_unsupported", L"// uniform");
        return false;
    }
    if (!st.source.empty() || code.find("iProgress") != std::string::npos) {  // mode effet : la page ne lit pas de video ni de transition
        err = TXF("error.html_unsupported", L"source / iProgress");
        return false;
    }
    if (mentionsRhythmVar(stripCodeComments(code))) {  // iBeat, iBass... : le rythme vient d'une analyse du son que la page ne fait pas
        err = TXF("error.html_unsupported", L"iBeat / iBass / iOnset");
        return false;
    }
    if (!st.post.empty() || !st.lut.empty()) {
        err = TXF("error.html_unsupported", L"post / lut");
        return false;
    }
    const size_t cut = shaderPath.find_last_of(L"\\/");
    const std::wstring dir = cut == std::wstring::npos ? L"" : shaderPath.substr(0, cut + 1);
    const std::wstring name = shaderPath.substr(cut == std::wstring::npos ? 0 : cut + 1);
    const size_t dot = name.find_last_of(L'.');
    const std::wstring stem = dir + (dot == std::wstring::npos ? name : name.substr(0, dot));
    const std::wstring ext = dot == std::wstring::npos ? L".glsl" : name.substr(dot);
    // passes : seuls Common et Buffer A a D sont reproduits
    for (const wchar_t* unsupported : {L".sound", L".cubemapA", L".post"})
        if (fileExists(stem + unsupported + ext)) {
            err = TXF("error.html_unsupported", (std::wstring(unsupported + 1) + L" pass").c_str());
            return false;
        }
    std::string common;
    if (readFile(stem + L".common" + ext, common)) {
        normalizeShaderText(common, quiet);
        common = adaptShaderCode(common, notes);
    }
    std::string imageChannels[4];
    std::wstring chErr;
    for (int i = 0; i < 4; i++)
        if (!channelJson(st, i, dir, imageChannels[i], chErr)) {
            err = chErr;
            return false;
        }
    std::string buffers = "[";
    for (int b = 0; b < 4; b++) {
        std::string bcode;
        const std::wstring file = stem + L".buffer" + (wchar_t)(L'A' + b) + ext;
        std::string one = "null";
        if (readFile(file, bcode)) {
            normalizeShaderText(bcode, quiet);
            bcode = adaptShaderCode(bcode, notes);
            Settings bs;
            parseLines(bs, bcode, true);
            std::string chans = "[";
            for (int i = 0; i < 4; i++) {
                std::string cj;
                if (!channelJson(bs, i, dir, cj, chErr)) {
                    err = chErr;
                    return false;
                }
                chans += (i ? "," : "") + cj;
            }
            one = "{\"code\":" + jsString(bcode) + ",\"channels\":" + chans + "]}";
        }
        buffers += (b ? "," : "") + one;
    }
    buffers += "]";
    std::string chans = "[";
    for (int i = 0; i < 4; i++) chans += (i ? "," : "") + imageChannels[i];
    chans += "]";
    char head[160];
    snprintf(head, sizeof(head), "{\"fps\":%.6g,\"start\":%.6g,", st.fps > 0 ? st.fps : 60.0, st.start);
    const std::string config = std::string(head) + "\"common\":" + jsString(common) + ",\"image\":{\"code\":" + jsString(code) + ",\"channels\":" + chans + "},\"buffers\":" + buffers + "}";
    std::string page = kPage;
    auto replaceAll = [&](const std::string& key, const std::string& value) {
        size_t p = 0;
        while ((p = page.find(key, p)) != std::string::npos) {
            page.replace(p, key.size(), value);
            p += value.size();
        }
    };
    replaceAll("@@TITLE@@", U8(dot == std::wstring::npos ? name : name.substr(0, dot)));
    replaceAll("@@APP@@", U8(std::wstring(APP_NAME_WSTR)) + " " + APP_VER_STR);
    replaceAll("@@CONFIG@@", config);  // en dernier : la configuration ne contient jamais de « @@ »
    return writeFileAtomic(outPath, page) ? true : (err = TXF("error.write_failed", outPath.c_str()), false);
}

bool exportHtmlBeside(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, std::wstring& outPath, std::wstring& err) {
    if (!s.outdir.empty()) platform::makeDir(s.outdir);
    outPath = outputPath(shaderPath, s.outdir, L".html");
    return exportHtml(shaderPath, exeDir, s, outPath, err);
}
