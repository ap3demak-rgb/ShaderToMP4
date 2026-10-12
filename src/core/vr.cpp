// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "vr.h"
#include "app_info.h"
#include "compat.h"
#include "hooks.h"
#include "i18n.h"
#include "render.h"
#include "util.h"
#include "platform/platform.h"

bool vrModeFor(const std::string& code, const Settings& s, int& mode, std::wstring& err) {
    const bool hasVR = codeUsesWord(code, "mainVR"), hasImage = codeUsesWord(code, "mainImage");
    mode = 0;
    if (s.vr == "off") return true;
    if (s.vr == "mono" || s.vr == "stereo" || s.vr == "sbs" || s.vr == "anaglyph" || s.vr == "fulldome") {
        if (!hasVR) {
            err = TX("error.vr_missing");
            return false;
        }
        mode = s.vr == "stereo" ? 2 : s.vr == "sbs" ? 3 : s.vr == "anaglyph" ? 4 : s.vr == "fulldome" ? 5 : 1;
        return true;
    }
    if (hasVR && !hasImage) mode = 1;
    return true;
}

bool vrAspectOk(int width, int height, int mode) {
    if (mode == 0 || height <= 0) return true;
    const double want = mode == 2 || mode == 5 || mode == 6 ? 1.0 : mode == 3 ? 4.0 : 2.0, got = (double)width / height;
    return std::fabs(got - want) <= 0.01 * want;
}

const wchar_t* vrAspectText(int mode) { return mode == 2 || mode == 5 || mode == 6 ? L"1:1" : mode == 3 ? L"4:1" : L"2:1"; }

void vrEyeSize(int mode, int width, int height, int& eyeW, int& eyeH) {
    eyeW = mode == 3 ? width / 2 : width;
    eyeH = mode == 2 ? height / 2 : height;
}

bool renderCubeFaces(const std::wstring& shaderPath, const std::wstring& exeDir, const Settings& s, const Hooks& H, std::vector<std::wstring>& files,
                     std::wstring& err) {
    files.clear();
    for (int face = 0; face < 6; face++) {
        Hooks H2 = H;
        H2.snapshot = true;
        H2.cubeFace = face;
        H2.thumbTime = -1;
        H2.sheetCols = H2.sheetRows = 0;
        H2.progress = nullptr;
        H2.output = [&](const std::wstring& path) {
            files.push_back(path);
            if (H.output) H.output(path);
        };
        if (convertFile(shaderPath, exeDir, s, H2, err) != Result::Ok) return false;
    }
    return true;
}

const wchar_t* vrFaceName(int face) {
    static const wchar_t* const names[6] = {L"px", L"nx", L"py", L"ny", L"pz", L"nz"};
    return names[face < 0 || face > 5 ? 0 : face];
}

// --- Metadonnees « Spherical Video V1 » dans un MP4 / MOV --------------------------------------------------------------
namespace {

typedef std::vector<uint8_t> Bytes;

uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
uint64_t get64(const uint8_t* p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }
void put64(uint8_t* p, uint64_t v) {
    put32(p, (uint32_t)(v >> 32));
    put32(p + 4, (uint32_t)v);
}

int64_t tell64(FILE* f) {
#ifdef _WIN32
    return _ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}
bool seek64(FILE* f, int64_t pos) {
#ifdef _WIN32
    return _fseeki64(f, pos, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)pos, SEEK_SET) == 0;
#endif
}

// Boite dont l'en-tete (taille 32 bits + type) est a `at` dans buf ; end = fin du conteneur
struct Box {
    size_t at = 0, size = 0;
    bool ok = false;
    std::string type() const { return type_; }
    std::string type_;
};

Box boxAt(const Bytes& buf, size_t at, size_t end) {
    Box b;
    if (at + 8 > end) return b;
    const uint32_t sz = get32(&buf[at]);
    if (sz < 8 || at + sz > end) return b;  // tailles 64 bits ou « jusqu'a la fin » : non gerees dans moov
    b.at = at;
    b.size = sz;
    b.type_.assign((const char*)&buf[at + 4], 4);
    b.ok = true;
    return b;
}

