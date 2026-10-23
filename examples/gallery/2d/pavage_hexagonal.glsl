// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// fr: Pavage hexagonal — Deux grilles décalées donnent le centre de l'hexagone le plus proche ; la couleur et la hauteur suivent une onde.
// en: Hexagonal tiling — Two shifted grids give the centre of the nearest hexagon; colour and height follow a wave.

vec4 hexCell(vec2 p) {
    const vec2 s = vec2(1.0, 1.7320508);
    vec4 hc = floor(vec4(p, p - vec2(1.0, 0.5)) / s.xyxy) + 0.5;
    vec4 h = vec4(p - hc.xy * s, p - (hc.zw + 0.5) * s);
    return dot(h.xy, h.xy) < dot(h.zw, h.zw) ? vec4(h.xy, hc.xy) : vec4(h.zw, hc.zw + 0.5);
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * 9.0;
    vec4 h = hexCell(uv);
    float edge = 0.5 - max(abs(h.x) * 0.8660254 + abs(h.y) * 0.5, abs(h.y));
    float w = 0.5 + 0.5 * sin(length(h.zw) * 0.8 - iTime * 2.0);
    vec3 col = 0.5 + 0.5 * cos(w * 3.0 + vec3(0.0, 2.0, 4.0));
    col *= smoothstep(0.0, 0.06, edge) * (0.4 + 0.6 * w);
    fragColor = vec4(col, 1.0);
}
