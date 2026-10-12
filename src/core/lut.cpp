// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "lut.h"
#include "i18n.h"
#include "util.h"

bool loadCubeLut(const std::wstring& path, Lut& out, std::wstring& err) {
    std::string text;
    if (!readFile(path, text)) {
        err = TXF("error.lut_read", path.c_str());
        return false;
    }
    Lut lut;
    long long dimSize1 = 0, dimSize3 = 0;
    std::vector<float> values;  // r g b, r g b...
    int lineNo = 0;
    size_t pos = 0;
    auto fail = [&](const char* key) {
        err = TXF(key, path.c_str(), lineNo);
        return false;
    };
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        lineNo++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;
        if (isalpha((unsigned char)line[0])) {  // mot-cle : TITLE, DOMAIN_MIN, DOMAIN_MAX, LUT_1D_SIZE, LUT_3D_SIZE
            const size_t sp = line.find_first_of(" \t");
            const std::string key = sp == std::string::npos ? line : line.substr(0, sp);
            const std::string rest = sp == std::string::npos ? "" : trim(line.substr(sp));
            if (key == "TITLE" || key == "LUT_1D_INPUT_RANGE" || key == "LUT_3D_INPUT_RANGE") continue;
            if (key == "LUT_1D_SIZE" || key == "LUT_3D_SIZE") {
                char* end = nullptr;
                const long long n = strtoll(rest.c_str(), &end, 10);
                if (rest.empty() || *end != 0 || n < 2 || n > (key == "LUT_1D_SIZE" ? 256 : 65)) return fail("error.lut_size");
                if ((key == "LUT_1D_SIZE" ? dimSize1 : dimSize3) != 0) return fail("error.lut_size");
                (key == "LUT_1D_SIZE" ? dimSize1 : dimSize3) = n;
                continue;
            }
            if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
                float v[3];
                if (sscanf(rest.c_str(), "%f %f %f", &v[0], &v[1], &v[2]) != 3) return fail("error.lut_domain");
                for (int i = 0; i < 3; i++) (key == "DOMAIN_MIN" ? lut.dmin : lut.dmax)[i] = v[i];
                continue;
            }
            return fail("error.lut_keyword");
        }
        float v[3];
        char extra = 0;
        if (sscanf(line.c_str(), "%f %f %f %c", &v[0], &v[1], &v[2], &extra) != 3) return fail("error.lut_data");
        for (float x : v)
            if (!std::isfinite(x)) return fail("error.lut_data");
        values.insert(values.end(), v, v + 3);
    }
    if ((dimSize1 != 0) == (dimSize3 != 0)) {  // exactement une des deux tailles
        lineNo = 0;
        return fail("error.lut_size");
    }
    for (int i = 0; i < 3; i++)
        if (!(lut.dmax[i] > lut.dmin[i])) {
            lineNo = 0;
            return fail("error.lut_domain");
        }
    lut.dim = dimSize3 ? 3 : 1;
    lut.size = (int)(dimSize3 ? dimSize3 : dimSize1);
    const size_t want = dimSize3 ? (size_t)lut.size * lut.size * lut.size : (size_t)lut.size;
    if (values.size() != want * 3) {
        err = TXF("error.lut_count", path.c_str(), (long long)(values.size() / 3), (long long)want);
        return false;
    }
    lut.rgba.resize(want * 4);
    for (size_t i = 0; i < want; i++) {
        lut.rgba[i * 4 + 0] = values[i * 3 + 0];
        lut.rgba[i * 4 + 1] = values[i * 3 + 1];
        lut.rgba[i * 4 + 2] = values[i * 3 + 2];
        lut.rgba[i * 4 + 3] = 1.0f;
    }
    out = std::move(lut);
    return true;
}
