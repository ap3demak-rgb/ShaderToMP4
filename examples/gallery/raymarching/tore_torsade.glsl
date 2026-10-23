// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Tore en rotation — Distance exacte d'un tore, tourné par une matrice ; l'éclairage ajoute une teinte de bord (Fresnel).
// en: Rotating torus — Exact torus distance, rotated by a matrix; the lighting adds an edge tint (Fresnel).

mat2 rot(float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)); }
float map(vec3 p) {
    p.xz *= rot(iTime * 0.6);
    p.xy *= rot(iTime * 0.4);
    vec2 q = vec2(length(p.xz) - 1.0, p.y);
    return length(q) - 0.35;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec3 ro = vec3(0.0, 0.0, -3.5), rd = normalize(vec3(uv, 1.5));
    float t = 0.0;
    for (int i = 0; i < 80; i++) { float d = map(ro + rd * t); if (d < 0.001 || t > 10.0) break; t += d; }
    vec3 col = vec3(0.04, 0.05, 0.1) + 0.1 * uv.y;
    if (t < 10.0) {
        vec3 p = ro + rd * t;
        vec2 e = vec2(0.001, 0.0);
        vec3 n = normalize(vec3(map(p + e.xyy) - map(p - e.xyy), map(p + e.yxy) - map(p - e.yxy), map(p + e.yyx) - map(p - e.yyx)));
        float dif = max(dot(n, normalize(vec3(0.5, 0.7, -0.6))), 0.0);
        float fre = pow(1.0 + dot(rd, n), 3.0);
        col = vec3(0.9, 0.5, 0.2) * (0.1 + dif) + vec3(0.3, 0.6, 1.0) * fre;
    }
    fragColor = vec4(pow(col, vec3(0.4545)), 1.0);
}
