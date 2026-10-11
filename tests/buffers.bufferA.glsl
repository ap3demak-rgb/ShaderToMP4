// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec4 prev = texelFetch(iChannel0, ivec2(fragCoord), 0);
    vec2 c = iResolution.xy * 0.5 + SWING * vec2(cos(iTime * 3.0), sin(iTime * 3.0));
    fragColor = max(prev * DECAY, vec4(smoothstep(6.0, 3.0, length(fragCoord - c))));
}
