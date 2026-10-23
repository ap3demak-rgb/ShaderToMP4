// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Mer et vagues — Surface de hauteur marchée par pas fixes puis affinée ; couleur d'eau selon l'angle et ciel dégradé à l'horizon.
// en: Sea waves — Height surface marched in fixed steps then refined; water colour depends on the angle, graded sky at the horizon.

float h(vec2 p) {
    float v = 0.0, a = 0.5, f = 1.0;
    for (int i = 0; i < 5; i++) { v += a * sin(p.x * f + iTime * (0.8 + 0.3 * float(i))) * cos(p.y * f * 0.8 - iTime * 0.5); p = p.yx * 1.2 + 1.3; a *= 0.5; f *= 1.9; }
    return v * 0.5;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec3 ro = vec3(0.0, 1.4, iTime), rd = normalize(vec3(uv.x, uv.y - 0.15, 1.0));
    vec3 sky = mix(vec3(0.9, 0.7, 0.5), vec3(0.2, 0.4, 0.8), clamp(rd.y * 3.0, 0.0, 1.0));
    vec3 col = sky;
    if (rd.y < 0.0) {
        float t = 0.0, lt = 0.0;
        for (int i = 0; i < 120; i++) {
            vec3 p = ro + rd * t;
            float d = p.y - h(p.xz);
            if (d < 0.01) break;
            lt = t; t += max(d * 0.6, 0.02);
            if (t > 80.0) break;
        }
        vec3 p = ro + rd * t;
        vec2 e = vec2(0.02, 0.0);
        vec3 n = normalize(vec3(h(p.xz - e.xy) - h(p.xz + e.xy), 2.0 * e.x, h(p.xz - e.yx) - h(p.xz + e.yx)));
        float fre = pow(1.0 - max(dot(n, -rd), 0.0), 4.0);
        vec3 water = mix(vec3(0.0, 0.12, 0.2), vec3(0.0, 0.3, 0.35), p.y * 0.8 + 0.5);
        col = mix(water, sky, fre + 0.1);
        col = mix(col, sky, smoothstep(30.0, 80.0, t));
    }
    fragColor = vec4(pow(col, vec3(0.5)), 1.0);
}
