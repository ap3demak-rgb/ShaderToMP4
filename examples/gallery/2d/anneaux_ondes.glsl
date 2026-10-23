// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Anneaux concentriques — Distance à l'origine passée dans un sinus qui se déplace : des anneaux qui sortent du centre, teintés par l'angle.
// en: Concentric rings — Distance to the origin passed through a moving sine: rings leaving the centre, tinted by the angle.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv), a = atan(uv.y, uv.x);
    float v = 0.5 + 0.5 * sin(r * 40.0 - iTime * 4.0 + 2.0 * sin(a * 3.0 + iTime));
    vec3 col = 0.5 + 0.5 * cos(a + iTime + vec3(0.0, 2.0, 4.0));
    fragColor = vec4(col * v * smoothstep(0.9, 0.2, r), 1.0);
}
