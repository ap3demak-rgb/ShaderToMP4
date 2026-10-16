// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "channels.h"
#include "source.h"
#include "audio.h"
#include "i18n.h"
#include "process.h"
#include "util.h"
#include "platform/platform.h"
#include <cctype>
#include <cmath>
#include <complex>
#include <cwctype>

namespace {

constexpr GLenum K_TEXTURE0 = 0x84C0;
constexpr GLenum K_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum K_REPEAT = 0x2901;
constexpr GLenum K_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum K_TEXTURE_CUBE_MAP = 0x8513;
constexpr GLenum K_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
constexpr GLenum K_TEXTURE_3D = 0x806F;
constexpr GLenum K_TEXTURE_WRAP_R = 0x8072;
constexpr GLenum K_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F;
constexpr GLenum K_R8 = 0x8229;
constexpr GLenum K_RED = 0x1903;

struct Pixels {
    int w = 0, h = 0;
    bool isFloat = false;
    std::vector<uint8_t> data;  // RGBA8 ou RGBA32F, premiere ligne en haut
};

// Bruit deterministe (xorshift) : la meme image a chaque rendu, pour des videos reproductibles
Pixels makeNoise(int channel) {
    Pixels p;
    p.w = p.h = 256;
    p.data.resize((size_t)p.w * p.h * 4);
    uint32_t x = 0x9E3779B9u ^ (uint32_t)(channel * 0x85EBCA6Bu);
    for (auto& b : p.data) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = (uint8_t)(x >> 8);
    }
    for (size_t i = 3; i < p.data.size(); i += 4) p.data[i] = 255;
    return p;
}

// Grille : fond sombre, lignes tous les 32 pixels, axes plus clairs
Pixels makeGrid() {
    Pixels p;
    p.w = p.h = 256;
    p.data.resize((size_t)p.w * p.h * 4);
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            const bool line = x % 32 == 0 || y % 32 == 0;
            const bool axis = x == 0 || y == 0;
            const uint8_t v = axis ? 230 : line ? 150 : 24;
            uint8_t* px = &p.data[((size_t)y * p.w + x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    return p;
}

// Ciel genere pour une cubemap : degrade bleu -> horizon clair -> sol gris (faces 0..5 = +X -X +Y -Y +Z -Z)
Pixels makeSkyFace(int face) {
    Pixels p;
    p.w = p.h = 64;
    p.data.resize((size_t)p.w * p.h * 4);
    for (int y = 0; y < p.h; y++) {
        const float t = y / 63.0f;  // 0 en haut de la face
        float c[3];
        if (face == 2) {  // +Y : zenith
            c[0] = 70; c[1] = 120; c[2] = 210;
        } else if (face == 3) {  // -Y : sol
            c[0] = c[1] = c[2] = 60;
        } else if (t < 0.5f) {  // du bleu a l'horizon
            const float k = t / 0.5f;
            c[0] = 90 + 110 * k; c[1] = 140 + 80 * k; c[2] = 220 + 20 * k;
        } else {  // de l'horizon au sol
            const float k = (t - 0.5f) / 0.5f;
            c[0] = 200 - 130 * k; c[1] = 220 - 150 * k; c[2] = 240 - 170 * k;
        }
        for (int x = 0; x < p.w; x++) {
            uint8_t* px = &p.data[((size_t)y * p.w + x) * 4];
            px[0] = (uint8_t)c[0]; px[1] = (uint8_t)c[1]; px[2] = (uint8_t)c[2]; px[3] = 255;
        }
    }
    return p;
}

std::wstring extensionOf(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

bool startsWith(const std::wstring& s, const wchar_t* prefix) { return s.compare(0, wcslen(prefix), prefix) == 0; }

// Taille « LxH » lue dans la description du flux video donnee par ffmpeg
bool probeSize(const std::wstring& ffmpeg, const std::wstring& path, int& w, int& h) {
    std::string all;
    runProcess(L"\"" + ffmpeg + L"\" -hide_banner -nostdin -i \"" + path + L"\"", [&](const std::wstring& l) { all += U8(l) + "\n"; });
    const size_t v = all.find("Video:");
    if (v == std::string::npos) return false;
    for (size_t i = v; i + 3 < all.size(); i++) {
        if (!isdigit((unsigned char)all[i]) || (i > 0 && (isdigit((unsigned char)all[i - 1]) || all[i - 1] == 'x'))) continue;
        int a = 0, b = 0, used = 0;
        if (sscanf(all.c_str() + i, "%dx%d%n", &a, &b, &used) == 2 && a > 0 && b > 0 && all[i - 1] == ' ') {
            const char next = all[i + used];
            if (next == ' ' || next == ',' || next == '[') {
                w = a;
                h = b;
                return true;
            }
        }
    }
    return false;
}

// Decode la premiere image par ffmpeg : RGBA 8 bits, ou RGBA flottant pour HDR / EXR
bool decodeImage(const std::wstring& ffmpeg, const std::wstring& path, Pixels& out, std::wstring& err) {
    int w = 0, h = 0;
    if (!fileExists(path)) {
        err = TXF("error.channel_missing", path.c_str());
        return false;
    }
    if (!probeSize(ffmpeg, path, w, h)) {
        err = TXF("error.channel_read", path.c_str());
        return false;
    }
    const std::wstring ext = extensionOf(path);
    const bool flt = ext == L"hdr" || ext == L"exr";
    const std::wstring fmt = flt ? L"gbrpf32le" : L"rgba";  // plans G, B, R pour le flottant : reassembles plus bas
    auto proc = platform::Process::start(
        platform::splitCommand(L"\"" + ffmpeg + L"\" -hide_banner -loglevel quiet -nostdin -i \"" + path +
                               L"\" -frames:v 1 -f rawvideo -pix_fmt " + fmt + L" -"),
        false, true);
    if (!proc) {
        err = TX("error.ffmpeg_launch");
        return false;
    }
    std::vector<uint8_t> raw;
    char buf[65536];
    size_t got;
    while ((got = proc->readOutput(buf, sizeof(buf))) > 0) raw.insert(raw.end(), (uint8_t*)buf, (uint8_t*)buf + got);
    proc->wait();
    const size_t px = (size_t)w * h;
    if (raw.size() < px * (flt ? 12 : 4)) {
        err = TXF("error.channel_read", path.c_str());
        return false;
    }
    out.w = w;
    out.h = h;
    out.isFloat = flt;
    if (!flt) {
        out.data.assign(raw.begin(), raw.begin() + px * 4);
    } else {
        out.data.resize(px * 16);
        const float* g = (const float*)raw.data();
        const float* b = g + px;
        const float* r = b + px;
        float* o = (float*)out.data.data();
        for (size_t i = 0; i < px; i++) {
            o[i * 4 + 0] = r[i];
            o[i * 4 + 1] = g[i];
            o[i * 4 + 2] = b[i];
            o[i * 4 + 3] = 1.0f;
        }
    }
    return true;
}

void flipRows(Pixels& p) {
    const size_t row = (size_t)p.w * (p.isFloat ? 16 : 4);
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < p.h / 2; y++) {
        uint8_t* a = &p.data[(size_t)y * row];
        uint8_t* b = &p.data[(size_t)(p.h - 1 - y) * row];
        memcpy(tmp.data(), a, row);
        memcpy(a, b, row);
        memcpy(b, tmp.data(), row);
    }
}

GLuint upload(const Pixels& p, const std::string& filter, const std::string& wrap) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, p.isFloat ? ogl::K_RGBA32F : ogl::K_RGBA8, p.w, p.h, 0, GL_RGBA,
                 p.isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, p.data.data());
    const bool mip = filter.empty() || filter == "mipmap";
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter == "nearest" ? GL_NEAREST : mip ? K_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == "nearest" ? GL_NEAREST : GL_LINEAR);
    const GLint mode = wrap.empty() || wrap == "repeat" ? K_REPEAT : K_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
    if (mip) ogl::GenerateMipmap(GL_TEXTURE_2D);
    return t;
}

