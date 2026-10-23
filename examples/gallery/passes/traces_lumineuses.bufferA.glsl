// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float prev = texelFetch(iChannel0, ivec2(fragCoord), 0).r * 0.97;
    vec2 c = iResolution.xy * (0.5 + 0.35 * vec2(sin(iTime * 1.3), cos(iTime * 0.9 + sin(iTime * 0.5))));
    float dot_ = smoothstep(12.0, 3.0, length(fragCoord - c));
    fragColor = vec4(max(prev, dot_), 0.0, 0.0, 1.0);
}
