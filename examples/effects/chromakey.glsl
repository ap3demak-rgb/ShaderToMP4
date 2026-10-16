// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Incrustation sur fond uni (chroma key) : le fond de la couleur choisie devient transparent, la couleur parasite est retiree des bords.
// Usage : ShaderToMP4 --render examples/effects/chromakey.glsl --source=fond_vert.mp4 --codec=prores_4444 --container=mov
// alpha=1
// uniform color keyColor = #00ff00
// uniform float tolerance = 0.28 [0..1]
// uniform float softness = 0.12 [0..1]
// uniform float despill = 1.0 [0..1]
vec2 cbcr(vec3 c) {
    return vec2(dot(c, vec3(-0.168736, -0.331264, 0.5)), dot(c, vec3(0.5, -0.418688, -0.081312)));
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = texture(iChannel0, uv).rgb;
    float d = distance(cbcr(c), cbcr(keyColor));
    float a = smoothstep(tolerance, tolerance + max(softness, 1e-4), d);
    // couleur parasite : le canal de la couleur de fond est ramene a la moyenne des deux autres
    vec3 k = normalize(keyColor + 1e-4);
    float spill = max(0.0, dot(c, k) - 0.5 * (c.r + c.g + c.b - dot(c, k)));
    c -= despill * spill * k * (1.0 - a);
    fragColor = vec4(c, a);
}
