// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Texture 3D generee (bruit) lue en une coupe
// iChannel0=volume:noise
// iChannel0.filter=nearest
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(texture(iChannel0, vec3(uv, 0.5)).rgb, 1.0);
}
