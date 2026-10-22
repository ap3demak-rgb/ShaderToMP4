// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "passfiles.h"
#include "i18n.h"
#include "proext.h"
#include "util.h"
#include "platform/platform.h"

std::vector<PassTab> passTabs(const std::wstring& shaderPath) {
    const size_t dot = shaderPath.find_last_of(L'.');
    const size_t slash = shaderPath.find_last_of(L"\\/");
    const std::wstring stem = (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) ? shaderPath : shaderPath.substr(0, dot);
    const std::wstring own = dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) ? L".glsl" : shaderPath.substr(dot);
    struct Def { const char* id; const wchar_t* label; const char* suffix; };
    static const Def defs[] = {{"bufferA", L"Buffer A", "bufferA"}, {"bufferB", L"Buffer B", "bufferB"}, {"bufferC", L"Buffer C", "bufferC"},  // nom-technique
                               {"bufferD", L"Buffer D", "bufferD"}, {"common", L"Common", "common"}, {"cubemapA", L"Cubemap A", "cubemapA"},  // nom-technique
                               {"sound", L"Sound", "sound"}, {"post", L"Post", "post"},                                                        // nom-technique
                               // fichiers d'une scene 3D (edition Pro) : etage des sommets, passe d'ombre, passe de calcul
                               {"vert", L"Vertex", "vert"}, {"shadow", L"Shadow", "shadow"}, {"compute", L"Compute", "compute"}};            // nom-technique
    const size_t count = proExt() ? sizeof(defs) / sizeof(defs[0]) : 8;  // sans l'edition Pro : les huit passes de Shadertoy seulement
    std::vector<PassTab> tabs;
    PassTab image;
    image.id = "image";
    image.label = L"Image";  // nom-technique
    image.path = shaderPath;
    image.exists = true;
    tabs.push_back(image);
    for (size_t di = 0; di < count; di++) {
        const Def& d = defs[di];
        PassTab t;
        t.id = d.id;
        t.label = d.label;
        t.path = stem + L"." + W(d.suffix) + L".glsl";
        for (const std::wstring& e : {own, std::wstring(L".glsl"), std::wstring(L".txt")}) {  // meme recherche que le moteur
            const std::wstring p = stem + L"." + W(d.suffix) + e;
            if (platform::fileExists(p)) {
                t.path = p;
                t.exists = true;
                break;
            }
        }
        tabs.push_back(t);
    }
    return tabs;
}

std::string passTemplate(const std::string& id) {
    if (id == "common") return "// " + U8(TX("ui.editor.tpl_common")) + "\n";
    if (id == "sound") return "vec2 mainSound(int samp, float time) {\n    return vec2(0.2 * sin(6.2831853 * 440.0 * time));\n}\n";
    if (id == "cubemapA")
        return "void mainCubemap(out vec4 fragColor, in vec2 fragCoord, in vec3 rayOri, in vec3 rayDir) {\n    fragColor = vec4(0.5 + 0.5 * rayDir, 1.0);\n}\n";
    if (id == "vert")
        return "out vec3 vWorldPos;\nout vec3 vNormal;\nout vec2 vUV;\nvoid main() {\n    vec4 w = iModel * vec4(aPosition, 1.0);\n    w.xyz += sceneInstanceOffset();\n    vWorldPos = w.xyz;\n    vNormal = normalize(iNormalMatrix * aNormal);\n    vUV = aUV;\n    gl_Position = iProjection * iView * w;\n}\n";
    if (id == "shadow") return "// light=4,7,3\n// lighttarget=0,0,0\n// shadowortho=5\n// shadowsize=1024\n// shadowrange=1,20\n";
    if (id == "compute")
        return "layout(local_size_x = 64) in;\nvoid main() {\n    uint i = gl_GlobalInvocationID.x;\n    if (int(i) >= iDataCount) return;\n    iData[i] = vec4(float(i), float(iFrame), iTime, 1.0);\n}\n";
    if (id == "post")
        return "// " + U8(TX("ui.editor.tpl_post")) + "\nvoid mainImage(out vec4 fragColor, in vec2 fragCoord) {\n    fragColor = texture(iChannel0, fragCoord / iResolution.xy);\n}\n";
    // tampons A a D : lisent leurs canaux par des commentaires // iChannel0=bufferA ...
    return "// iChannel0=" + id + "\nvoid mainImage(out vec4 fragColor, in vec2 fragCoord) {\n    vec2 uv = fragCoord / iResolution.xy;\n    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);\n}\n";
}

bool createPassFile(const PassTab& tab, std::wstring& err) {
    if (platform::fileExists(tab.path)) {
        err = TXF("error.pass_exists", tab.path.c_str());
        return false;
    }
    FILE* f = platform::openFile(tab.path, "wb");
    if (!f) {
        err = TXF("error.write_failed", tab.path.c_str());
        return false;
    }
    const std::string text = passTemplate(tab.id);
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok) err = TXF("error.write_failed", tab.path.c_str());
    return ok;
}
