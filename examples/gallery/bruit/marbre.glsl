// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Marbre — Veines de sinus dont la phase est perturbée par une turbulence (somme de valeurs absolues de bruit).
// en: Marble — Sine veins whose phase is disturbed by turbulence (sum of absolute noise values).

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = fragCoord / iResolution.y * 3.0;
    float t = 0.0, a = 1.0;
    vec2 q = p;
    for (int i = 0; i < 5; i++) { t += a * abs(vnoise(q + iTime * 0.1) * 2.0 - 1.0); q *= 2.0; a *= 0.5; }
    float v = 0.5 + 0.5 * sin((p.x + p.y) * 3.0 + t * 8.0);
    vec3 col = mix(vec3(0.1, 0.1, 0.12), vec3(0.95, 0.93, 0.9), pow(v, 0.7));
    fragColor = vec4(col, 1.0);
}