// --- Cubemap ---------------------------------------------------------------------------------------------------
std::wstring resolvePath(const std::wstring& shaderDir, const std::wstring& path) {
    const bool absolute = path.size() > 1 && (path[1] == L':' || path[0] == L'/' || path[0] == L'\\');
    return absolute ? path : shaderDir + path;
}

// Six faces (+X -X +Y -Y +Z -Z), de meme taille carree : generees, six fichiers (« %d » = 0 a 5) ou une croix 4x3
bool loadCubeFaces(const std::wstring& rest, const std::wstring& shaderDir, const std::wstring& ffmpeg, Pixels face[6], std::wstring& err) {
    if (rest == L"sky") {
        for (int i = 0; i < 6; i++) face[i] = makeSkyFace(i);
        return true;
    }
    const size_t pct = rest.find(L"%d");
    if (pct != std::wstring::npos) {
        for (int i = 0; i < 6; i++) {
            std::wstring name = rest;
            name.replace(pct, 2, std::to_wstring(i));
            if (!decodeImage(ffmpeg, resolvePath(shaderDir, name), face[i], err)) return false;
            if (face[i].w != face[i].h || face[i].w != face[0].w) {
                err = TXF("error.channel_cube", rest.c_str());
                return false;
            }
        }
        return true;
    }
    Pixels cross;  // croix horizontale :  .Y..  /  XZXZ(-X +Z +X -Z)  /  .-Y..
    if (!decodeImage(ffmpeg, resolvePath(shaderDir, rest), cross, err)) return false;
    if (cross.w % 4 != 0 || cross.h % 3 != 0 || cross.w / 4 != cross.h / 3) {
        err = TXF("error.channel_cube", rest.c_str());
        return false;
    }
    const int s = cross.w / 4;
    static const int pos[6][2] = {{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}};  // colonne, ligne de chaque face
    const size_t bpp = cross.isFloat ? 16 : 4;
    for (int i = 0; i < 6; i++) {
        face[i].w = face[i].h = s;
        face[i].isFloat = cross.isFloat;
        face[i].data.resize((size_t)s * s * bpp);
        for (int y = 0; y < s; y++)
            memcpy(&face[i].data[(size_t)y * s * bpp], &cross.data[((size_t)(pos[i][1] * s + y) * cross.w + pos[i][0] * s) * bpp], (size_t)s * bpp);
    }
    return true;
}

