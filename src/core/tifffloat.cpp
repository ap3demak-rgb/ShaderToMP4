// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "tifffloat.h"
#include "i18n.h"
#include "util.h"

namespace tifffloat {

namespace {

void put16(std::string& s, uint16_t v) { s.append((const char*)&v, 2); }
void put32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }
// Entree de repertoire : balise, type (3 SHORT, 4 LONG), nombre de valeurs, valeur ou decalage
void entry(std::string& s, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
    put16(s, tag);
    put16(s, type);
    put32(s, count);
    if (type == 3 && count == 1) {  // un SHORT seul : a gauche dans le champ de 4 octets
        put16(s, (uint16_t)value);
        put16(s, 0);
    } else {
        put32(s, value);
    }
}

}  // namespace

bool write(const std::wstring& path, const Image& img, std::wstring& err) {
    const int n = img.samples;
    if ((n != 3 && n != 4) || img.width <= 0 || img.height <= 0 || img.pixels.size() != (size_t)img.width * img.height * n) {
        err = TXF("error.write_failed", path.c_str());
        return false;
    }
    const int entries = n == 4 ? 12 : 11;
    const uint32_t ifdSize = 2 + (uint32_t)entries * 12 + 4;
    const uint32_t bitsOff = 8 + ifdSize, fmtOff = bitsOff + (uint32_t)n * 2, dataOff = fmtOff + (uint32_t)n * 2;
    const uint32_t bytes = (uint32_t)((size_t)img.width * img.height * n * 4);
    std::string f;
    f.append("II", 2);
    put16(f, 42);
    put32(f, 8);
    put16(f, (uint16_t)entries);
    entry(f, 256, 4, 1, (uint32_t)img.width);
    entry(f, 257, 4, 1, (uint32_t)img.height);
    entry(f, 258, 3, (uint32_t)n, bitsOff);
    entry(f, 259, 3, 1, 1);  // sans compression
    entry(f, 262, 3, 1, 2);  // RGB
    entry(f, 273, 4, 1, dataOff);
    entry(f, 277, 3, 1, (uint32_t)n);
    entry(f, 278, 4, 1, (uint32_t)img.height);
    entry(f, 279, 4, 1, bytes);
    entry(f, 284, 3, 1, 1);  // entrelace
    if (n == 4) entry(f, 338, 3, 1, img.premultiplied ? 1 : 2);
    entry(f, 339, 3, (uint32_t)n, fmtOff);
    put32(f, 0);
    for (int i = 0; i < n; i++) put16(f, 32);
    for (int i = 0; i < n; i++) put16(f, 3);  // IEEE flottant
    f.append((const char*)img.pixels.data(), bytes);
    if (!writeFileAtomic(path, f)) {
        err = TXF("error.write_failed", path.c_str());
        return false;
    }
    return true;
}

bool read(const std::wstring& path, Image& img, std::wstring& err) {
    std::string f;
    if (!readFile(path, f) || f.size() < 16 || f.compare(0, 4, std::string("II\x2a\x00", 4)) != 0) {
        err = TXF("error.tiff_read", path.c_str());
        return false;
    }
    auto u16 = [&](size_t o) { uint16_t v = 0; if (o + 2 <= f.size()) memcpy(&v, f.data() + o, 2); return v; };
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= f.size()) memcpy(&v, f.data() + o, 4); return v; };
    const uint32_t ifd = u32(4);
    const int count = u16(ifd);
    uint32_t w = 0, h = 0, off = 0, spp = 0, bytes = 0;
    int extra = 0;
    bool floatOk = true, plain = true;
    for (int i = 0; i < count; i++) {
        const size_t e = ifd + 2 + (size_t)i * 12;
        const uint16_t tag = u16(e), type = u16(e + 2);
        const uint32_t n = u32(e + 4);
        const uint32_t val = type == 3 && n == 1 ? u16(e + 8) : u32(e + 8);
        switch (tag) {
            case 256: w = val; break;
            case 257: h = val; break;
            case 258:
                if (n > 8) floatOk = false;  // au plus quatre canaux : un compte enorme d'un fichier abime ne fait pas boucler
                for (uint32_t k = 0; floatOk && k < n; k++) if (u16(val + k * 2) != 32) floatOk = false;
                break;
            case 259: if (val != 1) plain = false; break;
            case 273: off = val; break;
            case 277: spp = val; break;
            case 279: bytes = val; break;
            case 338: extra = (int)val; break;
            case 339:
                if (n > 8) floatOk = false;
                for (uint32_t k = 0; floatOk && k < n; k++) if (u16(val + k * 2) != 3) floatOk = false;
                break;
        }
    }
    if (!floatOk || !plain || (spp != 3 && spp != 4) || w == 0 || h == 0 || (uint64_t)bytes != (uint64_t)w * h * spp * 4 || (uint64_t)off + bytes > f.size()) {  // 64 bits : sans debordement
        err = TXF("error.tiff_read", path.c_str());
        return false;
    }
    img.width = (int)w;
    img.height = (int)h;
    img.samples = (int)spp;
    img.premultiplied = extra == 1;
    img.pixels.resize((size_t)w * h * spp);
    memcpy(img.pixels.data(), f.data() + off, bytes);
    return true;
}

}  // namespace tifffloat
