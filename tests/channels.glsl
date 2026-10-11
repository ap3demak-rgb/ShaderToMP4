// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Canaux generes (bruit, grille), iMouse et iDate fixes : rendu deterministe, sans fichier ni ffmpeg
// iChannel0=grid
// iChannel0.filter=nearest
// iChannel1=noise
// iChannel1.filter=nearest
// mouse=0.25,0.75,down
// date=2026-03-14T01:02:03
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 a = texture(iChannel0, uv * 2.0).rgb;
    vec3 b = texture(iChannel1, uv).rgb;
    float down = iMouse.z > 0.0 ? 0.1 : 0.0;
    fragColor = vec4(mix(a, b, 0.5) * 0.8 + vec3(down, iDate.x / 4000.0 - 0.5, iDate.w / 90000.0), 1.0);
}