GLuint uploadCube(const Pixels face[6], const std::string& filter) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(K_TEXTURE_CUBE_MAP, t);
    glPixelStorei(0x0CF5, 1);
    for (int i = 0; i < 6; i++)
        glTexImage2D(K_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, face[i].isFloat ? ogl::K_RGBA32F : ogl::K_RGBA8, face[i].w, face[i].h, 0, GL_RGBA,
                     face[i].isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, face[i].data.data());
    const bool mip = filter.empty() || filter == "mipmap";
    glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filter == "nearest" ? GL_NEAREST : mip ? K_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filter == "nearest" ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, K_CLAMP_TO_EDGE);
    glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, K_CLAMP_TO_EDGE);
    glTexParameteri(K_TEXTURE_CUBE_MAP, K_TEXTURE_WRAP_R, K_CLAMP_TO_EDGE);
    if (mip) ogl::GenerateMipmap(K_TEXTURE_CUBE_MAP);
    glEnable(K_TEXTURE_CUBE_MAP_SEAMLESS);  // pas de couture entre les faces
    return t;
}

// --- Volume (texture 3D) ---------------------------------------------------------------------------------------
// « noise » : 32x32x32 genere ; « fichier.png@N » : N tranches cote a cote dans une image
// Fichier .bin de Shadertoy : en-tete de 20 octets (signature, largeur, hauteur, profondeur en uint32 ; nombre de canaux, disposition en
// uint8 ; format en uint16 : 0 = octets, 10 = flottants 32 bits), puis les valeurs x puis y puis z, canaux entrelaces.
// Le resultat est en RGBA (1 canal : R seul, 2 : RG, 3 : RGB, alpha a 1), en octets ou en flottants (isFloat).
static bool loadVolumeBin(const std::wstring& path, int& w, int& h, int& d, bool& isFloat, std::vector<uint8_t>& data, std::wstring& err) {
    std::string raw;
    if (!readFile(path, raw) || raw.size() < 20) {
        err = TXF("error.channel_volume", path.c_str());
        return false;
    }
    const uint8_t* b = (const uint8_t*)raw.data();
    auto u32 = [&](size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; };
    const uint32_t bw = u32(4), bh = u32(8), bd = u32(12);
    const int ch = b[16], format = b[18] | b[19] << 8;
    const size_t bytes = format == 10 ? 4 : 1;
    if (bw < 1 || bh < 1 || bd < 1 || bw > 1024 || bh > 1024 || bd > 1024 || ch < 1 || ch > 4 || (format != 0 && format != 10) ||
        raw.size() != 20 + (size_t)bw * bh * bd * ch * bytes) {
        err = TXF("error.channel_volume", path.c_str());
        return false;
    }
    w = (int)bw;
    h = (int)bh;
    d = (int)bd;
    isFloat = format == 10;
    const size_t n = (size_t)w * h * d;
    data.assign(n * 4 * (isFloat ? 4 : 1), 0);
    for (size_t i = 0; i < n; i++) {
        for (int c = 0; c < 4; c++) {
            if (isFloat) {
                float v = c == 3 && ch < 4 ? 1.0f : 0.0f;
                if (c < ch) memcpy(&v, &b[20 + (i * ch + c) * 4], 4);
                memcpy(&data[(i * 4 + c) * 4], &v, 4);
            } else {
                data[i * 4 + c] = c < ch ? b[20 + i * ch + c] : (c == 3 ? 255 : 0);
            }
        }
    }
    return true;
}

