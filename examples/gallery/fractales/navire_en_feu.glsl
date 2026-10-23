// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Navire en feu — Comme Mandelbrot mais avec les valeurs absolues des parties réelle et imaginaire avant le carré.
// en: Burning Ship — Like Mandelbrot but with the absolute values of both coordinates of z taken before squaring.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float zoom = 1.0 / (1.0 + 6.0 * (0.5 - 0.5 * cos(iTime * 0.15)));
    vec2 c = vec2(-1.755, -0.035) + (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 0.25 * zoom * vec2(1.0, -1.0);
    vec2 z = vec2(0.0);
    float n = 0.0;
    for (int i = 0; i < 250; i++) {
        z = abs(z);
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (dot(z, z) > 100.0) break;
        n += 1.0;
    }
    vec3 col = n >= 249.0 ? vec3(0.0) : vec3(1.0, 0.45, 0.1) * pow(n / 250.0, 0.4) * 1.5;
    col += vec3(0.0, 0.1, 0.4) * (1.0 - n / 250.0);
    fragColor = vec4(col, 1.0);
}
