// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Tunnel à 360° — Vu de l'intérieur d'un cylindre infini : les motifs se répètent le long de l'axe Z et défilent avec le temps.
// en: 360° tunnel — Seen from inside an infinite cylinder: patterns repeat along the Z axis and scroll with time.

void mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 ro, in vec3 rd) {
    float r = length(rd.xy) + 1e-4;
    float z = rd.z / r * 1.0 - iTime * 0.8;
    float a = atan(rd.y, rd.x);
    float p = step(0.5, fract(z)) + step(0.5, fract(a * 4.0 / 3.14159265));
    vec3 col = mix(vec3(0.05, 0.1, 0.3), vec3(1.0, 0.5, 0.1), mod(p, 2.0));
    col *= clamp(r * 1.5, 0.15, 1.0);
    fragColor = vec4(col, 1.0);
}
