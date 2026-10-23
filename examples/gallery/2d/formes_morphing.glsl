// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Formes qui se transforment — Cercle, carré arrondi et étoile mélangés par interpolation de leurs distances signées.
// en: Morphing shapes — Circle, rounded square and star blended by interpolating their signed distances.

float sdCircle(vec2 p, float r) { return length(p) - r; }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - 0.05; }
float sdStar(vec2 p, float r) {
    float a = atan(p.y, p.x), k = 6.2831853 / 5.0;
    float m = mod(a + k * 0.5, k) - k * 0.5;
    return length(p) * cos(m - 0.0) - r * (0.75 + 0.25 * cos(m * 5.0));
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float t = fract(iTime / 6.0) * 3.0;
    float a = sdCircle(uv, 0.3), b = sdBox(uv, vec2(0.25)), c = sdStar(uv, 0.4);
    float d = t < 1.0 ? mix(a, b, smoothstep(0.0, 1.0, t)) : (t < 2.0 ? mix(b, c, smoothstep(1.0, 2.0, t)) : mix(c, a, smoothstep(2.0, 3.0, t)));
    vec3 col = mix(vec3(0.05, 0.06, 0.12), vec3(0.2, 0.3, 0.6), 0.5 + 0.5 * cos(d * 60.0));
    col = mix(col, vec3(1.0, 0.8, 0.3), smoothstep(0.004, 0.0, d));
    fragColor = vec4(col, 1.0);
}
