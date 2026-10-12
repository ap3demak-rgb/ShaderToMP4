// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// iChannel0=bufferA
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(texture(iChannel0, vec2(0.5)).r + 1.0 / 255.0, 0.0, 0.0, 1.0);
}
