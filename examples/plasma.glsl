// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Plasma colore : somme de sinus, palette cosinus
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float t = iTime * 0.6;
    float v = sin(uv.x * 8.0 + t) + sin(uv.y * 9.0 - t * 1.3) + sin((uv.x + uv.y) * 7.0 + t * 0.7)
            + sin(length(uv) * 14.0 - t * 2.0);
    vec3 col = 0.5 + 0.5 * cos(6.2831 * (v * 0.18 + vec3(0.0, 0.33, 0.67)) + t);
    fragColor = vec4(col * (1.1 - 0.5 * length(uv)), 1.0);
}
