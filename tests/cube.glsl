// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Cubemap generee (ciel) lue par direction
// iChannel0=cube:sky
// iChannel0.filter=nearest
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy * 2.0 - 1.0;
    fragColor = vec4(texture(iChannel0, normalize(vec3(uv, 1.0))).rgb, 1.0);
}
