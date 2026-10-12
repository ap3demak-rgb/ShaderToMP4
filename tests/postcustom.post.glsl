// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// uniform float strength = 1.0 [0..1]
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec3 c = texture(iChannel0, fragCoord / iResolution.xy).rgb;
    fragColor = vec4(mix(c, 1.0 - c, strength), 1.0);
}
