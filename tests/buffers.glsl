// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Passes multiples : Buffer A (trace qui s'estompe, memoire), Buffer B (lit A de l'image courante), Common
// iChannel0=bufferA
// iChannel1=bufferB
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(texture(iChannel0, uv).r, texture(iChannel1, uv).r, float(iFrame % 7) / 7.0, 1.0);
}
