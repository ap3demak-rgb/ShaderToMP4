// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Déformation de domaine — Le bruit est lu à des coordonnées elles-mêmes déformées par du bruit : f(p + f(p + f(p))).
// en: Domain warping — Noise is read at coordinates that are themselves warped by noise: f(p + f(p + f(p))).

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * vnoise(p); p = p * 2.0 + 3.1; a *= 0.5; }
    return v;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = fragCoord / iResolution.y * 3.0;
    float t = iTime * 0.15;
    vec2 q = vec2(fbm(p + t), fbm(p + vec2(5.2, 1.3) - t));
    vec2 r = vec2(fbm(p + 4.0 * q + vec2(1.7, 9.2)), fbm(p + 4.0 * q + vec2(8.3, 2.8)));
    float f = fbm(p + 4.0 * r);
    vec3 col = mix(vec3(0.1, 0.1, 0.4), vec3(0.9, 0.5, 0.2), f);
    col = mix(col, vec3(0.0, 0.2, 0.3), dot(q, q));
    col = mix(col, vec3(1.0), r.x * r.x * 0.8);
    fragColor = vec4(col * (0.5 + f), 1.0);
}