bool loadVolumeData(const std::wstring& rest, const std::wstring& shaderDir, const std::wstring& ffmpeg, int& w, int& h, int& d,
                    bool& isFloat, std::vector<uint8_t>& data, std::wstring& err) {
    isFloat = false;
    if (rest.size() > 4 && towlower(rest[rest.size() - 1]) == L'n' && towlower(rest[rest.size() - 2]) == L'i' &&
        towlower(rest[rest.size() - 3]) == L'b' && rest[rest.size() - 4] == L'.')
        return loadVolumeBin(resolvePath(shaderDir, rest), w, h, d, isFloat, data, err);
    if (rest == L"noise") {
        w = h = d = 32;
        data.resize((size_t)w * h * d * 4);
        uint32_t x = 0xC2B2AE35u;
        for (auto& b : data) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            b = (uint8_t)(x >> 8);
        }
        return true;
    }
    const size_t at = rest.find_last_of(L'@');
    int slices = 0;
    if (at == std::wstring::npos || (slices = (int)wcstol(rest.c_str() + at + 1, nullptr, 10)) < 2) {
        err = TXF("error.channel_volume", rest.c_str());
        return false;
    }
    Pixels atlas;
    if (!decodeImage(ffmpeg, resolvePath(shaderDir, rest.substr(0, at)), atlas, err)) return false;
    if (atlas.isFloat || atlas.w % slices != 0) {
        err = TXF("error.channel_volume", rest.c_str());
        return false;
    }
    w = atlas.w / slices;
    h = atlas.h;
    d = slices;
    data.resize((size_t)w * h * d * 4);
    for (int z = 0; z < d; z++)
        for (int y = 0; y < h; y++)
            memcpy(&data[(((size_t)z * h + y) * w) * 4], &atlas.data[((size_t)y * atlas.w + (size_t)z * w) * 4], (size_t)w * 4);
    return true;
}

GLuint uploadVolume(int w, int h, int d, bool isFloat, const std::vector<uint8_t>& data, const std::string& filter, const std::string& wrap) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(K_TEXTURE_3D, t);
    glPixelStorei(0x0CF5, 1);
    ogl::TexImage3D(K_TEXTURE_3D, 0, (GLint)(isFloat ? ogl::K_RGBA32F : ogl::K_RGBA8), w, h, d, 0, GL_RGBA, isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE,
                    data.data());
    const bool mip = filter.empty() || filter == "mipmap";
    glTexParameteri(K_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter == "nearest" ? GL_NEAREST : mip ? K_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(K_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter == "nearest" ? GL_NEAREST : GL_LINEAR);
    const GLint mode = wrap == "clamp" ? K_CLAMP_TO_EDGE : K_REPEAT;
    glTexParameteri(K_TEXTURE_3D, GL_TEXTURE_WRAP_S, mode);
    glTexParameteri(K_TEXTURE_3D, GL_TEXTURE_WRAP_T, mode);
    glTexParameteri(K_TEXTURE_3D, K_TEXTURE_WRAP_R, mode);
    if (mip) ogl::GenerateMipmap(K_TEXTURE_3D);
    return t;
}

// --- Audio : texture 512x2 comme Shadertoy (ligne 0 : spectre, ligne 1 : forme d'onde), calee sur le temps exact ----
class AudioChannel : public DynamicChannel {
public:
    explicit AudioChannel(GLuint tex) : tex_(tex) {}
    bool load(const std::wstring& ffmpeg, const std::wstring& path, std::wstring& err) { return audio_.load(ffmpeg, path, err); }

    bool timed() const override { return true; }
    void update(double t, long long) override {
        uint8_t row[512 * 2];
        audio_.rows(t, row);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(0x0CF5, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 2, K_RED, GL_UNSIGNED_BYTE, row);
    }

private:
    GLuint tex_;
    AudioAnalyzer audio_;
};

// --- Video : images decodees par ffmpeg au rythme du rendu, bouclee, relancee a la position voulue apres un saut --------
class VideoChannel : public DynamicChannel {
public:
    VideoChannel(GLuint tex, std::wstring ffmpeg, std::wstring path, int w, int h, double fps, bool vflip)
        : tex_(tex), ffmpeg_(std::move(ffmpeg)), path_(std::move(path)), w_(w), h_(h), fps_(fps), vflip_(vflip), frame_((size_t)w * h * 4) {}
    ~VideoChannel() override { proc_.reset(); }

    bool timed() const override { return true; }
    void update(double t, long long idx) override {
        if (idx == cur_) return;
        if (proc_ && idx >= next_ && idx - next_ <= 8) {  // quelques images en avant : on les lit et on jette
            while (next_ < idx) {
                if (!readFrame()) return;
                next_++;
            }
        } else {  // saut ou premiere image : on relance ffmpeg a ce temps
            if (!start(t)) return;
            next_ = idx;
        }
        if (!readFrame()) return;
        next_ = idx + 1;
        cur_ = idx;
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(0x0CF5, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, frame_.data());
    }

private:
    bool start(double t0) {
        proc_.reset();
        const std::wstring cmd = L"\"" + ffmpeg_ + L"\" -hide_banner -loglevel quiet -nostdin -stream_loop -1 -ss " + wfmt(L"%.3f", std::max(0.0, t0)) +
                                 L" -i \"" + path_ + L"\" -an -vf \"" + (vflip_ ? L"vflip," : L"") + wfmt(L"fps=%g,scale=%d:%d:flags=bilinear", fps_, w_, h_) +
                                 L"\" -f rawvideo -pix_fmt rgba -";
        proc_ = platform::Process::start(platform::splitCommand(cmd), false, true);
        return (bool)proc_;
    }
    bool readFrame() {
        size_t have = 0;
        while (have < frame_.size()) {
            const size_t got = proc_->readOutput((char*)frame_.data() + have, frame_.size() - have);
            if (got == 0) return false;  // fin du flux ou erreur : on garde l'image precedente
            have += got;
        }
        return true;
    }
    GLuint tex_;
    std::wstring ffmpeg_, path_;
    int w_, h_;
    double fps_;
    bool vflip_;
    std::vector<uint8_t> frame_;
    std::unique_ptr<platform::Process> proc_;
    long long cur_ = -1, next_ = 0;
};

