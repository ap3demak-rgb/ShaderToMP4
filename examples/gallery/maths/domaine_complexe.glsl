// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Coloration du plan complexe — La teinte est l'argument de f(z) = (z^3 - 1) / (z^2 + a) et la luminosité suit son module ; a tourne.
// en: Complex plane colouring — Hue is the argument of f(z) = (z^3 - 1) / (z^2 + a), brightness follows its modulus; a rotates.

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return cmul(a, vec2(b.x, -b.y)) / dot(b, b); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 z = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 3.5;
    vec2 a = 0.8 * vec2(cos(iTime * 0.3), sin(iTime * 0.3));
    vec2 z2 = cmul(z, z), z3 = cmul(z2, z);
    vec2 f = cdiv(z3 - vec2(1.0, 0.0), z2 + a);
    float h = atan(f.y, f.x) / 6.2831853;
    float m = length(f);
    vec3 col = 0.5 + 0.5 * cos(6.2831853 * (h + vec3(0.0, 0.33, 0.67)));
    col *= 0.45 + 0.55 * abs(sin(3.14159265 * log2(m + 1e-4)));
    fragColor = vec4(col, 1.0);
}
