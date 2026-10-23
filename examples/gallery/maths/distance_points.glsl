// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Cellules de distance — Chaque pixel cherche le plus proche de seize points mobiles ; la couleur donne la cellule, les bords sont les médiatrices.
// en: Distance cells — Each pixel looks for the nearest of sixteen moving points; colour gives the cell, borders are the bisectors.

vec2 pt(int i) {
    float f = float(i);
    return 0.45 * vec2(sin(f * 12.9898 + iTime * (0.2 + 0.03 * f)), cos(f * 78.233 + iTime * 0.17));
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float d1 = 1e9, d2 = 1e9;
    int id = 0;
    for (int i = 0; i < 16; i++) {
        float d = length(uv - pt(i));
        if (d < d1) { d2 = d1; d1 = d; id = i; } else if (d < d2) d2 = d;
    }
    vec3 col = 0.5 + 0.5 * cos(float(id) * 0.9 + vec3(0.0, 2.0, 4.0));
    col *= 0.6 + 0.4 * cos(d1 * 40.0);
    col = mix(vec3(0.0), col, smoothstep(0.0, 0.015, d2 - d1));
    fragColor = vec4(col, 1.0);
}
