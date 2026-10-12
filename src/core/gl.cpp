// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "gl.h"
#include "i18n.h"
#include "util.h"
#include "platform/platform.h"

namespace ogl {
#define F(r, n, a) PFN_##n n = nullptr;
GL_FUNCS(F)
#undef F

bool load() {
    bool ok = true;
#define F(r, n, a)                                          \
    n = (PFN_##n)platform::glProc("gl" #n);         \
    if (!n) ok = false;
    GL_FUNCS(F)
#undef F
    return ok;
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

GLuint buildProgram(const std::string& userCode, std::wstring& err, bool bufferOut, const std::string* chType, bool cubemapOut, bool soundOut,
                    int vrMode) {
    std::string channels;
    for (int i = 0; i < 4; i++) channels += "uniform " + (chType && !chType[i].empty() ? chType[i] : std::string("sampler2D")) + " iChannel" + std::to_string(i) + ";\n";
    const std::string vs =
        "#version 330 core\n"
        "void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

    const std::string fs =
        "#version 330 core\n"
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
        + channels +
        "uniform vec3  iChannelResolution[4];\n"
        "uniform float iChannelTime[4];\n"
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
         : vrMode > 0 ? std::string("\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); vec2 fc = gl_FragCoord.xy; float side = 0.0;") +
                        (vrMode == 2 ? " if (fc.y >= iResolution.y) { fc.y -= iResolution.y; side = -1.0; } else side = 1.0;" : "") +
                        " vec2 uv = fc / iResolution.xy; float lon = (uv.x - 0.5) * 6.28318530718; float lat = (uv.y - 0.5) * 3.14159265359;"
                        " vec3 d = vec3(sin(lon) * cos(lat), sin(lat), -cos(lon) * cos(lat));"
                        " vec3 o = side * 0.5 * st_ipd * vec3(cos(lon), 0.0, sin(lon));"
                        " mainVR(c, fc, o, d); st_outColor = vec4(c.rgb * st_weight, 1.0); }\n"
         : bufferOut ? "\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy); st_outColor = c; }\n"
                   : "\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy);"
                     " st_outColor = vec4(c.rgb * st_weight, 1.0); }\n");

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
        char log[4096] = {0};
        ogl::GetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        err = TX("error.shader_link") + L"\n" + W(log);
        return 0;
    }
    return prog;
}

GLuint buildPresentProgram(std::wstring& err) {
    const std::string vs =
        "#version 330 core\n"
        "void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    const std::string fs =
        "#version 330 core\n"
        "uniform sampler2D st_tex;\n"
        "out vec4 st_o;\n"
        "void main(){ st_o = vec4(texelFetch(st_tex, ivec2(gl_FragCoord.xy), 0).rgb, 1.0); }\n";
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