// --- Source du mode effet : video, sequence ou image fixe lue a pleine taille, l'image exacte de l'instant demande (cadence de sortie, sans interpolation),
// image tenue apres la fin, lecture en arriere par blocs, decalage de temps pour la deuxieme source d'une transition -------------------------------------
class SourceChannel : public DynamicChannel {
public:
    SourceChannel(GLuint tex, std::wstring ffmpeg, std::wstring path, int w, int h, double fps, bool vflip, bool still, double offset, double seqFps, std::string matrix)
        : tex_(tex), ffmpeg_(std::move(ffmpeg)), path_(std::move(path)), w_(w), h_(h), fps_(fps), vflip_(vflip), still_(still), offset_(offset), seqFps_(seqFps), matrix_(std::move(matrix)),
          frame_((size_t)w * h * 4) {
        const size_t per = (size_t)w * h * 4;
        blockFrames_ = (int)std::max<size_t>(4, std::min<size_t>(48, (256u << 20) / std::max<size_t>(1, per)));
    }
    ~SourceChannel() override { proc_.reset(); }

    bool timed() const override { return true; }
    void update(double t, long long) override {
        if (still_) {
            if (cur_ >= 0) return;
            if (!startStill()) return;
            cur_ = 0;
            upload();
            return;
        }
        const double ts = std::max(0.0, t + offset_);
        const long long k = std::llround(ts * fps_);
        if (k == cur_) return;
        if (!fetch(k)) return;  // image precedente gardee (fin de la source)
        cur_ = k;
        upload();
    }

private:
    std::wstring inputArgs() const {
        return (isSourcePattern(path_) && seqFps_ > 0 ? wfmt(L" -framerate %g", seqFps_) : std::wstring());
    }
    std::wstring scaleArgs() const { return wfmt(L"scale=%d:%d:flags=bilinear", w_, h_) + (matrix_.empty() ? std::wstring() : L":in_color_matrix=" + W(matrix_)); }
    std::wstring filter() const { return std::wstring(vflip_ ? L"vflip," : L"") + wfmt(L"fps=%.9g,", fps_) + scaleArgs(); }
    bool startStill() {
        proc_.reset();
        const std::wstring cmd = L"\"" + ffmpeg_ + L"\" -hide_banner -loglevel quiet -nostdin -i \"" + path_ + L"\" -an -frames:v 1 -vf \"" + (vflip_ ? L"vflip," : L"") +
                                 scaleArgs() + L"\" -f rawvideo -pix_fmt rgba -";
        proc_ = platform::Process::start(platform::splitCommand(cmd), false, true);
        return proc_ && readFrame(frame_);
    }
    bool start(long long k) {
        proc_.reset();
        const std::wstring cmd = L"\"" + ffmpeg_ + L"\" -hide_banner -loglevel quiet -nostdin" + inputArgs() + (k > 0 ? wfmt(L" -ss %.6f", (double)k / fps_) : std::wstring()) + L" -i \"" +
                                 path_ + L"\" -an -vf \"" + filter() + L"\" -f rawvideo -pix_fmt rgba -";
        proc_ = platform::Process::start(platform::splitCommand(cmd), false, true);
        return (bool)proc_;
    }
    bool readFrame(std::vector<uint8_t>& dst) {
        size_t have = 0;
        if (dst.size() != frame_.size()) dst.resize(frame_.size());
        while (have < dst.size()) {
            const size_t got = proc_->readOutput((char*)dst.data() + have, dst.size() - have);
            if (got == 0) return false;
            have += got;
        }
        return true;
    }
    // Lecture en arriere : un bloc d'images se terminant a k est decode d'un coup, puis servi image par image
    bool loadBlock(long long k) {
        const long long k0 = std::max(0LL, k - blockFrames_ + 1);
        const long long count = k - k0 + 1;
        proc_.reset();
        const std::wstring cmd = L"\"" + ffmpeg_ + L"\" -hide_banner -loglevel quiet -nostdin" + inputArgs() + (k0 > 0 ? wfmt(L" -ss %.6f", (double)k0 / fps_) : std::wstring()) +
                                 L" -i \"" + path_ + L"\" -an -vf \"" + filter() + wfmt(L"\" -frames:v %lld", count) + L" -f rawvideo -pix_fmt rgba -";
        auto p = platform::Process::start(platform::splitCommand(cmd), false, true);
        if (!p) return false;
        std::swap(p, proc_);
        block_.clear();
        for (long long i = 0; i < count; i++) {
            std::vector<uint8_t> f(frame_.size());
            if (!readFrame(f)) break;
            block_.push_back(std::move(f));
        }
        proc_.reset();
        blockK0_ = k0;
        nextK_ = -1;
        if (k - k0 >= (long long)block_.size()) return false;
        frame_ = block_[(size_t)(k - k0)];
        return true;
    }
    bool fetch(long long k) {
        if (!block_.empty() && k >= blockK0_ && k < blockK0_ + (long long)block_.size()) {
            frame_ = block_[(size_t)(k - blockK0_)];
            return true;
        }
        if (proc_ && nextK_ >= 0 && k >= nextK_ && k - nextK_ <= 8) {  // quelques images en avant : lues et jetees
            while (nextK_ < k) {
                if (!readFrame(frame_)) return false;
                nextK_++;
            }
            if (!readFrame(frame_)) return false;
            nextK_ = k + 1;
            return true;
        }
        if (cur_ >= 0 && k < cur_) return loadBlock(k);
        block_.clear();
        if (!start(k)) return false;
        nextK_ = k;
        if (!readFrame(frame_)) return false;
        nextK_ = k + 1;
        return true;
    }
    void upload() {
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(0x0CF5, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, frame_.data());
    }
    GLuint tex_;
    std::wstring ffmpeg_, path_;
    int w_, h_;
    double fps_;
    bool vflip_, still_;
    double offset_, seqFps_;
    std::string matrix_;
    std::vector<uint8_t> frame_;
    std::vector<std::vector<uint8_t>> block_;
    long long blockK0_ = 0, cur_ = -1, nextK_ = -1;
    int blockFrames_ = 8;
    std::unique_ptr<platform::Process> proc_;
};

