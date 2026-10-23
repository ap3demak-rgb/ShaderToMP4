// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Onde d'énergie — Une onde horizontale dont l'amplitude suit iEnergy ; l'attaque (iOnset) l'éclaire un instant.
// en: Energy wave — A horizontal wave whose amplitude follows iEnergy; the attack (iOnset) lights it for a moment.

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float en = max(iEnergy, step(iEnergy, 0.001) * (0.5 + 0.5 * sin(iTime)));
    float amp = 0.05 + 0.25 * en;
    float y = amp * sin(uv.x * 10.0 + iTime * 3.0) * sin(uv.x * 3.0 - iTime);
    float d = abs(uv.y - y);
    vec3 col = vec3(0.01, 0.02, 0.05) + vec3(0.2, 0.6, 1.0) * 0.006 / (d + 0.003) * (1.0 + 3.0 * iOnset);  // sans son : amplitude de secours
    fragColor = vec4(col, 1.0);
}
