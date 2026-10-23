// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Planètes autour de vous — Trois sphères lointaines testées par intersection rayon-sphère, avec un éclairage simple venant d'une étoile.
// en: Planets around you — Three distant spheres tested by ray-sphere intersection, with simple lighting from a star.

void mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 ro, in vec3 rd) {
    vec3 col = vec3(0.0, 0.0, 0.03);
    vec3 L = normalize(vec3(-1.0, 0.4, 0.3));
    for (int i = 0; i < 3; i++) {
        float f = float(i);
        float a = f * 2.1 + iTime * 0.05 * (1.0 + f);
        vec3 c = (6.0 + 3.0 * f) * vec3(cos(a), 0.25 * sin(f * 3.0), sin(a));
        float R = 1.2 + 0.8 * f;
        vec3 oc = ro - c;
        float b = dot(oc, rd), disc = b * b - (dot(oc, oc) - R * R);
        if (disc > 0.0) {
            float t = -b - sqrt(disc);
            if (t > 0.0) {
                vec3 n = normalize(ro + rd * t - c);
                vec3 base = 0.5 + 0.5 * cos(f * 2.0 + vec3(0.0, 2.0, 4.0));
                col = base * (0.05 + 0.95 * max(dot(n, L), 0.0)) * (0.7 + 0.3 * sin(n.y * 12.0 + f));
            }
        }
    }
    fragColor = vec4(col, 1.0);
}
