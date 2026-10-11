// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(texture(iChannel0, fragCoord / iResolution.xy).r * 0.5);
}