// --- Clavier simule : texture 256x3 de Shadertoy (etat, appui sur cette image, bascule), touches donnees par `// key=` ----------
class KeyboardChannel : public DynamicChannel {
public:
    KeyboardChannel(GLuint tex, std::vector<KeyPress> keys, double fps) : tex_(tex), keys_(std::move(keys)), dt_(1.0 / fps) {}
    void update(double t, long long) override {
        uint8_t px[kKeyboardW * kKeyboardH];
        keyboardState(keys_, t, dt_, px);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(0x0CF5, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kKeyboardW, kKeyboardH, K_RED, GL_UNSIGNED_BYTE, px);
    }

private:
    GLuint tex_;
    std::vector<KeyPress> keys_;
    double dt_;
};

}  // namespace

static bool isCubeA(const std::wstring& spec) {
    std::wstring l = spec;
    for (auto& c : l) c = (wchar_t)towlower(c);
    return l == L"cubea" || l == L"cubemapa";
}

std::string channelSamplerType(const std::wstring& spec) {
    if (startsWith(spec, L"cube:") || isCubeA(spec)) return "samplerCube";
    if (startsWith(spec, L"volume:")) return "sampler3D";
    return "sampler2D";
}

void channelTypes(const Settings& s, std::string out[4]) {
    for (int i = 0; i < 4; i++) out[i] = channelSamplerType(s.channel[i]);
}

