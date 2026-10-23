// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Éclair à chaque attaque — iOnset vaut 1 à chaque attaque puis décroît vite : il règle la luminosité d'un motif de rayons.
// en: Flash on every attack — iOnset is 1 at every attack then decays fast: it sets the brightness of a pattern of rays.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float a = atan(uv.y, uv.x);
    float rays = 0.5 + 0.5 * cos(a * 12.0 + iTime);
    float on = max(iOnset, step(iEnergy + iBass, 0.001) * pow(1.0 - fract(iTime * 2.0), 4.0));
    float v = (0.15 + 0.85 * on) * pow(rays, 3.0) / (length(uv) * 3.0 + 0.2);
    fragColor = vec4(vec3(1.0, 0.7, 0.3) * v + vec3(0.0, 0.0, 0.05), 1.0);
}
