// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Fractale par pliages — Pliage, mise à l'échelle et rotation répétés (système de fonctions itérées) ; on garde la plus petite distance à l'origine.
// en: Folding IFS fractal — Repeated folding, scaling and rotation (iterated function system); the smallest distance to the origin is kept.

mat2 rot(float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 2.2;
    float m = 1e9;
    for (int i = 0; i < 9; i++) {
        p = abs(p) - 0.35 - 0.1 * sin(iTime * 0.3);
        p *= rot(0.5 + 0.3 * sin(iTime * 0.2));
        p *= 1.35;
        m = min(m, length(p));
    }
    vec3 col = 0.5 + 0.5 * cos(m * 3.0 + vec3(0.0, 1.0, 2.0) + iTime * 0.3);
    col *= smoothstep(0.0, 0.4, m) * 0.8 + 0.2;
    fragColor = vec4(col, 1.0);
}
