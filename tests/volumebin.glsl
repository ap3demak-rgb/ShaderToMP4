// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Volume Shadertoy .bin : 4x4x4 en octets, un canal (volume8.bin), lu sans lissage dans la tranche z = 1 ;
// 2x2x2 en flottants, trois canaux, valeurs au-dessus de 1 (volumef.bin), moitie droite de l'image
// iChannel0=volume:volume8.bin
// iChannel0.filter=nearest
// iChannel1=volume:volumef.bin
// iChannel1.filter=nearest
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 a = texture(iChannel0, vec3(uv, 0.375)).rgb;
    vec3 b = texture(iChannel1, vec3(uv, 0.75)).rgb * 0.5;
    fragColor = vec4(uv.x < 0.5 ? a.rrr : b, 1.0);
}
