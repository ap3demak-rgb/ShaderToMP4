// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "dpx.h"
#include "util.h"

namespace {

const size_t kFilmFrameRate = 1664 + 60;       // en-tete film : cadence (R32)
const size_t kTvTemporalRate = 1664 + 256 + 20;  // en-tete television : cadence d'echantillonnage temporel (R32)
const size_t kGenericEnd = 1664;                // fin des en-tetes ecrits par ffmpeg

bool bigEndian(const std::string& d) { return d.compare(0, 4, "SDPX") == 0; }

uint32_t get32(const std::string& d, size_t o) {
    uint32_t v;
    memcpy(&v, d.data() + o, 4);
    if (bigEndian(d)) v = ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
    return v;
}
void set32(std::string& d, size_t o, uint32_t v) {
    if (bigEndian(d)) v = ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
    memcpy(&d[o], &v, 4);
}
void setFloat(std::string& d, size_t o, float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    set32(d, o, v);
}

}  // namespace

bool dpxWriteFrameRate(const std::wstring& path, double fps) {
    std::string d;
    if (!readFile(path, d) || d.size() < kGenericEnd) return false;
    if (d.compare(0, 4, "XPDS") != 0 && d.compare(0, 4, "SDPX") != 0) return false;
    const uint32_t dataOffset = get32(d, 4);
    if (dataOffset >= kGenericEnd + 384) return true;  // les en-tetes film et television existent deja
    if (dataOffset != kGenericEnd) return false;
    d.insert(kGenericEnd, std::string(384, '\0'));
    set32(d, 4, (uint32_t)(dataOffset + 384));           // debut des donnees
    set32(d, 16, get32(d, 16) + 384);                    // taille du fichier
    set32(d, 28, 384);                                   // taille de l'en-tete « industrie » (film + television)
    setFloat(d, kFilmFrameRate, (float)fps);
    setFloat(d, kTvTemporalRate, (float)fps);
    return writeFileAtomic(path, d);
}

double dpxFrameRate(const std::wstring& path) {
    std::string d;
    if (!readFile(path, d) || d.size() < kTvTemporalRate + 4) return 0.0;
    if (d.compare(0, 4, "XPDS") != 0 && d.compare(0, 4, "SDPX") != 0) return 0.0;
    uint32_t v = get32(d, kTvTemporalRate);
    float f;
    memcpy(&f, &v, 4);
    return std::isfinite(f) && f > 0 && f < 100000 ? (double)f : 0.0;
}
