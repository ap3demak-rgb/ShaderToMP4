// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
float hh(float n) { return fract(sin(n * 91.345) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    ivec2 p = ivec2(fragCoord);
    vec2 s = texelFetch(iChannel0, p, 0).rg;
    float lap = texelFetch(iChannel0, p + ivec2(1, 0), 0).r + texelFetch(iChannel0, p - ivec2(1, 0), 0).r
              + texelFetch(iChannel0, p + ivec2(0, 1), 0).r + texelFetch(iChannel0, p - ivec2(0, 1), 0).r - 4.0 * s.r;
    float v = (s.g + 0.4 * lap) * 0.995;
    float h = s.r + v;
    int k = iFrame / 40;
    if (iFrame % 40 == 0) {
        vec2 d = iResolution.xy * vec2(0.15 + 0.7 * hh(float(k)), 0.15 + 0.7 * hh(float(k) + 0.5));
        h += 0.9 * smoothstep(8.0, 0.0, length(fragCoord - d));
    }
    if (iFrame < 2) { h = 0.0; v = 0.0; }
    fragColor = vec4(h, v, 0.0, 1.0);
}