// Premier enfant `type` de la boite `parent` (en-tete de 8 octets), ou une boite non valide
Box child(const Bytes& buf, const Box& parent, const char* type, size_t skip = 0) {
    for (size_t p = parent.at + 8 + skip; p + 8 <= parent.at + parent.size;) {
        const Box b = boxAt(buf, p, parent.at + parent.size);
        if (!b.ok) break;
        if (b.type_ == type) return b;
        p += b.size;
    }
    return Box();
}

const uint8_t kSphericalUuid[16] = {0xff, 0xcc, 0x82, 0x63, 0xf8, 0x55, 0x4a, 0x93, 0x88, 0x14, 0x58, 0x7a, 0x02, 0x52, 0x1f, 0xdd};

bool containsUuid(const Bytes& buf, const Box& b) {
    for (size_t p = b.at + 8; p + 24 <= b.at + b.size;) {
        const Box c = boxAt(buf, p, b.at + b.size);
        if (!c.ok) break;
        if (c.type_ == "uuid" && c.size >= 24 && memcmp(&buf[p + 8], kSphericalUuid, 16) == 0) return true;
        p += c.size;
    }
    return false;
}

bool copyRange(FILE* in, FILE* out, int64_t from, int64_t to) {
    if (!seek64(in, from)) return false;
    std::vector<char> chunk(1 << 20);
    int64_t left = to - from;
    while (left > 0) {
        const size_t want = (size_t)std::min<int64_t>(left, (int64_t)chunk.size());
        const size_t got = fread(chunk.data(), 1, want, in);
        if (got == 0 || fwrite(chunk.data(), 1, got, out) != got) return false;
        left -= (int64_t)got;
    }
    return true;
}

}  // namespace

