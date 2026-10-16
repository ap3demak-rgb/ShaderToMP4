// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "exrtools.h"
#include "exr.h"
#include "i18n.h"
#include "util.h"

namespace {

uint32_t crc32(const std::string& s) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        built = true;
    }
    uint32_t c = 0xffffffffu;
    for (unsigned char b : s) c = table[(c ^ b) & 255] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void be32(std::string& s, uint32_t v) {
    for (int k = 3; k >= 0; k--) s.push_back((char)((v >> (8 * k)) & 255));
}

void chunk(std::string& png, const char* type, const std::string& data) {
    be32(png, (uint32_t)data.size());
    std::string body = std::string(type, 4) + data;
    png += body;
    be32(png, crc32(body));
}

}  // namespace

bool exrSummary(const std::wstring& path, std::vector<std::wstring>& lines, std::wstring& err) {
    exr::Image img;
    if (!exr::read(path, img, err)) return false;
    lines.push_back(TXF("log.exr_info", img.width, img.height, (int)img.channels.size()));
    std::vector<const exr::Channel*> sorted;
    for (const exr::Channel& c : img.channels) sorted.push_back(&c);
    std::sort(sorted.begin(), sorted.end(), [](const exr::Channel* a, const exr::Channel* b) { return a->name < b->name; });
    for (const exr::Channel* c : sorted) lines.push_back(TXF("log.exr_channel", W(c->name).c_str(), c->full ? L"32" : L"16"));
    for (const auto& t : img.text) lines.push_back(TXF("log.exr_attr", W(t.first).c_str(), W(t.second).c_str()));
    for (const auto& t : img.numbers) lines.push_back(TXF("log.exr_attr", W(t.first).c_str(), wfmt(L"%g", (double)t.second).c_str()));
    return true;
}

bool writeGray16Png(const std::wstring& path, int width, int height, const std::vector<uint16_t>& pixels) {
    if (width <= 0 || height <= 0 || pixels.size() != (size_t)width * height) return false;
    std::string raw;
    raw.reserve((size_t)height * (1 + (size_t)width * 2));
    for (int y = 0; y < height; y++) {
        raw.push_back('\0');  // filtre « aucun »
        for (int x = 0; x < width; x++) {
            const uint16_t v = pixels[(size_t)y * width + x];
            raw.push_back((char)(v >> 8));
            raw.push_back((char)(v & 255));
        }
    }
    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    be32(ihdr, (uint32_t)width);
    be32(ihdr, (uint32_t)height);
    ihdr.push_back(16);  // profondeur
    ihdr.push_back(0);   // niveaux de gris
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", exr::zlibCompress(raw));
    chunk(png, "IEND", "");
    return writeFileAtomic(path, png);
}

bool exrDepthMap(const std::wstring& exrPath, const std::wstring& pngPath, double nearZ, double farZ, bool invert, std::wstring& err) {
    exr::Image img;
    if (!exr::read(exrPath, img, err)) return false;
    const exr::Channel* z = img.find("depth.Z");
    if (!z) {
        err = TXF("error.depthmap_missing", exrPath.c_str());
        return false;
    }
    double lo = nearZ, hi = farZ;
    bool normalised = false;
    if (!(hi > lo)) {  // bornes declarees dans le fichier : la profondeur y est deja normalisee
        bool haveNear = false, haveFar = false;
        for (const auto& n : img.numbers) {
            if (n.first == "depthNear") haveNear = true;
            if (n.first == "depthFar") haveFar = true;
        }
        if (!haveNear || !haveFar) {
            err = TXF("error.depthmap_range", exrPath.c_str());
            return false;
        }
        normalised = true;
    }
    std::vector<uint16_t> out(z->pixels.size());
    for (size_t i = 0; i < out.size(); i++) {
        double v = normalised ? z->pixels[i] : (z->pixels[i] - lo) / (hi - lo);
        v = std::min(1.0, std::max(0.0, v));
        if (invert) v = 1.0 - v;
        out[i] = (uint16_t)std::lround(v * 65535.0);
    }
    if (!writeGray16Png(pngPath, img.width, img.height, out)) {
        err = TXF("error.write_failed", pngPath.c_str());
        return false;
    }
    return true;
}
