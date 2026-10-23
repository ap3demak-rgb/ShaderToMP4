// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Bruit de valeur fractal — Cinq octaves de bruit de valeur interpolé (lissage cubique) ; chaque octave double la fréquence et divise l'amplitude.
// en: Fractal value noise — Five octaves of interpolated value noise (cubic smoothing); each octave doubles the frequency and halves the amplitude.

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = fragCoord / iResolution.y * 4.0 + vec2(iTime * 0.2, 0.0);
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * vnoise(p); p *= 2.0; a *= 0.5; }
    fragColor = vec4(vec3(v), 1.0);
}
