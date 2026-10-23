// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Zoom sur Mandelbrot — Itération z = z^2 + c avec comptage lissé de l'échappement ; le zoom reste en double précision du GPU (limité à 1e-5 environ).
// en: Mandelbrot zoom — Iteration z = z^2 + c with smooth escape counting; the zoom stays in GPU single precision (limited to about 1e-5).

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float zoom = exp(-5.0 * (0.5 - 0.5 * cos(iTime * 0.2)));
    vec2 c = vec2(-0.743643887, 0.131825904) + (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 2.0 * zoom;
    vec2 z = vec2(0.0);
    float n = 0.0;
    for (int i = 0; i < 300; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (dot(z, z) > 256.0) break;
        n += 1.0;
    }
    float s = n - log2(log2(dot(z, z))) + 4.0;
    vec3 col = n >= 299.0 ? vec3(0.0) : 0.5 + 0.5 * cos(3.0 + s * 0.12 + vec3(0.0, 0.6, 1.0));
    fragColor = vec4(col, 1.0);
}
