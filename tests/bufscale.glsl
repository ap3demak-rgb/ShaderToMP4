// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Tampon a taille propre : Buffer A rendu en 8x8 (iResolution de la passe = 8x8), relu par l'image sans lissage,
// iChannelResolution donne la taille du tampon et non celle du rendu
// bufferA.size=8x8
// iChannel0=bufferA
// iChannel0.filter=nearest
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 a = texture(iChannel0, uv).rgb;
    fragColor = vec4(a.xy, a.z * 0.5 + iChannelResolution[0].x / 32.0, 1.0);
}
