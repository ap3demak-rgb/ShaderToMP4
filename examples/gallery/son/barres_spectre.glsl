// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Barres de spectre — Trois groupes de barres liés aux graves, aux médiums et aux aigus (iBass, iMid, iTreble), avec une ondulation propre à chaque barre.
// en: Spectrum bars — Three groups of bars tied to bass, mid and treble (iBass, iMid, iTreble), each bar with its own wobble.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float n = 24.0;
    float id = floor(uv.x * n);
    float band = id < 8.0 ? iBass : (id < 16.0 ? iMid : iTreble);
    band = max(band, step(iBass + iMid + iTreble, 0.001) * (0.5 + 0.5 * sin(iTime * 2.0 + id * 0.5)));
    float v = (0.1 + 0.8 * band) * (0.6 + 0.4 * sin(id * 1.7 + iTime * 3.0));
    float on = step(uv.y, v) * step(0.12, fract(uv.x * n)) * step(fract(uv.x * n), 0.88);
    vec3 col = mix(vec3(0.03, 0.03, 0.08), mix(vec3(0.1, 0.9, 0.4), vec3(1.0, 0.2, 0.3), uv.y), on);
    fragColor = vec4(col, 1.0);
}
