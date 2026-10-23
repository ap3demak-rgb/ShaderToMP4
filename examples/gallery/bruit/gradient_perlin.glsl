// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Bruit de gradient — Chaque coin de grille porte un gradient aléatoire ; le produit scalaire interpolé donne un bruit sans carrés visibles.
// en: Gradient noise — Each grid corner carries a random gradient; the interpolated dot product gives noise without visible squares.

vec2 grad(vec2 i) {
    float a = 6.2831853 * fract(sin(dot(i, vec2(12.9898, 78.233))) * 43758.5453);
    return vec2(cos(a), sin(a));
}
float gnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(grad(i), f), b = dot(grad(i + vec2(1, 0)), f - vec2(1, 0));
    float c = dot(grad(i + vec2(0, 1)), f - vec2(0, 1)), d = dot(grad(i + vec2(1, 1)), f - vec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = fragCoord / iResolution.y * 6.0;
    float n = gnoise(p + vec2(iTime * 0.3, 0.0)) * 0.5 + 0.5;
    vec3 col = mix(vec3(0.05, 0.1, 0.3), vec3(1.0, 0.8, 0.5), n);
    col += 0.2 * step(0.5, fract(n * 8.0)) * 0.3;
    fragColor = vec4(col, 1.0);
}
