// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
float h(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    ivec2 p = ivec2(fragCoord);
    if (iFrame < 2) { fragColor = vec4(step(0.72, h(fragCoord)), 0.0, 0.0, 1.0); return; }
    float self = texelFetch(iChannel0, p, 0).r;
    if (iFrame % 6 != 0) { fragColor = vec4(self, 0.0, 0.0, 1.0); return; }
    ivec2 sz = ivec2(iResolution.xy / 4.0);
    if (p.x >= sz.x || p.y >= sz.y) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float n = 0.0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            if (i != 0 || j != 0) n += texelFetch(iChannel0, (p + ivec2(i, j) + sz) % sz, 0).r;
    float alive = (self > 0.5) ? (n == 2.0 || n == 3.0 ? 1.0 : 0.0) : (n == 3.0 ? 1.0 : 0.0);
    if (iFrame % 300 == 0) alive = max(alive, step(0.9, h(fragCoord + float(iFrame))));
    fragColor = vec4(alive, 0.0, 0.0, 1.0);
}
