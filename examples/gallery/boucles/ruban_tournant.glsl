// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Ruban tournant — Une bande sinusoïdale dont la phase avance d'un tour complet pendant la boucle ; la couleur suit la phase.
// en: Rotating ribbon — A sinusoidal band whose phase advances one full turn during the loop; colour follows the phase.

// duration=5
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float ph = 6.2831853 * fract(iTime / 5.0);
    vec3 col = vec3(0.02, 0.03, 0.08);
    for (int i = 0; i < 20; i++) {
        float f = float(i) / 20.0;
        float y = 0.3 * sin(uv.x * 6.0 + ph + f * 3.0) * (0.3 + f);
        float d = abs(uv.y - y - (f - 0.5) * 0.4);
        col += (0.5 + 0.5 * cos(ph + f * 6.0 + vec3(0.0, 2.0, 4.0))) * 0.0015 / (d + 0.002);
    }
    fragColor = vec4(col, 1.0);
}
