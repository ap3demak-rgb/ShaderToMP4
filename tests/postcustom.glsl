// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Passe finale .post.glsl : l'image (0.2, 0.4, 0.6) est inversee par postcustom.post.glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(0.2, 0.4, 0.6, 1.0);
}
