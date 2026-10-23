// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Champ d'étoiles — Trois couches de points tirés au hasard par cellule, qui défilent à des vitesses différentes (parallaxe) et scintillent.
// en: Starfield — Three layers of random points per cell, scrolling at different speeds (parallax) and twinkling.

float h21(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec3 col = vec3(0.0, 0.0, 0.03);
    for (int l = 0; l < 3; l++) {
        float sc = 12.0 + 14.0 * float(l);
        vec2 p = uv * sc + vec2(iTime * (0.6 + 0.5 * float(l)), 0.0) + float(l) * 17.0;
        vec2 id = floor(p), f = fract(p) - 0.5;
        float r = h21(id);
        vec2 o = (vec2(h21(id + 3.1), h21(id + 7.7)) - 0.5) * 0.6;
        float d = length(f - o);
        float tw = 0.6 + 0.4 * sin(iTime * 4.0 + r * 60.0);
        col += step(0.7, r) * smoothstep(0.1, 0.0, d) * tw * mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.8, 0.6), r);
    }
    fragColor = vec4(col, 1.0);
}
