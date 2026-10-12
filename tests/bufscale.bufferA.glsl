// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float check = mod(floor(fragCoord.x) + floor(fragCoord.y), 2.0);
    fragColor = vec4(fragCoord / iResolution.xy, check, 1.0);
}
