// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Tunnel réactif — Un tunnel dont la vitesse suit iEnergy et dont les anneaux changent de couleur à chaque temps (iBeatCount).
// en: Reactive tunnel — A tunnel whose speed follows iEnergy and whose rings change colour on every beat (iBeatCount).

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv), a = atan(uv.y, uv.x);
    float z = 0.3 / r + iTime * (0.6 + 2.0 * max(iEnergy, step(iEnergy, 0.001) * 0.5));
    float ring = step(0.5, fract(z)) * step(0.3, fract(a * 4.0 / 6.2831853 + z * 0.1));
    vec3 col = 0.5 + 0.5 * cos(floor(z) * 0.7 + iBeatCount + vec3(0.0, 2.0, 4.0));
    fragColor = vec4(col * (0.15 + 0.85 * ring) * smoothstep(0.0, 0.25, r), 1.0);
}
