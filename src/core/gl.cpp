// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "gl.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"

namespace ogl {
#define F(r, n, a) PFN_##n n = nullptr;
GL_FUNCS(F)
GL_FUNCS_OPT(F)
#undef F

bool load() {
    bool ok = true;
#define F(r, n, a)                                          \
    n = (PFN_##n)platform::glProc("gl" #n);         \
    if (!n) ok = false;
    GL_FUNCS(F)
#undef F
#define F(r, n, a) n = (PFN_##n)platform::glProc("gl" #n);
    GL_FUNCS_OPT(F)
#undef F
    return ok;
}

bool computeAvailable() {
    if (!DispatchCompute || !MemoryBarrier) return false;
    GLint major = 0, minor = 0;
    glGetIntegerv(0x821B /*GL_MAJOR_VERSION*/, &major);
    glGetIntegerv(0x821C /*GL_MINOR_VERSION*/, &minor);
    return major > 4 || (major == 4 && minor >= 3);
}
}  // namespace ogl

void destroyGLContext(GLContext& c) { platform::destroyGLContext(c.impl); }

bool createGLContext(GLContext& c, std::wstring& err) {
    if (!platform::createGLContext(c.impl, err)) return false;
    if (!ogl::load()) { err = TX("error.gl_functions"); return false; }
    return true;
}

static GLuint compileShader(GLenum type, const std::string& src, std::wstring& err) {
    GLuint sh = ogl::CreateShader(type);
    const char* p = src.c_str();
    ogl::ShaderSource(sh, 1, &p, nullptr);
    ogl::CompileShader(sh);
    GLint ok = 0;
    ogl::GetShaderiv(sh, ogl::K_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        ogl::GetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
        err = TX("error.shader_compile") + L"\n" + W(log);
        ogl::DeleteShader(sh);
        return 0;
    }
    return sh;
}

// main() des modes mainVR : 1 equirectangulaire mono, 2 stereo haut / bas, 3 stereo cote a cote, 4 anaglyphe (st_eye choisit l'oeil : -1 gauche,
// 1 droit ; le moteur dessine deux fois avec des masques de couleur), 5 fulldome (fisheye 180 degres : centre = zenith, bas de l'image = devant,
// rayon 1 = horizon, noir au-dela), 6 une face de cubemap (st_face 0 a 5 = +x -x +y -y +z -z, image a l'endroit vue de l'interieur).
// Oeil gauche : cote -1 (haut ou gauche de l'image) ; le decalage de l'origine suit la direction du regard.
static std::string vrMainSource(int mode, const std::string& finalColor) {
    if (mode == 7)  // fulldome d'un shader mainImage : image carree, noire hors du cercle
        return "\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); vec2 fc = gl_FragCoord.xy + st_offset; vec2 q = fc / iResolution.xy * 2.0 - 1.0;"
               " if (length(q) <= 1.0) mainImage(c, fc); " + finalColor + " }\n";
    std::string m = "\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); vec2 fc = gl_FragCoord.xy + st_offset; float side = 0.0; vec3 o = vec3(0.0); vec3 d = vec3(0.0, 0.0, -1.0); bool inside = true;";
    if (mode == 2) m += " if (fc.y >= iResolution.y) { fc.y -= iResolution.y; side = -1.0; } else side = 1.0;";
    else if (mode == 3) m += " if (fc.x >= iResolution.x) { fc.x -= iResolution.x; side = 1.0; } else side = -1.0;";
    else if (mode == 4) m += " side = st_eye;";
    if (mode == 5) {
        m += " vec2 q = fc / iResolution.xy * 2.0 - 1.0; float r = length(q); inside = r <= 1.0; float th = r * 1.57079632679; float ph = r > 1e-5 ? atan(q.x, -q.y) : 0.0;"
             " d = vec3(sin(th) * sin(ph), cos(th), -sin(th) * cos(ph));";
    } else if (mode == 6) {
        m += " vec2 q = fc / iResolution.xy * 2.0 - 1.0;"
             " if (st_face == 0) d = vec3(1.0, q.y, q.x); else if (st_face == 1) d = vec3(-1.0, q.y, -q.x);"
             " else if (st_face == 2) d = vec3(q.x, 1.0, q.y); else if (st_face == 3) d = vec3(q.x, -1.0, -q.y);"
             " else if (st_face == 4) d = vec3(-q.x, q.y, 1.0); else d = vec3(q.x, q.y, -1.0);"
             " d = normalize(d);";
    } else {
        m += " vec2 uv = fc / iResolution.xy; float lon = (uv.x - 0.5) * 6.28318530718; float lat = (uv.y - 0.5) * 3.14159265359;"
             " d = vec3(sin(lon) * cos(lat), sin(lat), -cos(lon) * cos(lat));"
             " o = side * 0.5 * st_ipd * vec3(cos(lon), 0.0, sin(lon));";
    }
    return m + " if (inside) mainVR(c, fc, o, d); " + finalColor + " }\n";
}

GLuint buildProgram(const std::string& userCode, std::wstring& err, bool bufferOut, const std::string* chType, bool cubemapOut, bool soundOut,
                    int vrMode, const std::string& preamble, bool alphaOut, SceneProgram* scene) {
    std::string channels;
    for (int i = 0; i < 4; i++) channels += "uniform " + (chType && !chType[i].empty() ? chType[i] : std::string("sampler2D")) + " iChannel" + std::to_string(i) + ";\n";
    const std::string vs = scene ? scene->vertex :
        std::string("#version 330 core\n"
        "void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n");

    // Couleur ecrite par l'image : opaque (poids = fondu sur la couleur) ou avec transparence. En mode alpha, le fondu agit sur l'alpha ;
    // st_premul = 1 pendant l'accumulation du flou de mouvement (couleurs prémultipliées, somme exacte), 0 sinon (alpha droit).
    const std::string finalColor =
        alphaOut ? "{ float a = clamp(c.a, 0.0, 1.0); st_outColor = st_premul != 0 ? vec4(c.rgb * a, a) * st_weight : vec4(c.rgb, a * st_weight); }"
                 : "st_outColor = vec4(c.rgb * st_weight, 1.0);";
    const std::string fs =
        "#version " + (scene ? scene->version : std::string("330")) + " core\n"
        "uniform vec3  iResolution;\n"
        "uniform float iTime;\n"
        "uniform float iTimeDelta;\n"
        "uniform int   iFrame;\n"
        "uniform float iFrameRate;\n"
        "uniform vec4  iMouse;\n"
        "uniform vec4  iDate;\n"
        "uniform float iSampleRate;\n"
        "uniform float st_weight;\n"
        "uniform int   st_face;\n"
        "uniform int   st_sampleBase;\n"
        "uniform float st_ipd;\n"
        "uniform float st_eye;\n"
        "uniform vec2  st_offset;\n"
        "uniform int   st_premul;\n"
        + channels +
        "uniform vec3  iChannelResolution[4];\n"
        "uniform float iChannelTime[4];\n"
        + preamble +  // uniforms personnalises (// uniform float x = 1.0)
        "#define texture2D texture\n"  // noms GLSL ES 1.00, encore tres repandus sur Shadertoy
        "#define texture2DLod textureLod\n"
        "#define texture3D texture\n"
        "out vec4 st_outColor;\n"
        "#line 1\n" +
        userCode +
        (soundOut ? "\nvoid main(){ int samp = st_sampleBase + int(gl_FragCoord.x) + int(gl_FragCoord.y) * 512;"
                    " vec2 y = mainSound(samp, float(samp) / 44100.0);"
                    " if (y.x != y.x) y.x = 0.0; if (y.y != y.y) y.y = 0.0;"
                    " vec2 v = min(floor((0.5 + 0.5 * clamp(y, -1.0, 1.0)) * 65536.0), 65535.0);"
                    " vec2 lo = mod(v, 256.0) / 255.0; vec2 hi = floor(v / 256.0) / 255.0; st_outColor = vec4(lo.x, hi.x, lo.y, hi.y); }\n"
         : cubemapOut ? "\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); vec2 q = gl_FragCoord.xy / iResolution.xy * 2.0 - 1.0; vec3 d;"
                      " if (st_face == 0) d = vec3(1.0, -q.y, -q.x); else if (st_face == 1) d = vec3(-1.0, -q.y, q.x);"
                      " else if (st_face == 2) d = vec3(q.x, 1.0, q.y); else if (st_face == 3) d = vec3(q.x, -1.0, -q.y);"
                      " else if (st_face == 4) d = vec3(q.x, -q.y, 1.0); else d = vec3(-q.x, -q.y, -1.0);"
                      " mainCubemap(c, gl_FragCoord.xy, vec3(0.0), normalize(d)); st_outColor = c; }\n"
         : vrMode > 0 ? vrMainSource(vrMode, finalColor)
         : bufferOut ? std::string("\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy); st_outColor = c; }\n")
                   : "\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy + st_offset); " + finalColor + " }\n");

    return buildProgramFromSources(vs, fs, err, scene ? &scene->vertexFailed : nullptr);
}

GLuint buildProgramFromSources(const std::string& vs, const std::string& fs, std::wstring& err, bool* vertexFailed) {
    if (vertexFailed) *vertexFailed = false;
    GLuint v = compileShader(ogl::K_VERTEX_SHADER, vs, err);
    if (!v) {
        if (vertexFailed) *vertexFailed = true;
        return 0;
    }
    GLuint f = compileShader(ogl::K_FRAGMENT_SHADER, fs, err);
    if (!f) {
        ogl::DeleteShader(v);
        return 0;
    }
    GLuint prog = ogl::CreateProgram();
    ogl::AttachShader(prog, v);
    ogl::AttachShader(prog, f);
    ogl::LinkProgram(prog);
    ogl::DeleteShader(v);
    ogl::DeleteShader(f);
    GLint ok = 0;
    ogl::GetProgramiv(prog, ogl::K_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        ogl::GetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        err = TX("error.shader_link") + L"\n" + W(log);
        ogl::DeleteProgram(prog);
        return 0;
    }
    return prog;
}

GLuint buildComputeProgram(const std::string& cs, std::wstring& err) {
    if (!ogl::computeAvailable()) {
        err = TX("error.compute_unavailable");
        return 0;
    }
    GLuint c = compileShader(ogl::K_COMPUTE_SHADER, cs, err);
    if (!c) return 0;
    GLuint prog = ogl::CreateProgram();
    ogl::AttachShader(prog, c);
    ogl::LinkProgram(prog);
    ogl::DeleteShader(c);
    GLint ok = 0;
    ogl::GetProgramiv(prog, ogl::K_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        ogl::GetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        err = TX("error.shader_link") + L"\n" + W(log);
        ogl::DeleteProgram(prog);
        return 0;
    }
    return prog;
}

GLuint buildFullscreenProgram(const std::string& fs, std::wstring& err) {
    const std::string vs =
        "#version 330 core\n"
        "void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    GLuint v = compileShader(ogl::K_VERTEX_SHADER, vs, err);
    if (!v) return 0;
    GLuint f = compileShader(ogl::K_FRAGMENT_SHADER, fs, err);
    if (!f) {
        ogl::DeleteShader(v);
        return 0;
    }
    GLuint prog = ogl::CreateProgram();
    ogl::AttachShader(prog, v);
    ogl::AttachShader(prog, f);
    ogl::LinkProgram(prog);
    ogl::DeleteShader(v);
    ogl::DeleteShader(f);
    GLint ok = 0;
    ogl::GetProgramiv(prog, ogl::K_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        ogl::GetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        err = TX("error.shader_link") + L"\n" + W(log);
        ogl::DeleteProgram(prog);
        return 0;
    }
    return prog;
}

GLuint buildPresentProgram(std::wstring& err, bool alpha) {
    const std::string vs =
        "#version 330 core\n"
        "void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    const std::string fs = std::string(
        "#version 330 core\n"
        "uniform sampler2D st_tex;\n"
        "uniform float st_scale;\n"
        "out vec4 st_o;\n") +
        (alpha ? "void main(){ vec4 t = texelFetch(st_tex, ivec2(gl_FragCoord.xy), 0) * st_scale;"  // couleurs prémultipliées accumulées -> alpha droit
                 " st_o = t.a > 1e-5 ? vec4(t.rgb / t.a, t.a) : vec4(0.0); }\n"
               : "void main(){ st_o = vec4(texelFetch(st_tex, ivec2(gl_FragCoord.xy), 0).rgb * st_scale, 1.0); }\n");
    GLuint v = compileShader(ogl::K_VERTEX_SHADER, vs, err);
    if (!v) return 0;
    GLuint f = compileShader(ogl::K_FRAGMENT_SHADER, fs, err);
    if (!f) return 0;
    GLuint prog = ogl::CreateProgram();
    ogl::AttachShader(prog, v);
    ogl::AttachShader(prog, f);
    ogl::LinkProgram(prog);
    ogl::DeleteShader(v);
    ogl::DeleteShader(f);
    GLint ok = 0;
    ogl::GetProgramiv(prog, ogl::K_LINK_STATUS, &ok);
    if (!ok) {
        err = TX("error.present_link");
        return 0;
    }
    return prog;
}
