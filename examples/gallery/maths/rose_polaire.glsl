// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Roses polaires — La courbe r = cos(k theta) pour un k qui évolue ; l'épaisseur est la distance radiale au rayon de la courbe.
// en: Polar roses — The curve r = cos(k theta) for a changing k; thickness is the radial distance to the curve radius.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float k = 2.0 + 3.0 * (0.5 + 0.5 * sin(iTime * 0.25));
    float d = 1e9;
    for (int i = 0; i < 240; i++) {
        float t = float(i) / 240.0 * 12.5663706;
        float r = 0.42 * cos(k * t);
        d = min(d, length(uv - r * vec2(cos(t), sin(t))));
    }
    vec3 col = vec3(0.05, 0.02, 0.08) + vec3(1.0, 0.35, 0.6) * 0.003 / (d + 0.0015);
    col += vec3(0.1, 0.2, 0.5) * (1.0 - length(uv));
    fragColor = vec4(col, 1.0);
}