bool loadChannels(const Settings& s, const std::wstring& shaderDir, const std::wstring& ffmpeg, ChannelSet& out, const Hooks& H,
                  std::wstring& err, int bufW, int bufH, double start, double fps) {
    for (int i = 0; i < 4; i++) {
        const std::wstring& spec = s.channel[i];
        if (spec.empty()) continue;
        ogl::ActiveTexture(K_TEXTURE0);
        if (isCubeA(spec)) {  // cubemap rendue par la passe « Cubemap A »
            out.buffer[i] = 4;
            out.target[i] = K_TEXTURE_CUBE_MAP;
            out.w[i] = out.h[i] = kCubeSize;
            continue;
        }
        if (spec.size() == 7 && (spec.compare(0, 6, L"buffer") == 0 || spec.compare(0, 6, L"Buffer") == 0) && towlower(spec[6]) >= L'a' &&
            towlower(spec[6]) <= L'd') {  // tampon d'une passe multiple : pas de texture a charger
            out.buffer[i] = (int)(towlower(spec[6]) - L'a');
            out.w[i] = bufW;
            out.h[i] = bufH;
            out.filter[i] = s.chFilter[i].empty() || s.chFilter[i] == "mipmap" ? "linear" : s.chFilter[i];
            out.wrap[i] = s.chWrap[i].empty() ? "clamp" : s.chWrap[i];
            continue;
        }
        if (startsWith(spec, L"cube:")) {
            Pixels face[6];
            if (!loadCubeFaces(spec.substr(5), shaderDir, ffmpeg, face, err)) return false;
            out.tex[i] = uploadCube(face, s.chFilter[i]);
            out.target[i] = K_TEXTURE_CUBE_MAP;
            out.w[i] = out.h[i] = face[0].w;
            H.log(L"  " + TXF("log.channel_loaded", i, spec.c_str(), face[0].w, face[0].h));
            continue;
        }
        if (startsWith(spec, L"volume:")) {
            int w = 0, h = 0, d = 0;
            std::vector<uint8_t> data;
            bool isFloat = false;
            if (!loadVolumeData(spec.substr(7), shaderDir, ffmpeg, w, h, d, isFloat, data, err)) return false;
            out.tex[i] = uploadVolume(w, h, d, isFloat, data, s.chFilter[i], s.chWrap[i]);
            out.target[i] = K_TEXTURE_3D;
            out.w[i] = w;
            out.h[i] = h;
            out.d[i] = d;
            H.log(L"  " + TXF("log.channel_loaded", i, spec.c_str(), w, h));
            continue;
        }
        if (spec == L"audio" || startsWith(spec, L"audio:")) {  // spectre et forme d'onde du fichier (reglage audio par defaut)
            std::wstring path = spec == L"audio" ? s.audio : resolvePath(shaderDir, spec.substr(6));
            if (path.empty()) {
                err = TX("error.channel_audio");
                return false;
            }
            glGenTextures(1, &out.tex[i]);
            glBindTexture(GL_TEXTURE_2D, out.tex[i]);
            glPixelStorei(0x0CF5, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)K_R8, 512, 2, 0, K_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, K_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, K_CLAMP_TO_EDGE);
            auto a = std::make_shared<AudioChannel>(out.tex[i]);
            if (!a->load(ffmpeg, path, err)) return false;
            out.dyn[i] = a;
            out.w[i] = 512;
            out.h[i] = 2;
            H.log(L"  " + TXF("log.channel_loaded", i, path.c_str(), 512, 2));
            continue;
        }
        if (spec == L"keyboard") {  // clavier simule : texture 256x3, au temps exact de l'image
            glGenTextures(1, &out.tex[i]);
            glBindTexture(GL_TEXTURE_2D, out.tex[i]);
            glPixelStorei(0x0CF5, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)K_R8, kKeyboardW, kKeyboardH, 0, K_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, K_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, K_CLAMP_TO_EDGE);
            auto kb = std::make_shared<KeyboardChannel>(out.tex[i], s.keys, fps);
            kb->update(start, 0);  // l'etat existe des le premier usage (apercu, relecture)
            out.dyn[i] = kb;
            out.w[i] = kKeyboardW;
            out.h[i] = kKeyboardH;
            H.log(L"  " + TXF("log.channel_loaded", i, spec.c_str(), kKeyboardW, kKeyboardH));
            continue;
        }
        if (startsWith(spec, L"source:") || startsWith(spec, L"source2:")) {  // mode effet : la source (iChannel0) ou la deuxieme source d'une transition (iChannel1)
            const bool second = startsWith(spec, L"source2:");
            const std::wstring path = spec.substr(second ? 8 : 7);
            const int sw = second ? s.rt.width2 : s.rt.width, sh = second ? s.rt.height2 : s.rt.height;
            const double sfps = second ? s.rt.fps2 : s.rt.fps;
            const bool still = second ? s.rt.still2 : s.rt.still;
            int vw = std::max(2, std::min(7680, sw) & ~1), vh = std::max(2, std::min(4320, sh) & ~1);
            if (sw <= 0 || sh <= 0) {
                err = TXF("error.channel_read", path.c_str());
                return false;
            }
            glGenTextures(1, &out.tex[i]);
            glBindTexture(GL_TEXTURE_2D, out.tex[i]);
            glPixelStorei(0x0CF5, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ogl::K_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s.chFilter[i] == "nearest" ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s.chFilter[i] == "nearest" ? GL_NEAREST : GL_LINEAR);
            const GLint mode = s.chWrap[i] == "repeat" ? K_REPEAT : K_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
            out.dyn[i] = std::make_shared<SourceChannel>(out.tex[i], ffmpeg, path, vw, vh, fps, s.chVflip[i], still, second ? s.rt.timeOffset2 : 0.0, s.sourceFps > 0 ? s.sourceFps : 25.0, second ? s.rt.matrix2 : s.rt.matrix);
            (void)sfps;
            out.w[i] = vw;
            out.h[i] = vh;
            H.log(L"  " + TXF("log.channel_loaded", i, path.c_str(), vw, vh));
            continue;
        }
        if (startsWith(spec, L"video:")) {
            const std::wstring path = resolvePath(shaderDir, spec.substr(6));
            int vw = 0, vh = 0;
            if (!fileExists(path)) {
                err = TXF("error.channel_missing", path.c_str());
                return false;
            }
            if (!probeSize(ffmpeg, path, vw, vh)) {
                err = TXF("error.channel_read", path.c_str());
                return false;
            }
            if (vw > 1280 || vh > 720) {  // images reduites : 1280x720 au plus
                const double k = std::min(1280.0 / vw, 720.0 / vh);
                vw = (int)(vw * k);
                vh = (int)(vh * k);
            }
            vw = std::max(2, vw & ~1);
            vh = std::max(2, vh & ~1);
            glGenTextures(1, &out.tex[i]);
            glBindTexture(GL_TEXTURE_2D, out.tex[i]);
            glPixelStorei(0x0CF5, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ogl::K_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s.chFilter[i] == "nearest" ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s.chFilter[i] == "nearest" ? GL_NEAREST : GL_LINEAR);
            const GLint mode = s.chWrap[i] == "repeat" ? K_REPEAT : K_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
            out.dyn[i] = std::make_shared<VideoChannel>(out.tex[i], ffmpeg, path, vw, vh, fps, s.chVflip[i]);
            out.w[i] = vw;
            out.h[i] = vh;
            H.log(L"  " + TXF("log.channel_loaded", i, path.c_str(), vw, vh));
            continue;
        }
        Pixels p;
        if (spec == L"noise") {
            p = makeNoise(i);
        } else if (spec == L"grid") {
            p = makeGrid();
        } else {
            const std::wstring path = resolvePath(shaderDir, spec);
            if (!decodeImage(ffmpeg, path, p, err)) return false;
            if (s.chVflip[i]) flipRows(p);  // Shadertoy : le haut de l'image est en v = 1
            H.log(L"  " + TXF("log.channel_loaded", i, path.c_str(), p.w, p.h));
        }
        out.tex[i] = upload(p, s.chFilter[i], s.chWrap[i]);
        out.w[i] = p.w;
        out.h[i] = p.h;
    }
    (void)start;
    return true;
}

