// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Bassins de Newton — Méthode de Newton pour z^3 = 1 : la couleur dit vers quelle racine chaque point converge, la luminosité combien d'étapes il faut.
// en: Newton basins — Newton's method for z^3 = 1: colour tells which root each point converges to, brightness how many steps it takes.

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / dot(b, b); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 z = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * (2.5 + sin(iTime * 0.3));
    float n = 0.0;
    for (int i = 0; i < 40; i++) {
        vec2 z2 = cmul(z, z);
        vec2 f = cmul(z2, z) - vec2(1.0, 0.0);
        z -= cdiv(f, 3.0 * z2);
        if (dot(f, f) < 1e-8) break;
        n += 1.0;
    }
    float a = atan(z.y, z.x);
    vec3 base = a > 1.0 ? vec3(1.0, 0.3, 0.3) : (a < -1.0 ? vec3(0.3, 0.5, 1.0) : vec3(0.3, 1.0, 0.5));
    fragColor = vec4(base * (1.0 - n / 40.0 * 0.9), 1.0);
}
