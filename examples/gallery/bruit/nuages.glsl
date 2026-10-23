// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Nuages éclairés — Bruit fractal pris comme densité ; un deuxième échantillon décalé vers le soleil donne l'ombre portée.
// en: Lit clouds — Fractal noise used as density; a second sample shifted towards the sun gives the cast shadow.

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 6; i++) { v += a * vnoise(p); p = p * 2.02 + 7.0; a *= 0.5; }
    return v;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.y;
    vec2 p = uv * 3.0 + vec2(iTime * 0.05, 0.0);
    float d = smoothstep(0.4, 0.8, fbm(p));
    float ds = smoothstep(0.4, 0.8, fbm(p + vec2(0.06, 0.05)));
    vec3 sky = mix(vec3(0.25, 0.5, 0.9), vec3(0.7, 0.85, 1.0), uv.y);
    vec3 cloud = mix(vec3(0.55, 0.6, 0.7), vec3(1.0), clamp(1.0 - (ds - d) * 3.0, 0.0, 1.0));
    fragColor = vec4(mix(sky, cloud, d), 1.0);
}
