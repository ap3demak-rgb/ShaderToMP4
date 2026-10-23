// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    ivec2 p = ivec2(fragCoord);
    float c = texelFetch(iChannel0, p, 0).r;
    float n = texelFetch(iChannel0, p + ivec2(1, 0), 0).r + texelFetch(iChannel0, p - ivec2(1, 0), 0).r
            + texelFetch(iChannel0, p + ivec2(0, 1), 0).r + texelFetch(iChannel0, p - ivec2(0, 1), 0).r;
    float t = mix(c, n * 0.25, 0.9) * 0.9995;
    for (int i = 0; i < 2; i++) {
        vec2 s = iResolution.xy * (0.5 + 0.35 * vec2(sin(iTime * (0.7 + 0.4 * float(i)) + float(i) * 2.0), cos(iTime * (0.5 + 0.3 * float(i)))));
        t = max(t, smoothstep(14.0, 4.0, length(fragCoord - s)));
    }
    if (iFrame < 2) t = 0.0;
    fragColor = vec4(t, 0.0, 0.0, 1.0);
}