void bindChannels(const ChannelSet& cs, GLuint prog, const GLuint* bufferTex, int unitBase) {
    float res[12] = {0};
    for (int i = 0; i < 4; i++) {
        ogl::ActiveTexture(K_TEXTURE0 + unitBase + i);
        if (cs.buffer[i] == 4) {  // cubemap de la passe Cubemap A
            glBindTexture(K_TEXTURE_CUBE_MAP, bufferTex ? bufferTex[4] : 0);
            glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, K_CLAMP_TO_EDGE);
            glTexParameteri(K_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, K_CLAMP_TO_EDGE);
            glTexParameteri(K_TEXTURE_CUBE_MAP, K_TEXTURE_WRAP_R, K_CLAMP_TO_EDGE);
        } else if (cs.buffer[i] >= 0) {
            glBindTexture(GL_TEXTURE_2D, bufferTex ? bufferTex[cs.buffer[i]] : 0);
            const GLint f = cs.filter[i] == "nearest" ? GL_NEAREST : GL_LINEAR;
            const GLint m = cs.wrap[i] == "repeat" ? K_REPEAT : K_CLAMP_TO_EDGE;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m);
        } else {
            glBindTexture(cs.target[i], cs.tex[i]);
        }
        ogl::Uniform1i(ogl::GetUniformLocation(prog, ("iChannel" + std::to_string(i)).c_str()), unitBase + i);
        res[i * 3] = (float)cs.w[i];
        res[i * 3 + 1] = (float)cs.h[i];
        res[i * 3 + 2] = (float)cs.d[i];
    }
    ogl::ActiveTexture(K_TEXTURE0);
    ogl::Uniform3fv(ogl::GetUniformLocation(prog, "iChannelResolution"), 4, res);
    setChannelTimes(cs, prog);
}

void setChannelTimes(const ChannelSet& cs, GLuint prog) {
    const float times[4] = {(float)cs.time[0], (float)cs.time[1], (float)cs.time[2], (float)cs.time[3]};
    ogl::Uniform1fv(ogl::GetUniformLocation(prog, "iChannelTime"), 4, times);
}

void updateChannels(ChannelSet& cs, double t, long long frameIndex) {
    bool any = false;
    for (int i = 0; i < 4; i++)
        if (cs.dyn[i]) any = true;
    if (!any) return;
    ogl::ActiveTexture(K_TEXTURE0);
    for (int i = 0; i < 4; i++)
        if (cs.dyn[i]) {
            cs.dyn[i]->update(t, frameIndex);
            if (cs.dyn[i]->timed()) cs.time[i] = t;
        }
}

void freeChannels(ChannelSet& cs) {
    for (int i = 0; i < 4; i++) {
        cs.dyn[i].reset();
        if (cs.tex[i]) {
            glDeleteTextures(1, &cs.tex[i]);
            cs.tex[i] = 0;
        }
    }
}
