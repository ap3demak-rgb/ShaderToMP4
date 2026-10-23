// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Aurore boréale — Rideaux de lumière verte et violette obtenus par accumulation de couches de bruit le long du rayon.
// en: Northern lights — Curtains of green and purple light obtained by accumulating noise layers along the ray.

float h(float n) { return fract(sin(n) * 43758.5453); }
float noise(float x) { float i = floor(x), f = fract(x); return mix(h(i), h(i + 1.0), f * f * (3.0 - 2.0 * f)); }
void mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 ro, in vec3 rd) {
    vec3 col = mix(vec3(0.0, 0.01, 0.03), vec3(0.0, 0.03, 0.08), clamp(rd.y, 0.0, 1.0));
    if (rd.y > 0.02) {
        for (int i = 0; i < 24; i++) {
            float t = (1.0 + float(i) * 0.12) / rd.y;
            vec2 p = rd.xz * t * 0.5;
            float c = noise(p.x * 2.0 + iTime * 0.3) * noise(p.y * 1.5 - iTime * 0.2);
            c = pow(c, 2.5);
            col += mix(vec3(0.1, 1.0, 0.4), vec3(0.6, 0.2, 0.9), float(i) / 24.0) * c * 0.3 * smoothstep(0.02, 0.4, rd.y);
        }
    } else col = vec3(0.0, 0.01, 0.01);
    fragColor = vec4(col, 1.0);
}