bool injectSphericalMetadata(const std::wstring& path, int stereoMode, std::wstring& err) {
    FILE* in = platform::openFile(path, "rb");
    if (!in) {
        err = TXF("error.vr_meta", path.c_str());
        return false;
    }
    auto fail = [&]() {
        fclose(in);
        err = TXF("error.vr_meta", path.c_str());
        return false;
    };
    // boites de premier niveau : position de moov et du premier mdat
    int64_t fileSize = 0, pos = 0, moovAt = -1, moovSize = 0, mdatAt = -1;
    fseek(in, 0, SEEK_END);
    fileSize = tell64(in);
    while (pos + 8 <= fileSize) {
        uint8_t h[16];
        if (!seek64(in, pos) || fread(h, 1, 8, in) != 8) return fail();
        uint64_t sz = get32(h);
        size_t hdr = 8;
        if (sz == 1) {
            if (fread(h + 8, 1, 8, in) != 8) return fail();
            sz = get64(h + 8);
            hdr = 16;
        } else if (sz == 0) {
            sz = (uint64_t)(fileSize - pos);
        }
        if (sz < hdr || pos + (int64_t)sz > fileSize) return fail();
        const std::string type((const char*)h + 4, 4);
        if (type == "moov") {
            moovAt = pos;
            moovSize = (int64_t)sz;
            if (hdr != 8) return fail();
        } else if (type == "mdat" && mdatAt < 0) {
            mdatAt = pos;
        }
        pos += (int64_t)sz;
    }
    if (moovAt < 0 || moovSize > (int64_t)256 << 20) return fail();
    Bytes moov((size_t)moovSize);
    if (!seek64(in, moovAt) || fread(moov.data(), 1, moov.size(), in) != moov.size()) return fail();

    // piste video : trak / mdia / hdlr = « vide »
    const Box moovBox = boxAt(moov, 0, moov.size());
    if (!moovBox.ok) return fail();
    Box video;
    std::vector<Box> traks;
    for (size_t p = 8; p + 8 <= moov.size();) {
        const Box b = boxAt(moov, p, moov.size());
        if (!b.ok) break;
        if (b.type_ == "trak") {
            traks.push_back(b);
            const Box mdia = child(moov, b, "mdia");
            const Box hdlr = mdia.ok ? child(moov, mdia, "hdlr") : Box();
            if (!video.ok && hdlr.ok && hdlr.size >= 20 + 4 && memcmp(&moov[hdlr.at + 16], "vide", 4) == 0) video = b;
        }
        p += b.size;
    }
    if (!video.ok) return fail();
    if (containsUuid(moov, video)) {  // deja fait
        fclose(in);
        return true;
    }

    // boite uuid : identifiant + XML
    std::string xml =
        "<?xml version=\"1.0\"?><rdf:SphericalVideo xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
        "xmlns:GSpherical=\"http://ns.google.com/videos/1.0/spherical/\"><GSpherical:Spherical>true</GSpherical:Spherical>"
        "<GSpherical:Stitched>true</GSpherical:Stitched><GSpherical:StitchingSoftware>" APP_NAME_STR "</GSpherical:StitchingSoftware>"
        "<GSpherical:ProjectionType>equirectangular</GSpherical:ProjectionType>";
    if (stereoMode == 1) xml += "<GSpherical:StereoMode>top-bottom</GSpherical:StereoMode>";
    else if (stereoMode == 2) xml += "<GSpherical:StereoMode>left-right</GSpherical:StereoMode>";
    xml += "</rdf:SphericalVideo>";
    Bytes box(8 + 16 + xml.size());
    put32(&box[0], (uint32_t)box.size());
    memcpy(&box[4], "uuid", 4);
    memcpy(&box[8], kSphericalUuid, 16);
    memcpy(&box[24], xml.data(), xml.size());
    const size_t delta = box.size();

    // Si moov precede mdat (faststart), les positions des blocs de donnees se decalent de delta : stco / co64 de chaque piste
    if (mdatAt > moovAt) {
        for (const Box& t : traks) {
            const Box mdia = child(moov, t, "mdia");
            const Box minf = mdia.ok ? child(moov, mdia, "minf") : Box();
            const Box stbl = minf.ok ? child(moov, minf, "stbl") : Box();
            if (!stbl.ok) continue;
            const Box stco = child(moov, stbl, "stco"), co64 = child(moov, stbl, "co64");
            if (stco.ok && stco.size >= 16) {
                const uint32_t n = get32(&moov[stco.at + 12]);
                if (16 + (size_t)n * 4 > stco.size) return fail();
                for (uint32_t i = 0; i < n; i++) {
                    uint8_t* e = &moov[stco.at + 16 + (size_t)i * 4];
                    const uint64_t v = (uint64_t)get32(e) + delta;
                    if (v > 0xFFFFFFFFull) return fail();
                    put32(e, (uint32_t)v);
                }
            } else if (co64.ok && co64.size >= 16) {
                const uint32_t n = get32(&moov[co64.at + 12]);
                if (16 + (size_t)n * 8 > co64.size) return fail();
                for (uint32_t i = 0; i < n; i++) {
                    uint8_t* e = &moov[co64.at + 16 + (size_t)i * 8];
                    put64(e, get64(e) + delta);
                }
            }
        }
    }
    // insertion a la fin de la piste video, tailles de la piste et de moov mises a jour
    const size_t videoEnd = video.at + video.size;
    put32(&moov[video.at], (uint32_t)(video.size + delta));
    put32(&moov[0], (uint32_t)(moov.size() + delta));
    moov.insert(moov.begin() + (std::ptrdiff_t)videoEnd, box.begin(), box.end());

    const std::wstring tmp = path + L".vr.tmp";
    FILE* out = platform::openFile(tmp, "wb");
    if (!out) return fail();
    bool ok = copyRange(in, out, 0, moovAt) && fwrite(moov.data(), 1, moov.size(), out) == moov.size() &&
              copyRange(in, out, moovAt + moovSize, fileSize);
    ok = (fclose(out) == 0) && ok;
    fclose(in);
    if (!ok || !platform::moveFile(tmp, path, true)) {
        platform::removeFile(tmp);
        err = TXF("error.vr_meta", path.c_str());
        return false;
    }
    return true;
}
