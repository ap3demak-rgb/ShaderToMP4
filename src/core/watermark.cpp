// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "watermark.h"
#include "gl.h"
#include "i18n.h"

namespace {

// Police 5x7 : les seules lettres du texte « SHADERTOMP4 TRIAL » ; une ligne = 5 bits, le bit de poids fort a gauche.
struct Glyph {
    char c;
    uint8_t rows[7];
};
const Glyph kGlyphs[] = {
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
};

const Glyph* glyphFor(char c) {
    for (const Glyph& g : kGlyphs)
        if (g.c == c) return &g;
    return nullptr;
}

const char kLong[] = "SHADERTOMP4 TRIAL";
const char kShort[] = "TRIAL";

}  // namespace

int Watermark::pixelScaleFor(int outputH, int ssFactor) {
    const int k = std::max(1, (int)std::lround(outputH / 270.0));  // 1080 lignes -> 4 pixels par point de la police
    return k * std::max(1, ssFactor);
}

std::string Watermark::textFor(int fullW, int pixelScale) {
    const int longW = ((int)strlen(kLong) * 6 - 1 + 1) * pixelScale;
    return longW <= fullW * 8 / 10 ? kLong : kShort;
}

void Watermark::bitmap(const std::string& text, int k, std::vector<uint8_t>& rgba, int& w, int& h) {
    const int units = (int)text.size() * 6 - 1;
    w = units * k + k;
    h = 7 * k + k;
    rgba.assign((size_t)w * h * 4, 0);
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool overwrite) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &rgba[((size_t)y * w + x) * 4];
        if (!overwrite && p[3] != 0) return;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    };
    for (int pass = 0; pass < 2; pass++) {  // 0 : lettres, 1 : ombre portee (sous les lettres, jamais dessus)
        for (size_t i = 0; i < text.size(); i++) {
            const Glyph* g = glyphFor(text[i]);
            if (!g) continue;
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++) {
                    if (!(g->rows[r] & (0x10 >> c))) continue;
                    for (int dy = 0; dy < k; dy++)
                        for (int dx = 0; dx < k; dx++) {
                            const int x = ((int)i * 6 + c) * k + dx, y = r * k + dy;
                            if (pass == 0) put(x, y, 255, 255, 255, 153, true);
                            else put(x + k, y + k, 0, 0, 0, 128, false);
                        }
                }
        }
    }
}

bool Watermark::build(int fullW, int fullH, int outputH, int ssFactor, std::wstring& err) {
    release();
    const int k = pixelScaleFor(outputH, ssFactor);
    std::vector<uint8_t> px;
    bitmap(textFor(fullW, k), k, px, bw_, bh_);
    fullW_ = fullW;
    fullH_ = fullH;
    ox_ = std::max(0, fullW - bw_ - 2 * k);
    oy_ = 2 * k;  // origine OpenGL en bas : le filigrane est en bas de l'image finale
    // lignes de bas en haut pour OpenGL
    std::vector<uint8_t> flipped(px.size());
    for (int y = 0; y < bh_; y++) memcpy(&flipped[(size_t)(bh_ - 1 - y) * bw_ * 4], &px[(size_t)y * bw_ * 4], (size_t)bw_ * 4);
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw_, bh_, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    tex_ = t;
    const std::string fs =
        "#version 330 core\n"
        "uniform sampler2D st_tex;\n"
        "uniform vec2 st_org;\n"
        "uniform vec2 st_off;\n"
        "uniform vec2 st_size;\n"
        "out vec4 st_o;\n"
        "void main(){ vec2 q = floor(gl_FragCoord.xy) + st_off - st_org;"
        " if (q.x < 0.0 || q.y < 0.0 || q.x >= st_size.x || q.y >= st_size.y) discard;"
        " st_o = texelFetch(st_tex, ivec2(q), 0); }\n";
    std::wstring e;
    prog_ = buildFullscreenProgram(fs, e);
    if (!prog_) {
        err = e;
        release();
        return false;
    }
    return true;
}

void Watermark::draw(int tileW, int tileH, int offX, int offY) {
    (void)tileW;
    (void)tileH;
    if (!prog_) return;
    ogl::UseProgram(prog_);
    ogl::ActiveTexture(0x84C0 /*GL_TEXTURE0*/);
    glBindTexture(GL_TEXTURE_2D, tex_);
    ogl::Uniform1i(ogl::GetUniformLocation(prog_, "st_tex"), 0);
    ogl::Uniform2f(ogl::GetUniformLocation(prog_, "st_org"), (float)ox_, (float)oy_);
    ogl::Uniform2f(ogl::GetUniformLocation(prog_, "st_off"), (float)offX, (float)offY);
    ogl::Uniform2f(ogl::GetUniformLocation(prog_, "st_size"), (float)bw_, (float)bh_);
    glEnable(GL_BLEND);
    ogl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // couleur : melange ; alpha : « par-dessus »
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    ogl::UseProgram(0);
}

void Watermark::release() {
    if (prog_) ogl::DeleteProgram(prog_);
    if (tex_) glDeleteTextures(1, &tex_);
    prog_ = 0;
    tex_ = 0;
}
