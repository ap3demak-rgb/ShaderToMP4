// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Opérations booléennes — Union, intersection et soustraction de formes : un cube creusé par une sphère, percé de trois cylindres.
// en: Boolean operations — Union, intersection and subtraction of shapes: a cube hollowed by a sphere, pierced by three cylinders.

mat2 rot(float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)); }
float map(vec3 p) {
    p.xz *= rot(iTime * 0.5);
    p.yz *= rot(0.5);
    vec3 q = abs(p) - vec3(0.8);
    float box = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
    float sph = length(p) - 1.05;
    float d = max(box, sph);
    float c1 = length(p.xy) - 0.3, c2 = length(p.yz) - 0.3, c3 = length(p.xz) - 0.3;
    d = max(d, -min(c1, min(c2, c3)));
    return d;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec3 ro = vec3(0.0, 0.0, -3.8), rd = normalize(vec3(uv, 1.7));
    float t = 0.0;
    for (int i = 0; i < 90; i++) { float d = map(ro + rd * t); if (d < 0.001 || t > 10.0) break; t += d; }
    vec3 col = vec3(0.08, 0.09, 0.12);
    if (t < 10.0) {
        vec3 p = ro + rd * t;
        vec2 e = vec2(0.001, 0.0);
        vec3 n = normalize(vec3(map(p + e.xyy) - map(p - e.xyy), map(p + e.yxy) - map(p - e.yxy), map(p + e.yyx) - map(p - e.yyx)));
        col = vec3(0.8, 0.7, 0.55) * (0.2 + 0.8 * max(dot(n, normalize(vec3(-0.4, 0.7, -0.6))), 0.0));
    }
    fragColor = vec4(pow(col, vec3(0.4545)), 1.0);
}
